// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace clicknova {

using Milliseconds = std::chrono::milliseconds;
using ClockPoint = std::chrono::steady_clock::time_point;
using Value = std::variant<bool, std::int64_t, double, std::string>;
using Variables = std::unordered_map<std::string, Value>;

enum class TargetKind { Desktop, Window, BrowserTab };
struct Target { TargetKind kind; std::string key; };

enum class ProbeKind { WindowExists, ElementExists, TextContains, ImageMatches };
struct Probe {
    ProbeKind kind;
    Target target;
    std::string query;       // e.g. accessibility id, CSS selector or image asset id
    std::string expected;    // for TextContains only
    double minConfidence{0.9}; // for ImageMatches only
};

struct Condition;
using ConditionPtr = std::shared_ptr<const Condition>;
struct VariableEquals { std::string name; Value expected; };
struct All { std::vector<ConditionPtr> items; };
struct Any { std::vector<ConditionPtr> items; };
struct Not { ConditionPtr item; };
struct Condition {
    std::variant<VariableEquals, Probe, All, Any, Not> expression;
};

enum class ActionKind { ClickElement, TypeText, KeyChord };
struct UiAction {
    ActionKind kind;
    Target target;
    std::string selector; // locator is interpreted only by the matching adapter
    std::string payload;  // text or key-chord; never copied into event logs
};

struct Step;
struct Delay { Milliseconds duration; };
struct Assign { std::string name; Value value; };
struct Perform { UiAction action; };
struct IfElse {
    Condition condition;
    std::vector<Step> thenSteps;
    std::vector<Step> elseSteps;
};
struct Repeat { std::uint32_t count; std::vector<Step> steps; };
struct WaitUntil {
    Condition condition;
    Milliseconds pollInterval;
    Milliseconds timeout;
};
struct Note { std::string message; };
struct Step {
    std::string id;
    std::variant<Delay, Assign, Perform, IfElse, Repeat, WaitUntil, Note> operation;
};
struct Scenario {
    std::uint32_t schemaVersion{1};
    std::string id;
    std::string name;
    std::vector<Step> steps;
};

// Providers MUST NOT silently fall back from a background target to global input.
enum class ProviderStatus {
    Success, Unsupported, PermissionDenied, TargetUnavailable, Failed
};
struct ActionResponse { ProviderStatus status; std::string message; };
enum class ProbeStatus {
    Matched, NotMatched, Unsupported, PermissionDenied, TargetUnavailable, Failed
};
struct ProbeResponse { ProbeStatus status; std::string message; };

class IAutomationProvider {
public:
    virtual ~IAutomationProvider() = default;
    virtual ProbeResponse probe(const Probe& probe, std::stop_token stop) = 0;
    virtual ActionResponse perform(const UiAction& action, std::stop_token stop) = 0;
};

class IClock {
public:
    virtual ~IClock() = default;
    virtual ClockPoint now() const = 0;
    // Return false on cancellation. Implementations must not busy-spin.
    virtual bool waitFor(Milliseconds duration, std::stop_token stop) = 0;
};

class SteadyClock final : public IClock {
public:
    ClockPoint now() const override;
    bool waitFor(Milliseconds duration, std::stop_token stop) override;
};

enum class EventType { Started, StepStarted, StepCompleted, ConditionChecked,
                       Note, Error, Finished };
struct Event {
    EventType type;
    std::string scenarioId;
    std::string stepId;
    std::string detail; // no payloads, typed text or secrets
};
class IEventSink {
public:
    virtual ~IEventSink() = default;
    // Implementations must be quick and must not throw.
    virtual void publish(const Event& event) noexcept = 0;
};

class NullEventSink final : public IEventSink {
public:
    void publish(const Event&) noexcept override {}
};

enum class ErrorCode { InvalidScenario, Cancelled, Unsupported, PermissionDenied,
                       TargetUnavailable, ProbeFailed, ActionFailed, Timeout,
                       StepLimitExceeded };
struct RunError { ErrorCode code; std::string stepId; std::string message; };
enum class RunStatus { Completed, Cancelled, Failed };
struct RunResult {
    RunStatus status{RunStatus::Failed};
    std::optional<RunError> error;
    std::uint64_t executedSteps{0};
    Variables variables;
};
struct RunLimits {
    std::uint64_t maxExecutedSteps{100'000};
    std::uint32_t maxNesting{32};
};

// A Runner is single-use-at-a-time; execute distinct instances concurrently.
class Runner {
public:
    Runner(IAutomationProvider& provider, IClock& clock,
           IEventSink& events, RunLimits limits = {});
    RunResult run(const Scenario& scenario, std::stop_token stop = {});

private:
    struct Execution;
    IAutomationProvider& provider_;
    IClock& clock_;
    IEventSink& events_;
    RunLimits limits_;

    bool validate(const Scenario& scenario, std::string& reason) const;
    bool executeSteps(const std::vector<Step>& steps, Execution& state,
                      std::uint32_t depth);
    bool executeStep(const Step& step, Execution& state, std::uint32_t depth);
    std::optional<bool> evaluate(const Condition& condition, Execution& state,
                                 const std::string& stepId,
                                 std::uint32_t depth);
    bool checkStop(Execution& state, const std::string& stepId);
    void fail(Execution& state, ErrorCode code,
              const std::string& stepId, std::string message);
    void emit(EventType type, const Execution& state,
              const std::string& stepId, const std::string& detail) noexcept;
};

} // namespace clicknova
