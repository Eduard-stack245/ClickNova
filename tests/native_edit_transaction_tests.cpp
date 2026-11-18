// SPDX-License-Identifier: MIT
#include "../src/gui/qml/action_workspace.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace clicknova;
using namespace clicknova::input;
using Workspace = clicknova::gui::qml::ActionWorkspace;
using Outcome = Workspace::EditOutcome;
using Field = Workspace::FieldEdit;

int main() {
    int assertions = 0;
    const auto check = [&](bool good, const char* message) {
        ++assertions;
        if (!good) { std::cerr << "Native edit transaction failed: " << message << '\n'; std::exit(1); }
    };
    std::vector<Action> actions{Click{Button::Left,1,100}, MovePointer{MoveMode::Relative,0,0,10}, TypeText{u"saved"}};
    std::vector<StepWindow> windows(actions.size());
    windows[0].elementName = "Submit";
    windows[0].elementAutomationId = "ok";
    windows[0].elementOperation = 1;
    ActionListHistory history;
    Workspace workspace(actions,windows,history);

    check(workspace.select(0),"select first row");
    const auto originalClick = std::get<Click>(actions[0]);
    const auto originalWindows = windows;
    // All fields must pass validation; no partial write and no Undo on failure.
    check(workspace.editSelectedFields(0, {
        {"inputLabClickCount",u"2",u"1"},
        {"inputLabClickIntervalMs",u"not-a-number",u"100"}}) == Outcome::Rejected,
        "second invalid field rejects entire batch");
    check(std::get<Click>(actions[0]).count==originalClick.count &&
          std::get<Click>(actions[0]).intervalMs==originalClick.intervalMs &&
          windows==originalWindows && !history.canUndo(),"failure leaves state untouched");
    check(workspace.editSelectedFields(0, {
        {"inputLabClickCount",u"2",u"1"},
        {"inputLabClickIntervalMs",u"250",u"100"}}) == Outcome::Updated,
        "valid pair commits");
    check(std::get<Click>(actions[0]).count==2 && std::get<Click>(actions[0]).intervalMs==250,
          "both fields updated");
    check(windows[0].elementName.empty() && windows[0].elementAutomationId.empty(),
          "incompatible binding removed together with action edit");
    check(workspace.undo(),"one Undo restores complete change");
    check(std::get<Click>(actions[0]).count==originalClick.count &&
          std::get<Click>(actions[0]).intervalMs==originalClick.intervalMs &&
          windows==originalWindows,"one Undo restores both fields and binding");
    check(!workspace.undo(),"single multi-field commit uses one history entry");
    check(workspace.redo(),"redo transaction");
    check(std::get<Click>(actions[0]).count==2 && std::get<Click>(actions[0]).intervalMs==250,
          "redo restores all fields");

    // Replaying stale QML drafts cannot overwrite newer native/Undo values.
    check(workspace.editSelectedFields(0, {{"inputLabClickCount",u"3",u"1"}})==Outcome::Rejected,
          "stale original expected value rejected");
    check(std::get<Click>(actions[0]).count==2,"stale write changes nothing");
    check(workspace.editSelectedFields(0, {{"inputLabClickCount",u"3",u"2"}})==Outcome::Updated,
          "matching expectation accepted");
    check(workspace.editSelectedFields(0, {{"inputLabClickCount",u"4",u"3"},
                                     {"inputLabClickCount",u"5",u"3"}})==Outcome::Rejected,
          "duplicate same field rejected");
    check(workspace.editSelectedFields(0, {{"inputLabClickCount",u"4",u"3"},
                                     {"inputLabMoveX",u"2",u"0"}})==Outcome::Rejected,
          "unsupported second field rejected");
    check(std::get<Click>(actions[0]).count==3,"reject leaves first field unchanged");
    check(workspace.editSelectedFields(1, {{"inputLabMoveX",u"7",u"0"}})==Outcome::Rejected,
          "wrong selected row denied");
    check(workspace.editSelectedFields(0,{})==Outcome::Rejected,"empty list denied");
    std::vector<Field> tooMany(17, Field{"inputLabClickCount",u"3",u"3"});
    check(workspace.editSelectedFields(0,tooMany)==Outcome::Rejected,"bounded edit count");

    check(workspace.select(1),"select movement");
    check(workspace.editSelectedFields(1, {{"inputLabMoveX",u"-15",u"0"},
                                     {"inputLabMoveY",u"22",u"0"},
                                     {"inputLabMoveDuration",u"300",u"10"}})==Outcome::Updated,
          "movement trio accepted");
    check(std::get<MovePointer>(actions[1]).x==-15 && std::get<MovePointer>(actions[1]).y==22 &&
          std::get<MovePointer>(actions[1]).durationMs==300,"movement values committed");
    check(workspace.undo(),"undo movement transaction");
    check(std::get<MovePointer>(actions[1]).x==0 && std::get<MovePointer>(actions[1]).y==0 &&
          std::get<MovePointer>(actions[1]).durationMs==10,"undo movement trio in one step");

    // The same optimistic concurrency guard protects free-form Unicode text.
    check(workspace.select(2),"select text");
    check(workspace.editSelectedFields(2, {{"inputLabText",u"Новый текст",u"saved"}})==Outcome::Updated,
          "unicode commit");
    check(std::get<TypeText>(actions[2]).text==u"Новый текст","unicode preserved");
    check(workspace.editSelectedFields(2, {{"inputLabText",u"bad",u"saved"}})==Outcome::Rejected,
          "stale Unicode draft rejected");
    check(workspace.undo(),"unicode Undo available");
    check(std::get<TypeText>(actions[2]).text==u"saved","Unicode Undo restores original");
    // A draft is tied to the entire native document, not just one field.
    // Another step can change, be reordered or receive a different binding
    // without changing the input text the user originally saw.
    check(workspace.select(0), "select revision row");
    const auto currentRevision = workspace.revision();
    check(currentRevision.size() == 32, "opaque revision is 128-bit hexadecimal");
    check(workspace.revision() == currentRevision, "read-only calls keep revision stable");
    check(workspace.editSelectedFields(0, { {"inputLabClickIntervalMs", u"260", u"250"} },
                                       currentRevision) == Outcome::Updated,
          "fresh document revision commits");
    check(workspace.revision() != currentRevision, "revision changes after edit");
    check(workspace.editSelectedFields(0, { {"inputLabClickIntervalMs", u"270", u"260"} },
                                       currentRevision) == Outcome::Rejected,
          "stale document revision rejects even when individual value matches");
    check(std::get<Click>(actions[0]).intervalMs == 260,
          "stale revision rejection is nonmutating");
    const auto beforeOtherRow = workspace.revision();
    actions[2] = TypeText{u"externally changed"}; // native import/legacy editor
    check(beforeOtherRow != workspace.revision(),
          "external mutation of a different row is detected");
    check(workspace.editSelectedFields(0, { {"inputLabClickIntervalMs", u"280", u"260"} },
                                       beforeOtherRow) == Outcome::Rejected,
          "another row's native mutation invalidates the inspector draft");
    const auto beforeWindow = workspace.revision();
    windows[2].manualGroupTitle = "Changed outside QML";
    check(beforeWindow != workspace.revision(), "binding-only mutation is detected");

    std::vector<Action> similar{Click{Button::Left, 1, 100}, Click{Button::Left, 1, 100}};
    std::vector<StepWindow> bindings(similar.size());
    bindings[0].title = "Window A";
    bindings[1].title = "Window B";
    ActionListHistory similarHistory;
    Workspace similarEditor(similar, bindings, similarHistory);
    check(similarEditor.select(0), "select first of identical click actions");
    const auto beforeSwap = similarEditor.revision();
    std::swap(similar[0], similar[1]);
    std::swap(bindings[0], bindings[1]);
    check(beforeSwap != similarEditor.revision(),
          "reordering equal steps with different targets changes revision");
    check(similarEditor.editSelectedFields(0, { {"inputLabClickIntervalMs", u"200", u"100"} },
                                           beforeSwap) == Outcome::Rejected,
          "stale draft cannot overwrite a different same-looking window-bound step");
    check(!similarHistory.canUndo() && std::get<Click>(similar[0]).intervalMs == 100,
          "rejection leaves history and step intact");

    std::cout << "Native atomic action edits: " << assertions << " checks passed\n";
}
