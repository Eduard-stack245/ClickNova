// SPDX-License-Identifier: MIT
// Qt-free regression tests for QML-to-native optimistic-concurrency tickets.
#include "../src/gui/qml/action_revision_guard.hpp"
#include "../src/gui/qml/action_revision.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace clicknova;
using namespace clicknova::gui::qml;

int main() {
    int checks = 0;
    auto require = [&](bool condition, const char* description) {
        ++checks;
        if (!condition) {
            std::cerr << "Revision-guard regression: " << description << '\n';
            std::exit(1);
        }
    };
    const std::string zeroes(32, '0');
    const std::string others(32, 'a');
    require(revision_guard::wellFormed(zeroes), "lowercase hex accepted");
    require(revision_guard::wellFormed(others), "alphabetic lowercase hex accepted");
    require(!revision_guard::wellFormed(""), "empty rejected");
    require(!revision_guard::wellFormed(std::string(31,'0')), "short rejected");
    require(!revision_guard::wellFormed(std::string(33,'0')), "long rejected");
    require(!revision_guard::wellFormed(std::string(32,'A')), "uppercase rejected");
    require(!revision_guard::wellFormed(std::string(32,'g')), "outside hex rejected");
    require(!revision_guard::wellFormed(std::string(32,'\0')), "embedded NUL rejected");
    require(!revision_guard::wellFormed(std::string(32,' ')), "whitespace rejected");
    require(revision_guard::matches(zeroes, zeroes), "equal tickets accepted");
    require(!revision_guard::matches(zeroes, others), "different tickets rejected");
    require(!revision_guard::matches("", ""), "two missing tickets not equal");
    require(!revision_guard::matches("x", "x"), "two malformed tickets not equal");

    require(revision_guard::permitsWindowBinding("quick", -1, 5, "", ""),
            "quick window independent of scenario selection");
    require(!revision_guard::permitsWindowBinding("quick", 0, 0, "", zeroes),
            "quick rejects a step row");
    require(!revision_guard::permitsWindowBinding("quick", -1, 0, zeroes, zeroes),
            "quick rejects sequence revision argument");
    require(revision_guard::permitsWindowBinding("sequence", -1, 5, zeroes, zeroes),
            "sequence accepts fresh document");
    require(!revision_guard::permitsWindowBinding("sequence", 0, 0, zeroes, zeroes),
            "sequence rejects step row");
    require(!revision_guard::permitsWindowBinding("sequence", -1, 0, zeroes, others),
            "sequence rejects stale document");
    require(!revision_guard::permitsWindowBinding("sequence", -1, 0, "", ""),
            "sequence requires valid ticket");
    require(revision_guard::permitsWindowBinding("step", 1, 1, zeroes, zeroes),
            "step accepts fresh selection");
    require(!revision_guard::permitsWindowBinding("step", -1, -1, zeroes, zeroes),
            "step must be selected");
    require(!revision_guard::permitsWindowBinding("step", 0, 1, zeroes, zeroes),
            "step selection mismatch rejected");
    require(!revision_guard::permitsWindowBinding("step", 1, 1, zeroes, others),
            "step stale document rejected");
    require(!revision_guard::permitsWindowBinding("step", 1, 1, "", zeroes),
            "step missing ticket rejected");
    require(!revision_guard::permitsWindowBinding("unknown", 1, 1, zeroes, zeroes),
            "unrecognized scope rejected");
    require(revision_guard::hasRequiredWindowConfirmation("quick", false),
            "quick target can be selected without destructive-action confirmation");
    require(revision_guard::hasRequiredWindowConfirmation("quick", true),
            "quick target also accepts explicit confirmation");
    require(!revision_guard::hasRequiredWindowConfirmation("sequence", false),
            "sequence target requires native-side confirmation");
    require(revision_guard::hasRequiredWindowConfirmation("sequence", true),
            "sequence target with confirmation permitted");
    require(!revision_guard::hasRequiredWindowConfirmation("step", false),
            "step target requires native-side confirmation");
    require(revision_guard::hasRequiredWindowConfirmation("step", true),
            "step target with confirmation permitted");
    require(!revision_guard::hasRequiredWindowConfirmation("unknown", true),
            "unrecognized scope rejected even if confirmed");

    std::vector<input::Action> actions{
        input::Click{input::Button::Left, 1, 100}, input::Wait{100}, input::Wait{100}};
    std::vector<input::StepWindow> windows(actions.size());
    windows[0].title = "Browser";
    windows[1].title = "Editor";
    const auto original = action_revision::snapshot(actions, windows);
    require(revision_guard::wellFormed(original), "real native digest format accepted");
    require(revision_guard::permitsWindowBinding("step", 1, 1, original, original),
            "real native digest permits fresh binding");
    windows[1].elementAutomationId = "secret-action-id";
    const auto changedWindow = action_revision::snapshot(actions, windows);
    require(original != changedWindow, "hidden element selector changes revision");
    require(!revision_guard::permitsWindowBinding("step", 1, 1, original, changedWindow),
            "hidden selector invalidates window dialog");
    windows[1].elementAutomationId.clear();
    std::swap(actions[1], actions[2]); // the two steps are intentionally identical
    std::swap(windows[1], windows[2]);
    const auto changedOrder = action_revision::snapshot(actions, windows);
    require(original != changedOrder, "identical-looking rows with differing bindings reorder");
    require(!revision_guard::matches(original, changedOrder),
            "reorder invalidates batch selection revision");
    require(!revision_guard::permitsWindowBinding("sequence", -1, -1, original, changedOrder),
            "reorder invalidates sequence window binding");
    actions.push_back(input::Wait{100});
    windows.emplace_back();
    const auto added = action_revision::snapshot(actions, windows);
    require(!revision_guard::matches(changedOrder, added), "addition invalidates batch ticket");
    actions.pop_back();
    windows.pop_back();
    require(revision_guard::matches(changedOrder, action_revision::snapshot(actions, windows)),
            "same content produces reproducible revision");
    std::cout << "Action revision guard: " << checks << " assertions passed\n";
}
