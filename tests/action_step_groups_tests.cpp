// SPDX-License-Identifier: Apache-2.0
#include "clicknova/action_step_groups.hpp"
#include <cassert>
#include <vector>

int main() {
    using namespace clicknova::input;
    const std::vector<Action> actions{
        Click{Button::Left, 1, 100}, Click{Button::Right, 4, 250}, Click{Button::Left, 2, 50},
        Wait{100}, Wait{200}, KeyTap{65},
        TypeText{u"first"}, TypeText{u"second"},
        KeyTap{66}
    };
    const auto groups = adjacentStepRuns(actions);
    assert(groups.size() == 3);
    assert(groups[0].first == 0 && groups[0].end == 3);
    assert(groups[1].first == 3 && groups[1].end == 5);
    assert(groups[2].first == 6 && groups[2].end == 8);
    assert(actions.size() == 9); // Collapsing in UI cannot remove execution steps.
    const std::vector<Action> disjoint{KeyTap{65}, Click{Button::Left, 1, 50}, KeyTap{65}};
    assert(adjacentStepRuns(disjoint).empty());
    assert(sameStepCommand(actions[0], actions[1]));
    assert(!sameStepCommand(actions[0], actions[3]));
    ElementTask invoke{}; invoke.command = ElementCommand::Activate;
    ElementTask invokeWithOtherData{}; invokeWithOtherData.command = ElementCommand::Activate;
    invokeWithOtherData.timeoutMs = 9999;
    ElementTask text{}; text.command = ElementCommand::SetText;
    std::vector<Action> elementSteps{invoke, invokeWithOtherData, text};
    const auto elementGroups = adjacentStepRuns(elementSteps);
    assert(elementGroups.size() == 1 && elementGroups[0].first == 0 && elementGroups[0].end == 2);
    // Manual groups may contain different commands. They never mutate the
    // action order; automatic runs are confined to the group and do not
    // consume its first (display-header) step.
    std::vector<StepWindow> windows(actions.size());
    for (int i = 0; i < 5; ++i) {
        windows[i].manualGroupId = "5f6f886a-f050-420d-a269-1f70c719083a";
        windows[i].manualGroupTitle = "Authorization";
    }
    const auto named = namedStepRuns(windows);
    assert(named.size() == 1 && named[0].first == 0 && named[0].end == 5);
    assert(named[0].title == "Authorization");
    const auto automatic = visibleAutomaticRuns(actions, windows);
    assert(automatic.size() == 3);
    assert(automatic[0].first == 1 && automatic[0].end == 3);
    assert(automatic[1].first == 3 && automatic[1].end == 5);
    assert(automatic[2].first == 6 && automatic[2].end == 8);
    // A moved action can split one named group into two independently
    // rendered spans while retaining each member's identity.
    windows[2].manualGroupId.clear();
    windows[2].manualGroupTitle.clear();
    const auto split = namedStepRuns(windows);
    assert(split.size() == 2);
    assert(split[0].first == 0 && split[0].end == 2);
    assert(split[1].first == 3 && split[1].end == 5);
    assert(actions.size() == 9);
    return 0;
}
