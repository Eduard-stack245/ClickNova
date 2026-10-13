// SPDX-License-Identifier: Apache-2.0
#include "clicknova/action_list_edit.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
using namespace clicknova::input;

int main() {
    std::vector<Action> steps{Click{Button::Left, 2, 30}, Wait{150}, KeyTap{65},
                              MovePointer{MoveMode::Absolute, 200, 300, 400}};
    assert(moveAction(steps, 0, 3));
    assert(std::holds_alternative<Wait>(steps[0]));
    assert(std::holds_alternative<Click>(steps[3]));
    assert(std::get<Click>(steps[3]).count == 2);
    assert(moveAction(steps, 3, 1));
    assert(std::holds_alternative<Click>(steps[1]));
    assert(std::holds_alternative<MovePointer>(steps[3]));
    assert(!moveAction(steps, 5, 0) && !moveAction(steps, 1, 5));
    assert(moveAction(steps, 1, 1));
    assert(steps.size() == 4);

    // Structural edits preserve exact action payloads and the selected row.
    ActionListHistory history;
    int selectedRow = 1;
    history.record(steps, selectedRow);
    steps.erase(steps.begin() + 1);
    selectedRow = 1;
    assert(history.canUndo() && !history.canRedo());
    assert(history.undo(steps, selectedRow));
    assert(steps.size() == 4 && selectedRow == 1);
    assert(std::get<Click>(steps[1]).count == 2);
    assert(history.canRedo());
    assert(history.redo(steps, selectedRow));
    assert(steps.size() == 3 && selectedRow == 1);
    assert(std::holds_alternative<Click>(steps[0]) == false);
    assert(!history.redo(steps, selectedRow));

    history.record(steps, selectedRow);
    steps.push_back(Wait{450});
    assert(history.undo(steps, selectedRow));
    assert(steps.size() == 3);
    // New edit after undo invalidates the forward branch.
    history.record(steps, selectedRow);
    steps.push_back(KeyTap{90});
    assert(!history.canRedo());
    history.clear();
    assert(!history.canUndo() && !history.canRedo());

    std::vector<StepWindow> bindings(steps.size());
    bindings[0] = StepWindow{"Browser", "browser.exe", true, 5000};
    history.record(steps, bindings, 0);
    steps.erase(steps.begin());
    bindings.erase(bindings.begin());
    assert(history.undo(steps, bindings, selectedRow));
    assert(bindings.size() == steps.size());
    assert(bindings[0].title == "Browser" && bindings[0].activate);
    assert(history.redo(steps, bindings, selectedRow));
    assert(bindings.size() == steps.size());
    history.clear();
    // Cap snapshots so long typing sessions cannot grow history forever.
    for (int i = 0; i < 60; ++i) {
        history.record(steps, 0);
        steps.push_back(Wait{i});
    }
    int undoCount = 0;
    while (history.undo(steps, selectedRow)) ++undoCount;
    assert(undoCount == static_cast<int>(ActionListHistory::kMaxEntries));
    return 0;
}
