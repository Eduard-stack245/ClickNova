// SPDX-License-Identifier: Apache-2.0
#include "clicknova/core.hpp"

#include <deque>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

using namespace clicknova;

#define CHECK(x) do { if (!(x)) { \
    throw std::runtime_error(std::string("CHECK failed: ") + #x + \
                             " at line " + std::to_string(__LINE__)); \
} } while (false)

namespace {
struct FakeClock final : IClock {
    ClockPoint time{};
    std::function<void()> onWait;
    ClockPoint now() const override { return time; }
    bool waitFor(Milliseconds duration, std::stop_token stop) override {
        if (stop.stop_requested()) return false;
        time += duration;
        if (onWait) onWait();
        return !stop.stop_requested();
    }
};

struct FakeProvider final : IAutomationProvider {
    std::deque<ProbeResponse> replies;
    std::vector<UiAction> actions;
    ProviderStatus actionStatus{ProviderStatus::Success};
    bool throwOnAction{false};
    bool throwOnProbe{false};
    std::function<void()> onAction;
    std::size_t probeCount{0};
    ProbeResponse probe(const Probe&, std::stop_token) override {
        ++probeCount;
        if (throwOnProbe) throw std::runtime_error("simulated provider failure");
        if (replies.empty()) return {ProbeStatus::NotMatched, {}};
        auto first = replies.front();
        replies.pop_front();
        return first;
    }
    ActionResponse perform(const UiAction& action, std::stop_token) override {
        actions.push_back(action);
        if (onAction) onAction();
        if (throwOnAction) throw std::runtime_error("simulated provider failure");
        return {actionStatus, actionStatus == ProviderStatus::Success ? "" : "not supported"};
    }
};

struct Events final : IEventSink {
    std::vector<Event> items;
    void publish(const Event& event) noexcept override { items.push_back(event); }
};

Target browser() { return {TargetKind::BrowserTab, "my-tab"}; }
UiAction click() {
    return {ActionKind::ClickElement, browser(), "#download", {}};
}
Condition flag() {
    return Condition{VariableEquals{"enabled", Value{true}}};
}
Condition statusProbe() {
    return Condition{Probe{ProbeKind::TextContains, browser(), "#status", "Done", 0.9}};
}
Scenario makeScenario(std::vector<Step> steps) {
    return {1, "test", "Unit test", std::move(steps)};
}

void testIfRepeatAndVariables() {
    FakeProvider provider;
    FakeClock clock;
    Events events;
    Scenario scenario = makeScenario({
        {"assign", Assign{"enabled", Value{true}}},
        {"branch", IfElse{flag(),
            {{"then-click", Perform{click()}}},
            {{"else-note", Note{"unreachable"}}}}},
        {"loop", Repeat{3, {{"repeat-click", Perform{click()}}}}}
    });
    Runner runner(provider, clock, events);
    auto result = runner.run(scenario);
    CHECK(result.status == RunStatus::Completed);
    CHECK(!result.error.has_value());
    CHECK(result.executedSteps == 7);
    CHECK(provider.actions.size() == 4);
    CHECK(result.variables.at("enabled") == Value{true});
    CHECK(events.items.front().type == EventType::Started);
    CHECK(events.items.back().type == EventType::Finished);
}

void testWaitConditionEventuallyMatches() {
    FakeProvider provider;
    FakeClock clock;
    Events events;
    provider.replies = {{ProbeStatus::NotMatched, {}},
                        {ProbeStatus::NotMatched, {}},
                        {ProbeStatus::Matched, {}}};
    Scenario scenario = makeScenario({
        {"wait", WaitUntil{statusProbe(), Milliseconds{100}, Milliseconds{500}}},
        {"click", Perform{click()}}
    });
    Runner runner(provider, clock, events);
    const auto result = runner.run(scenario);
    CHECK(result.status == RunStatus::Completed);
    CHECK(provider.probeCount == 3);
    CHECK(provider.actions.size() == 1);
    CHECK(clock.time.time_since_epoch() == Milliseconds{200});
}

void testTimeoutDistinctFromNotMatched() {
    FakeProvider provider;
    FakeClock clock;
    Events events;
    Scenario scenario = makeScenario({
        {"wait", WaitUntil{statusProbe(), Milliseconds{100}, Milliseconds{250}}}
    });
    Runner runner(provider, clock, events);
    const auto result = runner.run(scenario);
    CHECK(result.status == RunStatus::Failed);
    CHECK(result.error && result.error->code == ErrorCode::Timeout);
    CHECK(result.error->stepId == "wait");
    CHECK(provider.probeCount == 4);
    CHECK(clock.time.time_since_epoch() == Milliseconds{250});
}

void testUnsupportedActionFailsWithoutFallback() {
    FakeProvider provider;
    provider.actionStatus = ProviderStatus::Unsupported;
    FakeClock clock;
    Events events;
    Scenario scenario = makeScenario({
        {"click", Perform{click()}}, {"unused", Note{"must not run"}}
    });
    Runner runner(provider, clock, events);
    const auto result = runner.run(scenario);
    CHECK(result.status == RunStatus::Failed);
    CHECK(result.error && result.error->code == ErrorCode::Unsupported);
    CHECK(result.executedSteps == 1);
    CHECK(provider.actions.size() == 1);
}

void testCancellationInterruptsWait() {
    FakeProvider provider;
    FakeClock clock;
    Events events;
    std::stop_source source;
    clock.onWait = [&] { source.request_stop(); };
    Scenario scenario = makeScenario({
        {"delay", Delay{Milliseconds{5000}}},
        {"later", Perform{click()}}
    });
    Runner runner(provider, clock, events);
    const auto result = runner.run(scenario, source.get_token());
    CHECK(result.status == RunStatus::Cancelled);
    CHECK(result.error && result.error->code == ErrorCode::Cancelled);
    CHECK(provider.actions.empty());
}

void testInvalidIdsAreRejectedBeforeExecution() {
    FakeProvider provider;
    FakeClock clock;
    Events events;
    Scenario scenario = makeScenario({
        {"dup", Perform{click()}}, {"dup", Perform{click()}}
    });
    Runner runner(provider, clock, events);
    const auto result = runner.run(scenario);
    CHECK(result.status == RunStatus::Failed);
    CHECK(result.error && result.error->code == ErrorCode::InvalidScenario);
    CHECK(provider.actions.empty());
}

void testRunBudgetLimitsRepeats() {
    FakeProvider provider;
    FakeClock clock;
    Events events;
    Scenario scenario = makeScenario({
        {"loop", Repeat{9999, {{"inside", Perform{click()}}}}}
    });
    Runner runner(provider, clock, events, RunLimits{2, 32});
    const auto result = runner.run(scenario);
    CHECK(result.status == RunStatus::Failed);
    CHECK(result.error && result.error->code == ErrorCode::StepLimitExceeded);
    CHECK(result.executedSteps == 2);
    CHECK(provider.actions.size() == 1);
}

void testCancellationDuringFinalAction() {
    FakeProvider provider;
    FakeClock clock;
    Events events;
    std::stop_source source;
    provider.onAction = [&] { source.request_stop(); };
    Scenario scenario = makeScenario({{"click", Perform{click()}}});
    Runner runner(provider, clock, events);
    const auto result = runner.run(scenario, source.get_token());
    CHECK(result.status == RunStatus::Cancelled);
    CHECK(result.error && result.error->code == ErrorCode::Cancelled);
}

void testProviderExceptionsBecomeErrors() {
    FakeProvider provider;
    provider.throwOnAction = true;
    FakeClock clock;
    Events events;
    Scenario scenario = makeScenario({{"click", Perform{click()}}});
    Runner runner(provider, clock, events);
    const auto result = runner.run(scenario);
    CHECK(result.status == RunStatus::Failed);
    CHECK(result.error && result.error->code == ErrorCode::ActionFailed);

    FakeProvider probeProvider;
    probeProvider.throwOnProbe = true;
    Scenario probeScenario = makeScenario({
        {"check", IfElse{statusProbe(), {}, {}}}
    });
    Runner probeRunner(probeProvider, clock, events);
    const auto probeResult = probeRunner.run(probeScenario);
    CHECK(probeResult.status == RunStatus::Failed);
    CHECK(probeResult.error && probeResult.error->code == ErrorCode::ProbeFailed);
}

void testCompositeConditions() {
    FakeProvider provider;
    FakeClock clock;
    Events events;
    auto a = std::make_shared<Condition>(Condition{VariableEquals{"x", Value{std::int64_t{3}}}});
    auto b = std::make_shared<Condition>(Condition{VariableEquals{"y", Value{true}}});
    Condition composite{All{{a, std::make_shared<Condition>(Condition{Not{b}})}}};
    Scenario scenario = makeScenario({
        {"x", Assign{"x", Value{std::int64_t{3}}}},
        {"y", Assign{"y", Value{false}}},
        {"if", IfElse{composite, {{"yes", Note{"yes"}}}, {{"no", Note{"no"}}}}}
    });
    Runner runner(provider, clock, events);
    const auto result = runner.run(scenario);
    CHECK(result.status == RunStatus::Completed);
    bool sawYes = false, sawNo = false;
    for (const auto& e : events.items) {
        if (e.type == EventType::Note && e.detail == "yes") sawYes = true;
        if (e.type == EventType::Note && e.detail == "no") sawNo = true;
    }
    CHECK(sawYes && !sawNo);
}

void testProbeFailureNeverMeansConditionFalse() {
    FakeProvider provider;
    provider.replies = {{ProbeStatus::PermissionDenied, "capture permission missing"}};
    FakeClock clock;
    Events events;
    Scenario scenario = makeScenario({
        {"if", IfElse{Condition{Not{std::make_shared<Condition>(statusProbe())}},
                         {{"wrong", Perform{click()}}}, {}}}
    });
    Runner runner(provider, clock, events);
    const auto result = runner.run(scenario);
    CHECK(result.status == RunStatus::Failed);
    CHECK(result.error && result.error->code == ErrorCode::PermissionDenied);
    CHECK(provider.actions.empty());
}
}

int main() {
    using Test = std::pair<const char*, void (*)()>;
    const Test tests[] = {
        {"IF/REPEAT/variables", testIfRepeatAndVariables},
        {"WAIT UNTIL succeeds", testWaitConditionEventuallyMatches},
        {"WAIT UNTIL timeout", testTimeoutDistinctFromNotMatched},
        {"unsupported no fallback", testUnsupportedActionFailsWithoutFallback},
        {"cancel wait", testCancellationInterruptsWait},
        {"reject invalid ids", testInvalidIdsAreRejectedBeforeExecution},
        {"repetition step budget", testRunBudgetLimitsRepeats},
        {"cancel final action", testCancellationDuringFinalAction},
        {"provider exceptions", testProviderExceptionsBecomeErrors},
        {"AND/NOT conditions", testCompositeConditions},
        {"probe failure not false", testProbeFailureNeverMeansConditionFalse},
    };
    int failures = 0;
    for (const auto& [name, fn] : tests) {
        try { fn(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) {
            ++failures;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    }
    std::cout << "RESULT " << (std::size(tests) - failures)
              << "/" << std::size(tests) << " passed\n";
    return failures == 0 ? 0 : 1;
}
