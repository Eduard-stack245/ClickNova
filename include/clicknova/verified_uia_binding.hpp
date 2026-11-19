// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/element_candidate_search.hpp"
#include "clicknova/input_actions.hpp"
#include <string_view>
#include <variant>

namespace clicknova::accessibility::binding {
using candidates::Candidate;
struct OriginalWindow {
    std::string title;
    std::string process;
};
inline bool canCommit(const input::Action& action, const input::StepWindow& binding,
                      const OriginalWindow& original, const Candidate& verified) noexcept {
    if (!candidates::eligible(verified) || !binding.enforce ||
        binding.title.empty() || binding.process.empty() ||
        binding.title != original.title || binding.process != original.process)
        return false;
    const auto* click = std::get_if<input::Click>(&action);
    // Non-click tasks and repeated clicks may have different native semantics.
    return click && click->button == input::Button::Left && click->count == 1;
}
inline bool assign(input::StepWindow& dest, const Candidate& verified) {
    if (!candidates::eligible(verified)) return false;
    dest.elementName.clear(); // Never persist a UIA Name (potentially private).
    dest.elementAutomationId = verified.automationId;
    dest.elementClass = verified.className;
    dest.elementRole = candidates::supportedRole(verified.controlType);
    dest.elementProvider = 0; // UIA only
    dest.elementOperation = 0; // Invoke only; never SetValue or text
    dest.elementValue.clear();
    dest.elementBrowser = false;
    dest.elementTab.clear();
    return true;
}
} // namespace clicknova::accessibility::binding
