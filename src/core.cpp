// SPDX-License-Identifier: Apache-2.0
#include "clicknova/core.hpp"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <type_traits>
#include <unordered_set>

namespace clicknova {
namespace {
constexpr Milliseconds kMaxSingleWait = std::chrono::duration_cast<Milliseconds>(std::chrono::hours{24 * 7});

bool finiteValue(const Value& value) {
    if (auto number = std::get_if<double>(&value)) return std::isfinite(*number);
    return true;
}

template <class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

bool validateCondition(const Condition& condition, std::uint32_t depth,
                       std::uint32_t limit, std::string& reason) {
    if (depth > limit) { reason = "condition nesting limit exceeded"; return false; }
    return std::visit(Overloaded{
        [&](const VariableEquals& c) {
            if (c.name.empty() || !finiteValue(c.expected)) {
                reason = "invalid variable comparison"; return false;
            }
            return true;
        },
        [&](const Probe& p) {
            if (p.target.key.empty()) { reason = "empty target key"; return false; }
            if (p.kind != ProbeKind::WindowExists && p.query.empty()) {
                reason = "empty probe query"; return false;
            }
            if (p.kind == ProbeKind::ImageMatches &&
                (!std::isfinite(p.minConfidence) || p.minConfidence <= 0 ||
                 p.minConfidence > 1)) {
                reason = "invalid image threshold"; return false;
            }
            return true;
        },
        [&](const All& c) {
            if (c.items.empty()) { reason = "empty AND"; return false; }
            for (const auto& item : c.items) {
                if (!item || !validateCondition(*item, depth + 1, limit, reason)) {
                    if (reason.empty()) reason = "null AND operand";
                    return false;
                }
            }
            return true;
        },
        [&](const Any& c) {
            if (c.items.empty()) { reason = "empty OR"; return false; }
            for (const auto& item : c.items) {
                if (!item || !validateCondition(*item, depth + 1, limit, reason)) {
                    if (reason.empty()) reason = "null OR operand";
                    return false;
                }
            }
            return true;
        },
        [&](const Not& c) {
            if (!c.item) { reason = "null NOT operand"; return false; }
            return validateCondition(*c.item, depth + 1, limit, reason);
        }
    }, condition.expression);
}

bool validateSteps(const std::vector<Step>& steps, std::uint32_t depth,
                   std::uint32_t limit, std::unordered_set<std::string>& ids,
                   std::string& reason) {
    if (depth > limit) { reason = "step nesting limit exceeded"; return false; }
    for (const auto& step : steps) {
        if (step.id.empty() || !ids.insert(step.id).second) {
            reason = "empty or duplicate step id"; return false;
        }
        bool good = std::visit(Overloaded{
            [&](const Delay& x) {
                if (x.duration.count() < 0 || x.duration > kMaxSingleWait) {
                    reason = "delay outside supported range"; return false;
                }
                return true;
            },
            [&](const Assign& x) {
                if (x.name.empty() || !finiteValue(x.value)) {
                    reason = "invalid assignment"; return false;
                }
                return true;
            },
            [&](const Perform& x) {
                if (x.action.target.key.empty()) {
                    reason = "empty action target"; return false;
                }
                if (x.action.kind != ActionKind::KeyChord && x.action.selector.empty()) {
                    reason = "empty action selector"; return false;
                }
                return true;
            },
            [&](const IfElse& x) {
                return validateCondition(x.condition, 0, limit, reason) &&
                       validateSteps(x.thenSteps, depth + 1, limit, ids, reason) &&
                       validateSteps(x.elseSteps, depth + 1, limit, ids, reason);
            },
            [&](const Repeat& x) {
                if (x.count == 0) { reason = "repeat count must be positive"; return false; }
                return validateSteps(x.steps, depth + 1, limit, ids, reason);
            },
            [&](const WaitUntil& x) {
                if (x.pollInterval.count() <= 0 || x.timeout.count() < 0 ||
                    x.pollInterval > kMaxSingleWait || x.timeout > kMaxSingleWait) {
                    reason = "invalid polling interval or timeout"; return false;
                }
                return validateCondition(x.condition, 0, limit, reason);
            },
            [&](const Note&) { return true; }
        }, step.operation);
        if (!good) return false;
    }
    return true;
}

ErrorCode mapProbeFailure(ProbeStatus s) {
    switch (s) {
    case ProbeStatus::Unsupported: return ErrorCode::Unsupported;
    case ProbeStatus::PermissionDenied: return ErrorCode::PermissionDenied;
    case ProbeStatus::TargetUnavailable: return ErrorCode::TargetUnavailable;
    default: return ErrorCode::ProbeFailed;
    }
}
ErrorCode mapActionFailure(ProviderStatus s) {
    switch (s) {
    case ProviderStatus::Unsupported: return ErrorCode::Unsupported;
    case ProviderStatus::PermissionDenied: return ErrorCode::PermissionDenied;
    case ProviderStatus::TargetUnavailable: return ErrorCode::TargetUnavailable;
    default: return ErrorCode::ActionFailed;
    }
}
} // namespace

ClockPoint SteadyClock::now() const { return std::chrono::steady_clock::now(); }
bool SteadyClock::waitFor(Milliseconds duration, std::stop_token stop) {
    if (stop.stop_requested()) return false;
    std::mutex mutex;
    std::condition_variable_any cv;
    std::unique_lock lock(mutex);
    cv.wait_for(lock, stop, duration, [] { return false; });
    return !stop.stop_requested();
}

struct Runner::Execution {
    std::string scenarioId;
    std::stop_token stop;
    RunResult result;
};

Runner::Runner(IAutomationProvider& provider, IClock& clock,
               IEventSink& events, RunLimits limits)
    : provider_(provider), clock_(clock), events_(events), limits_(limits) {}

void Runner::emit(EventType type, const Execution& state,
                  const std::string& stepId, const std::string& detail) noexcept {
    events_.publish(Event{type, state.scenarioId, stepId, detail});
}

void Runner::fail(Execution& state, ErrorCode code,
                  const std::string& stepId, std::string message) {
    if (state.result.error) return;
    state.result.error = RunError{code, stepId, std::move(message)};
    state.result.status = code == ErrorCode::Cancelled
        ? RunStatus::Cancelled : RunStatus::Failed;
    emit(EventType::Error, state, stepId, state.result.error->message);
}

bool Runner::checkStop(Execution& state, const std::string& stepId) {
    if (!state.stop.stop_requested()) return true;
    fail(state, ErrorCode::Cancelled, stepId, "execution cancelled");
    return false;
}

bool Runner::validate(const Scenario& scenario, std::string& reason) const {
    if (scenario.schemaVersion != 1) { reason = "unsupported schema version"; return false; }
    if (scenario.id.empty() || scenario.name.empty()) {
        reason = "scenario requires id and name"; return false;
    }
    if (limits_.maxExecutedSteps == 0 || limits_.maxNesting == 0) {
        reason = "invalid runner limits"; return false;
    }
    std::unordered_set<std::string> ids;
    return validateSteps(scenario.steps, 0, limits_.maxNesting, ids, reason);
}

RunResult Runner::run(const Scenario& scenario, std::stop_token stop) {
    Execution state{scenario.id, stop, {}};
    std::string reason;
    if (!validate(scenario, reason)) {
        fail(state, ErrorCode::InvalidScenario, {}, std::move(reason));
        emit(EventType::Finished, state, {}, "failed validation");
        return std::move(state.result);
    }
    if (!checkStop(state, {})) {
        emit(EventType::Finished, state, {}, "cancelled");
        return std::move(state.result);
    }
    emit(EventType::Started, state, {}, "scenario started");
    if (executeSteps(scenario.steps, state, 0)) {
        state.result.status = RunStatus::Completed;
    }
    emit(EventType::Finished, state, {},
         state.result.status == RunStatus::Completed ? "completed" : "stopped");
    return std::move(state.result);
}

std::optional<bool> Runner::evaluate(const Condition& condition, Execution& state,
                                     const std::string& stepId,
                                     std::uint32_t depth) {
    if (!checkStop(state, stepId)) return std::nullopt;
    if (depth > limits_.maxNesting) {
        fail(state, ErrorCode::InvalidScenario, stepId, "condition nesting exceeded");
        return std::nullopt;
    }
    const auto evaluation = std::visit(Overloaded{
        [&](const VariableEquals& x) -> std::optional<bool> {
            auto it = state.result.variables.find(x.name);
            return it != state.result.variables.end() && it->second == x.expected;
        },
        [&](const Probe& p) -> std::optional<bool> {
            try {
                const auto response = provider_.probe(p, state.stop);
                if (response.status == ProbeStatus::Matched) return true;
                if (response.status == ProbeStatus::NotMatched) return false;
                fail(state, mapProbeFailure(response.status), stepId,
                     response.message.empty() ? "probe unavailable or failed" : response.message);
            } catch (const std::exception&) {
                fail(state, ErrorCode::ProbeFailed, stepId, "probe provider threw an exception");
            } catch (...) {
                fail(state, ErrorCode::ProbeFailed, stepId, "probe provider failed");
            }
            return std::nullopt;
        },
        [&](const All& c) -> std::optional<bool> {
            for (const auto& item : c.items) {
                auto result = evaluate(*item, state, stepId, depth + 1);
                if (!result || !*result) return result;
            }
            return true;
        },
        [&](const Any& c) -> std::optional<bool> {
            for (const auto& item : c.items) {
                auto result = evaluate(*item, state, stepId, depth + 1);
                if (!result) return std::nullopt;
                if (*result) return true;
            }
            return false;
        },
        [&](const Not& c) -> std::optional<bool> {
            auto result = evaluate(*c.item, state, stepId, depth + 1);
            return result ? std::optional<bool>{!*result} : std::nullopt;
        }
    }, condition.expression);
    if (evaluation) emit(EventType::ConditionChecked, state, stepId,
                         *evaluation ? "true" : "false");
    return evaluation;
}

bool Runner::executeSteps(const std::vector<Step>& steps,
                          Execution& state, std::uint32_t depth) {
    if (depth > limits_.maxNesting) {
        fail(state, ErrorCode::InvalidScenario, {}, "step nesting exceeded");
        return false;
    }
    for (const auto& step : steps) {
        if (!checkStop(state, step.id)) return false;
        if (state.result.executedSteps >= limits_.maxExecutedSteps) {
            fail(state, ErrorCode::StepLimitExceeded, step.id,
                 "execution step budget exhausted");
            return false;
        }
        ++state.result.executedSteps;
        emit(EventType::StepStarted, state, step.id, "start");
        if (!executeStep(step, state, depth)) return false;
        if (!checkStop(state, step.id)) return false;
        emit(EventType::StepCompleted, state, step.id, "done");
    }
    return true;
}

bool Runner::executeStep(const Step& step, Execution& state, std::uint32_t depth) {
    return std::visit(Overloaded{
        [&](const Delay& x) {
            if (clock_.waitFor(x.duration, state.stop)) return true;
            return checkStop(state, step.id);
        },
        [&](const Assign& x) {
            state.result.variables.insert_or_assign(x.name, x.value);
            return true;
        },
        [&](const Perform& x) {
            try {
                const auto response = provider_.perform(x.action, state.stop);
                if (response.status == ProviderStatus::Success) return true;
                fail(state, mapActionFailure(response.status), step.id,
                     response.message.empty() ? "action unsupported or failed" : response.message);
            } catch (const std::exception&) {
                fail(state, ErrorCode::ActionFailed, step.id, "action provider threw an exception");
            } catch (...) {
                fail(state, ErrorCode::ActionFailed, step.id, "action provider failed");
            }
            return false;
        },
        [&](const IfElse& x) {
            auto result = evaluate(x.condition, state, step.id, 0);
            if (!result) return false;
            return executeSteps(*result ? x.thenSteps : x.elseSteps, state, depth + 1);
        },
        [&](const Repeat& x) {
            for (std::uint32_t i = 0; i < x.count; ++i) {
                if (!checkStop(state, step.id) ||
                    !executeSteps(x.steps, state, depth + 1)) return false;
            }
            return true;
        },
        [&](const WaitUntil& x) {
            const auto deadline = clock_.now() + x.timeout;
            while (true) {
                auto result = evaluate(x.condition, state, step.id, 0);
                if (!result) return false;
                if (*result) return true;
                const auto remaining = std::chrono::duration_cast<Milliseconds>(deadline - clock_.now());
                if (remaining <= Milliseconds::zero()) {
                    fail(state, ErrorCode::Timeout, step.id, "condition timed out");
                    return false;
                }
                if (!clock_.waitFor(std::min(x.pollInterval, remaining), state.stop)) {
                    return checkStop(state, step.id);
                }
            }
        },
        [&](const Note& x) {
            emit(EventType::Note, state, step.id, x.message);
            return true;
        }
    }, step.operation);
}

} // namespace clicknova
