// SPDX-License-Identifier: MIT
#include "../src/gui/qml/action_workspace.hpp"
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if(!(x)) {std::cerr << "FAIL " << __LINE__ << " " #x << '\n';std::abort();} }while(false)
using namespace clicknova;
int main() {
    using namespace input;
    using namespace accessibility::candidates;
    std::vector<Action> actions{Click{Button::Left,1,200},Wait{100}};
    std::vector<StepWindow> bindings(2);
    bindings[0].title="Target";bindings[0].process="app.exe";bindings[0].enforce=true;
    bindings[0].manualGroupId="group";
    ActionListHistory history;
    gui::qml::ActionWorkspace workspace(actions,bindings,history);
    Candidate unique{50000,true,"run","Button"};
    CHECK(workspace.select(0));
    CHECK(!workspace.bindVerifiedUia(1,{"Target","app.exe"},unique));
    CHECK(!workspace.bindVerifiedUia(0,{"Stale","app.exe"},unique));
    CHECK(!workspace.bindVerifiedUia(0,{"Target","app.exe"},{50000,true,"","Button"}));
    CHECK(!history.canUndo() && bindings[0].elementAutomationId.empty());
    CHECK(workspace.bindVerifiedUia(0,{"Target","app.exe"},unique));
    CHECK(history.canUndo());
    CHECK(bindings[0].elementAutomationId=="run" && bindings[0].elementName.empty());
    CHECK(bindings[0].manualGroupId=="group");
    CHECK(workspace.undo() && bindings[0].elementAutomationId.empty());
    CHECK(workspace.redo() && bindings[0].elementAutomationId=="run");
    CHECK(workspace.bindVerifiedUia(0,{"Target","app.exe"},unique)); // unchanged
    CHECK(workspace.select(1));
    CHECK(!workspace.bindVerifiedUia(1,{"",""},unique));
    std::cout << "PASS transactional native model binding, Undo/Redo\n";
}
