// SPDX-License-Identifier: Apache-2.0
#include "clicknova/input_actions.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
using namespace clicknova::input;
namespace {
struct Backend final : IBackend {
    int resolved = 0, invoked = 0, doubled = 0, scrolled = 0, typed = 0, sent = 0;
    int windowAttempts = 0, windowAfter = 0;
    int revealAfter = 0;
    bool reject = false;
    std::u16string text;
    bool dispatch(const Event&) override { ++sent; return !reject; }
    bool prepareStepWindow(const StepWindow& s, std::stop_token t) override {
        ++windowAttempts;
        // Probes of a missing window may not themselves wait for the full step timeout.
        assert(s.waitMs == 0 || windowAfter == 0);
        return !t.stop_requested() && windowAttempts > windowAfter;
    }
    bool invokeElement(const StepWindow& s, std::stop_token t) override {
        ++invoked; return !reject && !t.stop_requested() && !s.elementAutomationId.empty();
    }
    bool doubleClickElement(const StepWindow& s, int interval, std::stop_token t) override {
        ++doubled; return !reject && !t.stop_requested() && interval > 0 &&
            !s.elementAutomationId.empty();
    }
    bool elementPresent(const StepWindow& s, std::stop_token t) override {
        ++resolved; return !reject && !t.stop_requested() &&
            !s.elementAutomationId.empty() && resolved > revealAfter;
    }
    bool scrollElement(const StepWindow& s, Axis, int amount, std::stop_token t) override {
        ++scrolled; return !reject && !t.stop_requested() && amount && !s.elementAutomationId.empty();
    }
    bool setElementText(const StepWindow& s, const std::u16string& value, std::stop_token t) override {
        ++typed; text = value; return !reject && !t.stop_requested() && !s.elementAutomationId.empty();
    }
};
struct Clock final : IWaiter {
    int ticks = 0;
    std::stop_source* cancel = nullptr;
    bool wait(std::int64_t, std::stop_token t) override {
        ++ticks; if (cancel) cancel->request_stop();
        return !t.stop_requested() && cancel == nullptr;
    }
};
StepWindow selector(int provider = 0, int role = 1, int op = 0) {
    StepWindow s;
    s.title = "Demo"; s.process = "demo.exe"; s.activate = true;
    s.enforce = true; s.elementName = "Run"; s.elementAutomationId = "run";
    s.elementProvider = provider; s.elementRole = role; s.elementOperation = op;
    return s;
}
Plan planFor(ElementTask action, StepWindow binding) {
    Plan p;
    p.actions.push_back(std::move(action));
    p.stepWindows.push_back(std::move(binding));
    return p;
}
}
int main() {
    static_assert(std::is_constructible_v<Action, ElementTask>);
    Backend backend;
    Clock clock;
    // Native action, double activation and provider-specific scrolling.
    auto p = planFor({ElementCommand::Activate}, selector());
    assert(validate(p).empty());
    auto r = play(p, backend, clock);
    assert(r.status == Status::Completed && backend.invoked == 1 && backend.sent == 0 && r.eventsSent == 1);
    p.actions[0] = ElementTask{ElementCommand::DoubleActivate};
    assert(validate(p).empty());
    r = play(p, backend, clock);
    assert(r.status == Status::Completed && backend.invoked == 1 && backend.doubled == 1 && backend.sent == 0 && r.eventsSent == 2);
    p = planFor({ElementCommand::DoubleActivate}, selector(0, 8, 0));
    assert(validate(p).empty()); // list items are valid genuine double-click targets
    r = play(p, backend, clock);
    assert(r.status == Status::Completed && backend.doubled == 2 && backend.sent == 0);
    p = planFor({ElementCommand::SetText, u"Привет!"}, selector(0, 2, 1));
    assert(validate(p).empty());
    r = play(p, backend, clock);
    assert(r.status == Status::Completed && backend.typed == 1 && backend.text == u"Привет!" && backend.sent == 0);
    p = planFor({ElementCommand::Scroll, u"", Axis::Vertical, -240}, selector(0, 0, 10));
    assert(validate(p).empty());
    r = play(p, backend, clock);
    assert(r.status == Status::Completed && backend.scrolled == 1 && backend.sent == 0);
    p.stepWindows[0].elementProvider = 1; // MSAA ScrollPattern is not UIA.
    assert(!validate(p).empty());
    p = planFor({ElementCommand::AssertVisible}, selector(0, 12));
    assert(validate(p).empty());
    backend.resolved = 0;
    backend.revealAfter = 0;
    r = play(p, backend, clock);
    assert(r.status == Status::Completed && backend.resolved == 1 && backend.sent == 0 && r.eventsSent == 0);
    backend.reject = true;
    r = play(p, backend, clock);
    assert(r.status == Status::DispatchFailed && backend.sent == 0);
    backend.reject = false;
    backend.revealAfter = 2;
    backend.resolved = 0;
    p = planFor({ElementCommand::WaitVisible, u"", Axis::Vertical, 120, 2000, 100}, selector());
    assert(validate(p).empty());
    r = play(p, backend, clock);
    assert(r.status == Status::Completed && backend.resolved == 3 && clock.ticks >= 2);
    // The window is absent for two polls, then appears. The executor must
    // retry preparation under the ELEMENT timeout before reading the element.
    backend.windowAttempts = 0;
    backend.windowAfter = 2;
    backend.resolved = 0;
    backend.revealAfter = 0;
    p.stepWindows[0].waitMs = 1000;
    p.requireTargetWindow = true; // An absent target can be acquired by the waiting step.
    r = play(p, backend, clock);
    assert(r.status == Status::Completed && backend.windowAttempts == 3 &&
        backend.resolved == 1 && backend.sent == 0);
    backend.windowAfter = 0;
    // Cancellation occurs during the polling wait, not after a later click.
    std::stop_source stop;
    clock.cancel = &stop;
    backend.revealAfter = 999;
    backend.resolved = 0;
    r = play(p, backend, clock, stop.get_token());
    assert(r.status == Status::Cancelled && backend.sent == 0);
    clock.cancel = nullptr;
    p.actions[0] = ElementTask{ElementCommand::SetText, u""};
    p.stepWindows[0].elementOperation = 1;
    p.stepWindows[0].elementRole = 2;
    assert(!validate(p).empty());
    // Visual text entry is an explicitly foreground, selector-bound input.
    p = planFor({ElementCommand::SetText, u"play"}, selector(3, 1, 0));
    p.stepWindows[0].elementAutomationId = "cv:template.png";
    p.stepWindows[0].elementClass = "v1:5000,5000";
    assert(validate(p).empty());
    p.stepWindows[0].activate = false;
    assert(!validate(p).empty());
    // Foreground-only MSAA scrolling is valid, but background wheel is refused.
    p = planFor({ElementCommand::Scroll, u"", Axis::Vertical, -120}, selector(1, 0, 10));
    p.stepWindows[0].elementAutomationId = "msaa/d0";
    assert(validate(p).empty());
    p.stepWindows[0].activate = false;
    assert(!validate(p).empty());
    // No selector may become a raw input operation, including after loading.
    p.stepWindows[0].elementAutomationId.clear();
    p.stepWindows[0].elementName.clear();
    assert(!validate(p).empty());
    return 0;
}
