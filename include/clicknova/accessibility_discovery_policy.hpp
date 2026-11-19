// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/accessibility_inspection_policy.hpp"
#include <cstddef>

// The discovery gate is intentionally OS/Qt-free. It authorizes a request to
// the *existing native picker*, not a live UIA match or an input action.
namespace clicknova::accessibility::discovery {
enum class Operation { Find, Clear };
enum class Refusal {
    None, Busy, NoSelection, StaleSelection, EditorLocked,
    UnsupportedAction, NoSelector, InvalidProvider, NativeUnavailable
};
struct Request {
    Operation operation{Operation::Find};
    int expectedRow{-1};
    int selectedRow{-1};
    std::size_t actionCount{0};
    bool workflowEditable{false};
    bool actionEditable{false};
    bool backendAvailable{false};
    bool busy{false};
    Facts facts{};
};

constexpr Refusal decide(const Request& r) noexcept {
    if (r.busy) return Refusal::Busy;
    if (r.expectedRow < 0 || r.selectedRow < 0 ||
        static_cast<std::size_t>(r.expectedRow) >= r.actionCount ||
        static_cast<std::size_t>(r.selectedRow) >= r.actionCount) return Refusal::NoSelection;
    if (r.expectedRow != r.selectedRow) return Refusal::StaleSelection;
    if (!r.workflowEditable || !r.actionEditable) return Refusal::EditorLocked;
    const auto assessment = inspect(r.facts);
    if (r.operation == Operation::Find && !assessment.canRequestSearch) {
        if (assessment.configured && !assessment.providerKnown) return Refusal::InvalidProvider;
        return Refusal::UnsupportedAction;
    }
    if (r.operation == Operation::Clear && !assessment.canRequestClear)
        return Refusal::NoSelector;
    if (!r.backendAvailable) return Refusal::NativeUnavailable;
    return Refusal::None;
}
} // namespace clicknova::accessibility::discovery
