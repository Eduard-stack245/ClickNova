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
    int invocations{0};
    bool permitInvoke{true};
    bool invokeElement(const StepWindow& binding, std::stop_token stop) override {
        ++invocations;
        return !stop.stop_requested() && permitInvoke && !binding.elementAutomationId.empty();
    }
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
    // A saved UIA selector invokes the provider, never the coordinate backend.
    StepWindow uiWindow{"Example", "example.exe", true, 100};
    uiWindow.elementName = "Confirm";
    uiWindow.elementAutomationId = "acceptButton";
    uiWindow.elementClass = "Button";
    uiWindow.elementRole = 1;
    const Plan uiPlan{{Click{Button::Left, 2, 0}}, 1, 0, false, {uiWindow}};
    assert(validate(uiPlan).empty());
    backend.missing.clear();
    backend.sent.clear();
    backend.invocations = 0;
    const auto uiOk = play(uiPlan, backend, waiter);
    assert(uiOk.status == Status::Completed && uiOk.eventsSent == 2);
    assert(backend.invocations == 2 && backend.sent.empty());
    backend.permitInvoke = false;
    backend.sent.clear();
    const auto uiFailed = play(uiPlan, backend, waiter);
    assert(uiFailed.status == Status::DispatchFailed && backend.sent.empty());
    assert(uiFailed.message.find("Element operation failed") != std::string::npos);
    // Native providers are persisted explicitly and cannot fall back to UIA.
    StepWindow msaa = uiWindow;
    msaa.elementProvider = 1;
    msaa.elementAutomationId = "msaa/d0/i4";
    assert(validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {msaa}}).empty());
    msaa.elementProvider = 2;
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {msaa}}).empty());
    StepWindow visual = uiWindow;
    visual.elementProvider = 3;
    visual.elementAutomationId = "cv:C:\\templates\\play.png";
    visual.elementClass = "v1:5000,5000";
    assert(validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {visual}}).empty());
    assert(!validate(Plan{{Click{Button::Left, 2}}, 1, 0, false, {visual}}).empty());
    visual.elementOperation = 1;
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {visual}}).empty());
    visual.elementOperation = 0;
    visual.elementClass = "";
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {visual}}).empty());
    assert(!validate(Plan{{Click{Button::Left, 2}}, 1, 0, false, {visual}}).empty());
    visual.elementProvider = 4;
    visual.elementAutomationId = "ocr:PLAY";
    visual.elementClass = "v1:5000,5000";
    assert(validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {visual}}).empty());
    assert(!validate(Plan{{Click{Button::Left, 2}}, 1, 0, false, {visual}}).empty());
    StepWindow jab = uiWindow;
    jab.elementProvider = 2;
    jab.elementAutomationId = "jab/1/2";
    assert(validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {jab}}).empty());
    jab.elementOperation = 2; jab.elementRole = 3;
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {jab}}).empty());
    jab = uiWindow;
    jab.elementProvider = 1; jab.elementAutomationId = "msaa/d0";
    jab.elementBrowser = true;
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {jab}}).empty());
    jab = uiWindow;
    jab.elementProvider = 99;
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {jab}}).empty());
    // Background execution is ONLY for natively bound accessibility operations.
    StepWindow background = uiWindow;
    background.activate = false;
    assert(backgroundElementAction(background));
    assert(validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {background}}).empty());
    background.elementProvider = 1;
    background.elementAutomationId = "msaa/d0";
    assert(backgroundElementAction(background));
    background.elementProvider = 2;
    background.elementAutomationId = "jab/0";
    assert(backgroundElementAction(background));
    background.elementProvider = 3;
    background.elementAutomationId = "cv:C:\\assets\\play.png";
    background.elementClass = "v1:5000,5000";
    assert(!backgroundElementAction(background));
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {background}}).empty());
    background.elementProvider = 4;
    background.elementAutomationId = "ocr:Play";
    assert(!backgroundElementAction(background));
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {background}}).empty());
    background = uiWindow;
    background.activate = false;
    background.elementOperation = 8; // Focus may steal keyboard focus
    assert(!backgroundElementAction(background));
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {background}}).empty());
    background = uiWindow;
    background.activate = false;
    background.elementBrowser = true;
    assert(!backgroundElementAction(background));
    background.elementTab = "Tab A";
    assert(!backgroundElementAction(background));
    assert(!validate(Plan{{Click{Button::Left, 1}}, 1, 0, false, {background}}).empty());
    background = uiWindow;
    background.activate = false;
    background.elementAutomationId.clear();
    background.elementName.clear();
    assert(!backgroundElementAction(background));
    backend.permitInvoke = true;
    invalid = uiPlan;
    invalid.actions[0] = Click{Button::Right, 1, 0};
    assert(!validate(invalid).empty());
    invalid = uiPlan;
    invalid.actions[0] = TypeText{u"oops"};
    assert(!validate(invalid).empty());
    invalid = uiPlan;
    invalid.stepWindows[0].enforce = false;
    assert(!validate(invalid).empty());
    invalid = uiPlan;
    invalid.stepWindows[0].elementName.clear();
    invalid.stepWindows[0].elementAutomationId.clear();
    invalid.stepWindows[0].elementRole = 1;
    assert(!validate(invalid).empty());
    invalid = uiPlan;
    invalid.stepWindows[0].elementRole = 2;
    assert(!validate(invalid).empty());
    invalid = Plan{{KeyDown{0x11}, Click{Button::Left, 1, 0}, KeyUp{0x11}},
        1, 0, false, {uiWindow, uiWindow, uiWindow}};
    assert(!validate(invalid).empty());

    // UIA operations are explicit, validated and have no coordinate fallback.
    auto patterned = uiPlan;
    patterned.actions[0] = Click{Button::Left, 1, 0};
    auto& b = patterned.stepWindows[0];
    b.elementRole = 2;       // edit
    b.elementOperation = 1;  // ValuePattern
    b.elementValue = "Replacement text";
    assert(validate(patterned).empty());
    b.elementBrowser = true;
    b.elementTab = "Browser website tab";
    assert(validate(patterned).empty());
    invalid = patterned;
    invalid.stepWindows[0].elementBrowser = false;
    assert(!validate(invalid).empty());
    invalid = patterned;
    invalid.stepWindows[0].elementTab = std::string(513, 'x');
    assert(!validate(invalid).empty());
    b.elementTab.clear();
    b.elementBrowser = false;
    backend.sent.clear();
    const auto valuePlayback = play(patterned, backend, waiter);
    assert(valuePlayback.status == Status::Completed && backend.sent.empty());
    invalid = patterned;
    invalid.actions[0] = Click{Button::Left, 2, 0};
    assert(!validate(invalid).empty());
    invalid = patterned;
    invalid.stepWindows[0].elementRole = 1; // button cannot use ValuePattern
    assert(!validate(invalid).empty());
    invalid = patterned;
    invalid.stepWindows[0].elementValue = std::string(4097, 'x');
    assert(!validate(invalid).empty());
    b.elementRole = 3; b.elementOperation = 2; b.elementValue.clear();
    assert(validate(patterned).empty()); // Check
    b.elementOperation = 3;
    assert(validate(patterned).empty()); // Uncheck
    b.elementRole = 9; b.elementOperation = 4;
    assert(validate(patterned).empty()); // list selection
    b.elementRole = 4; b.elementOperation = 5;
    assert(validate(patterned).empty()); // expand combo
    b.elementOperation = 6;
    assert(validate(patterned).empty()); // collapse combo
    b.elementRole = 11; b.elementOperation = 7; b.elementValue = "12.5";
    assert(validate(patterned).empty()); // slider
    invalid = patterned;
    invalid.stepWindows[0].elementValue = "inf";
    assert(!validate(invalid).empty());
    invalid = patterned;
    invalid.stepWindows[0].elementValue = "1,5";
    assert(!validate(invalid).empty());
    b.elementOperation = 8; b.elementValue.clear();
    assert(validate(patterned).empty()); // Focus
    b.elementOperation = 9;
    assert(validate(patterned).empty()); // ScrollIntoView
    invalid = patterned;
    invalid.stepWindows[0].elementOperation = 99;
    assert(!validate(invalid).empty());

    // Unconfigured required-window behavior remains unchanged.
    assert(browser.enforce);
    return 0;
}
