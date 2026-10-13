// SPDX-License-Identifier: Apache-2.0
#include "clicknova/input_actions.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>
#include <stop_token>
#include <type_traits>

using namespace clicknova::input;

namespace {
struct WindowBackend final : IBackend {
    std::vector<std::string> prepared;
    std::vector<Event> sent;
    std::string missing;
    bool dispatch(const Event& event) override {
        sent.push_back(event);
        return true;
    }
    bool prepareStepWindow(const StepWindow& binding, std::stop_token stop) override {
        prepared.push_back(binding.enforce ? binding.title : std::string{});
        return !stop.stop_requested() && (!binding.enforce || missing.empty() || binding.title != missing);
    }
};
struct TestWaiter final : IWaiter {
    bool wait(std::int64_t, std::stop_token stop) override { return !stop.stop_requested(); }
};
}

int main() {
    // Retain every existing automation step type across future window-binding changes.
    static_assert(std::is_constructible_v<Action, Click>);
    static_assert(std::is_constructible_v<Action, ButtonDown>);
    static_assert(std::is_constructible_v<Action, ButtonUp>);
    static_assert(std::is_constructible_v<Action, MouseHold>);
    static_assert(std::is_constructible_v<Action, Wheel>);
    static_assert(std::is_constructible_v<Action, KeyTap>);
    static_assert(std::is_constructible_v<Action, KeyDown>);
    static_assert(std::is_constructible_v<Action, KeyUp>);
    static_assert(std::is_constructible_v<Action, KeyHold>);
    static_assert(std::is_constructible_v<Action, TypeText>);
    static_assert(std::is_constructible_v<Action, Wait>);
    static_assert(std::is_constructible_v<Action, MovePointer>);
    static_assert(std::is_constructible_v<Action, RunScript>);
    static_assert(std::is_constructible_v<Action, WaitUntil>);
    const StepWindow browser{"Search - Browser", "browser.exe", true, 5000};
    const StepWindow editor{"Document - Editor", "editor.exe", false, 120000};
    Plan plan{{KeyTap{65}, TypeText{u"sample"}, Wait{0}, KeyTap{66}}, 1, 0,
        false, {browser, browser, StepWindow{}, editor}};
    assert(validate(plan).empty());
    WindowBackend backend;
    TestWaiter waiter;
    const auto result = play(plan, backend, waiter);
    assert(result.status == Status::Completed);
    assert(backend.prepared == std::vector<std::string>({browser.title, browser.title, "", editor.title}));
    assert(!backend.sent.empty());
    backend.missing = editor.title;
    backend.prepared.clear();
    backend.sent.clear();
    const auto failed = play(plan, backend, waiter);
    assert(failed.status == Status::DispatchFailed);
    assert(failed.actionsCompleted == 3);
    assert(backend.prepared.back() == editor.title);
    assert(!failed.message.empty());

    auto invalid = plan;
    invalid.stepWindows.pop_back();
    assert(!validate(invalid).empty());
    invalid = plan;
    invalid.stepWindows[0].title.clear();
    assert(!validate(invalid).empty());
    invalid = plan;
    invalid.stepWindows[0].waitMs = -1;
    assert(!validate(invalid).empty());
    invalid = plan;
    invalid.stepWindows[0].title = std::string(1100, 'x');
    assert(!validate(invalid).empty());
    // Focus may not switch while Ctrl is held. The matching Up is allowed.
    invalid = Plan{{KeyDown{0x11}, KeyUp{0x11}}, 1, 0, false, {browser, editor}};
    assert(!validate(invalid).empty());
    invalid.stepWindows[1] = browser;
    assert(validate(invalid).empty());
    // Recorded metadata may change during a held Alt+Tab gesture without
    // silently switching the actual playback target.
    StepWindow observedDesktop{"Workspace 13", "explorer.exe"};
    observedDesktop.enforce = false;
    StepWindow observedBrowser{"Search", "browser.exe"};
    observedBrowser.enforce = false;
    const Plan recorded{{Wait{3000}, MovePointer{MoveMode::Absolute, 500, 400, 0},
                         KeyDown{0x12}, KeyUp{0x12}, Click{}}, 1, 0, false,
                        {observedDesktop, observedDesktop, observedDesktop,
                         observedBrowser, observedBrowser}};
    assert(validate(recorded).empty());
    backend.missing = observedDesktop.title;
    backend.prepared.clear();
    backend.sent.clear();
    const auto recordedPlayback = play(recorded, backend, waiter);
    assert(recordedPlayback.status == Status::Completed);
    assert(recordedPlayback.actionsCompleted == recorded.actions.size());
    for (const auto& name : backend.prepared) assert(name.empty());
    auto mixedLabels = recorded;
    mixedLabels.stepWindows[3] = {}; // no title after Alt+Tab
    assert(validate(mixedLabels).empty());
    // Requiring a different target while holding Alt is still rejected.
    invalid = recorded;
    invalid.stepWindows[2].enforce = true;
    assert(!validate(invalid).empty());
    // Unconfigured required-window behavior remains unchanged.
    assert(browser.enforce);
    return 0;
}
