// SPDX-License-Identifier: MIT
#include "../src/gui/qml/action_revision.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace clicknova::input;
using clicknova::gui::qml::action_revision::snapshot;

static int assertions = 0;
static void check(bool condition, const char* reason) {
    ++assertions;
    if (!condition) {
        std::cerr << "Action revision test failed: " << reason << '\n';
        std::exit(1);
    }
}

int main() {
    const std::vector<Action> defaults{
        Click{}, ButtonDown{}, ButtonUp{}, MouseHold{}, Wheel{},
        KeyTap{}, KeyDown{}, KeyUp{}, KeyHold{}, TypeText{}, Wait{},
        MovePointer{}, RunScript{}, WaitUntil{}, ElementTask{}
    };
    std::vector<StepWindow> bindings(defaults.size());
    const auto baseline = snapshot(defaults, bindings);
    check(baseline.size() == 32, "digest is a fixed-length opaque token");
    check(baseline == snapshot(defaults, bindings), "same document has stable token");
    check(baseline != snapshot({}, {}), "action count contributes to digest");
    check(baseline != snapshot(defaults, {}), "window count contributes to digest");

    const std::vector<Action> changed{
        Click{Button::Right, 2, 10}, ButtonDown{Button::Right},
        ButtonUp{Button::Middle}, MouseHold{Button::Right, 600},
        Wheel{Axis::Horizontal, -120}, KeyTap{0x42}, KeyDown{0x42},
        KeyUp{0x42}, KeyHold{0x42, 900}, TypeText{u"秘密"},
        Wait{1900}, MovePointer{MoveMode::WindowClient,-1,18,20},
        RunScript{u"Write-Output 1"}, WaitUntil{2,3,4},
        ElementTask{ElementCommand::SetText, u"test", Axis::Horizontal, 140, 200, 50, 70}
    };
    check(defaults.size() == changed.size(), "test covers each action variant");
    for (std::size_t i = 0; i < changed.size(); ++i) {
        auto copy = defaults;
        copy[i] = changed[i];
        check(baseline != snapshot(copy, bindings), "editing an action invalidates revision");
    }
    const auto checkWindow = [&](auto mutate) {
        auto copy = bindings;
        mutate(copy[2]);
        check(baseline != snapshot(defaults, copy),
              "changing a window target/selector/group invalidates revision");
    };
    checkWindow([](StepWindow& w) { w.title = "Window"; });
    checkWindow([](StepWindow& w) { w.process = "program.exe"; });
    checkWindow([](StepWindow& w) { w.activate = true; });
    checkWindow([](StepWindow& w) { w.waitMs = 150; });
    checkWindow([](StepWindow& w) { w.enforce = false; });
    checkWindow([](StepWindow& w) { w.elementName = "Submit"; });
    checkWindow([](StepWindow& w) { w.elementAutomationId = "button1"; });
    checkWindow([](StepWindow& w) { w.elementClass = "Button"; });
    checkWindow([](StepWindow& w) { w.elementProvider = 2; });
    checkWindow([](StepWindow& w) { w.elementRole = 4; });
    checkWindow([](StepWindow& w) { w.elementBrowser = true; });
    checkWindow([](StepWindow& w) { w.elementTab = "Tab"; });
    checkWindow([](StepWindow& w) { w.elementOperation = 2; });
    checkWindow([](StepWindow& w) { w.elementValue = "42"; });
    checkWindow([](StepWindow& w) { w.manualGroupId = "uuid"; });
    checkWindow([](StepWindow& w) { w.manualGroupTitle = "Group"; });
    checkWindow([](StepWindow& w) { w.displayName = "Submit order"; });

    auto reordered = defaults;
    std::swap(reordered[0], reordered[1]);
    check(baseline != snapshot(reordered, bindings), "action order contributes to revision");
    auto targetOrder = bindings;
    targetOrder[0].title = "First";
    targetOrder[1].title = "Second";
    const auto beforeSwap = snapshot(defaults, targetOrder);
    std::swap(targetOrder[0], targetOrder[1]);
    check(beforeSwap != snapshot(defaults, targetOrder), "binding order contributes to revision");
    check(baseline == snapshot(defaults, bindings), "read-only revisions do not mutate input");

    std::cout << "Native document revision: " << assertions << " checks passed\n";
}
