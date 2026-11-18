// SPDX-License-Identifier: MIT
#include "../src/gui/qml/action_workspace.hpp"
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace clicknova;
using namespace clicknova::input;
using namespace clicknova::gui::qml;
using EditOutcome = ActionWorkspace::EditOutcome;

int main() {
    int assertions = 0;
    const auto expect = [&](bool truth, const char* message) {
        ++assertions;
        if (!truth) {
            std::cerr << "Action workspace test failed: " << message << '\n';
            std::exit(1);
        }
    };
    std::vector<Action> actions{Click{Button::Left, 1, 200}, Wait{400}, TypeText{u"abc"}};
    std::vector<StepWindow> windows(actions.size());
    windows[0].elementName = "Button";
    windows[0].elementAutomationId = "run";
    windows[0].elementProvider = 1;
    windows[0].elementOperation = 1;
    windows[0].elementBrowser = true;
    windows[0].elementTab = "tab";
    windows[0].elementValue = "value";
    windows[1].title = "Notes";
    ActionListHistory history;
    ActionWorkspace workspace(actions, windows, history);

    expect(workspace.selectedRow() == -1, "selection starts clear");
    expect(!workspace.select(3) && workspace.selectedRow() == -1, "out-of-range select rejected");
    expect(!workspace.select(-2) && workspace.selectedRow() == -1, "negative select rejected");
    expect(workspace.select(0) && workspace.isSelected(0), "select first action");
    expect(!workspace.isSelected(1), "only one selected");
    expect(!workspace.undo() && !workspace.redo(), "empty history safe");
    const auto before = actions;
    expect(workspace.editSelected(1, "inputLabWaitExpression", u"2s") == EditOutcome::Rejected, "stale row rejects write");
    expect(workspace.editSelected(0, "inputLabClickCount", u"invalid") == EditOutcome::Rejected, "invalid count rejected");
    expect(!history.canUndo() && std::get<Click>(actions[0]).count == std::get<Click>(before[0]).count, "rejections do not create history");
    const auto noChange = workspace.editSelected(0, "inputLabClickCount", u"1");
    expect(noChange == EditOutcome::Unchanged && !history.canUndo(), "no-change edit does not create history");
    const auto changed = workspace.editSelected(0, "inputLabClickCount", u"2");
    expect(changed == EditOutcome::Updated, "valid edit accepted");
    expect(std::get<Click>(actions[0]).count == 2, "original action is changed");
    expect(windows[0].elementName.empty() && windows[0].elementAutomationId.empty(),
           "incompatible click binding is cleared");
    expect(windows[0].elementTab.empty() && windows[0].elementValue.empty() &&
           !windows[0].elementBrowser && windows[0].elementProvider == 0,
           "all incompatible UIA metadata cleared");
    expect(workspace.canUndo() && !workspace.canRedo(), "undo available after edit");
    expect(workspace.undo() && std::get<Click>(actions[0]).count == 1, "undo restores action");
    expect(windows[0].elementName == "Button" && windows[0].elementAutomationId == "run" &&
           windows[1].title == "Notes", "undo restores matching window bindings");
    expect(workspace.selectedRow() == 0 && workspace.canRedo(), "undo restores selected row");
    expect(workspace.redo() && std::get<Click>(actions[0]).count == 2, "redo reapplies action");
    expect(windows[0].elementName.empty(), "redo restores cleared binding");
    expect(workspace.select(1) && workspace.isSelected(1), "select second action");
    expect(workspace.editSelected(0, "inputLabClickCount", u"4") == EditOutcome::Rejected, "previous row cannot be modified");
    expect(workspace.editSelected(1, "inputLabWaitExpression", u"2s") == EditOutcome::Updated,
           "wait duration is edited through domain service");
    expect(std::get<Wait>(actions[1]).durationMs == 2000, "duration becomes milliseconds");
    expect(workspace.undo() && std::get<Wait>(actions[1]).durationMs == 400, "undo restores other kind");
    expect(workspace.selectedRow() == 1, "selection stays at second action");
    expect(workspace.editSelected(1, "inputLabWaitExpression", u"500 ms") == EditOutcome::Updated,
           "editing after undo is allowed");
    expect(!workspace.canRedo(), "new branch invalidates redo");
    expect(!workspace.redo(), "redo unavailable on divergent history");

    expect(workspace.select(2), "select text action");
    expect(workspace.editSelected(2, "inputLabText", u"updated") == EditOutcome::Updated, "text updated");
    expect(std::get<TypeText>(actions[2]).text == u"updated", "text stored in domain value");
    expect(workspace.undo(), "undo text edit");
    expect(std::get<TypeText>(actions[2]).text == u"abc", "original text restored");
    expect(workspace.redo(), "redo text edit");
    expect(std::get<TypeText>(actions[2]).text == u"updated", "new text restored");

    // Native imports and legacy table operations can replace the model; the
    // coordinator rejects invalid selection rather than retaining a stale row.
    actions.resize(1);
    windows.resize(1);
    workspace.reconcileSelection(2);
    expect(workspace.selectedRow() == -1, "removed row clears selection");
    expect(workspace.editSelected(2, "inputLabText", u"bad") == EditOutcome::Rejected, "removed row cannot be edited");
    expect(workspace.select(0), "existing row can be selected");
    windows.clear();
    expect(workspace.editSelected(0, "inputLabClickCount", u"9") == EditOutcome::Rejected, "unaligned bindings reject edits");
    expect(std::get<Click>(actions[0]).count == 2, "unaligned edit leaves domain unchanged");
    windows.resize(1);
    expect(workspace.editSelected(0, "inputLabClickCount", u"9") == EditOutcome::Updated, "recovered alignment edits");
    workspace.reconcileSelection(0);

    // A compatible native Invoke binding survives a multi-click change,
    // whereas an incompatible action type removes only selector fields and
    // preserves the user's manual grouping and target window identity.
    std::vector<Action> linkedActions{Click{Button::Left, 1, 200}};
    std::vector<StepWindow> linkedWindows(1);
    linkedWindows[0].title = "Browser";
    linkedWindows[0].manualGroupId = "grp";
    linkedWindows[0].manualGroupTitle = "Startup";
    linkedWindows[0].elementName = "Continue";
    linkedWindows[0].elementOperation = 0; // Standard Invoke remains compatible.
    ActionListHistory linkedHistory;
    ActionWorkspace linked(linkedActions, linkedWindows, linkedHistory);
    expect(linked.select(0), "linked action selection");
    expect(linked.editSelected(0, "inputLabClickCount", u"3") == EditOutcome::Updated,
           "multi-click with Invoke compatible");
    expect(linkedWindows[0].elementName == "Continue", "compatible selector survives");
    expect(linked.editSelected(0, "inputLabType", u"4") == EditOutcome::Updated,
           "change mouse click to button-down");
    expect(linkedWindows[0].elementName.empty(), "incompatible selector removed on type change");
    expect(linkedWindows[0].title == "Browser" &&
           linkedWindows[0].manualGroupId == "grp" &&
           linkedWindows[0].manualGroupTitle == "Startup", "target and grouping preserved");
    expect(linked.undo(), "undo type change");
    expect(std::holds_alternative<Click>(linkedActions[0]) &&
           linkedWindows[0].elementName == "Continue", "undo type change restores selector");
    expect(linked.undo(), "undo compatible click count");
    expect(std::get<Click>(linkedActions[0]).count == 1, "undo exact value");
    expect(linked.redo() && linked.redo(), "redo both edits");

    std::vector<Action> specialActions{ElementTask{}};
    std::vector<StepWindow> specialBindings(1);
    ActionListHistory specialHistory;
    ActionWorkspace special(specialActions, specialBindings, specialHistory);
    expect(special.select(0), "select special element task");
    expect(special.editSelected(0, "inputLabType", u"0") == EditOutcome::Rejected,
           "special element task cannot be overwritten by generic editor");
    expect(!specialHistory.canUndo(), "special rejection must not create history");

    // The history is bounded to avoid pathological memory accumulation.
    history.clear();
    for (int i = 0; i < 100; ++i) {
        const auto value = std::to_string(i + 10);
        const std::u16string typed(value.begin(), value.end());
        expect(workspace.editSelected(0, "inputLabClickCount", typed) == EditOutcome::Updated,
            "bounded repeated edits");
    }
    int undoCount = 0;
    while (workspace.undo()) ++undoCount;
    expect(undoCount == static_cast<int>(ActionListHistory::kMaxEntries), "history is bounded to 48");
    std::cout << "ActionWorkspace: " << assertions << " assertions passed\n";
}
