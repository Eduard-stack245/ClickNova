// SPDX-License-Identifier: MIT
#include "action_workspace.hpp"
#include "clicknova/browser_tab_scope.hpp"
#include "action_revision.hpp"

#include <variant>
#include <utility>

namespace clicknova::gui::qml {

ActionWorkspace::ActionWorkspace(std::vector<input::Action>& actions,
                                 std::vector<input::StepWindow>& windows,
                                 input::ActionListHistory& history) noexcept
    : actions_(actions), windows_(windows), history_(history) {}

bool ActionWorkspace::validRow(int row) const noexcept {
    return row >= 0 && static_cast<std::size_t>(row) < actions_.size();
}

bool ActionWorkspace::select(int row) noexcept {
    if (row != -1 && !validRow(row)) return false;
    selectedRow_ = row;
    return true;
}

void ActionWorkspace::reconcileSelection(int desired) noexcept {
    selectedRow_ = validRow(desired) ? desired : -1;
}

bool ActionWorkspace::isSelected(int row) const noexcept {
    return validRow(row) && selectedRow_ == row;
}

std::string ActionWorkspace::revision() const {
    return action_revision::snapshot(actions_, windows_);
}

void ActionWorkspace::checkpoint() {
    history_.record(actions_, windows_, selectedRow_);
}

bool ActionWorkspace::undo() {
    int restored = selectedRow_;
    if (!history_.undo(actions_, windows_, restored)) return false;
    reconcileSelection(restored);
    return true;
}

bool ActionWorkspace::redo() {
    int restored = selectedRow_;
    if (!history_.redo(actions_, windows_, restored)) return false;
    reconcileSelection(restored);
    return true;
}

ActionWorkspace::EditOutcome ActionWorkspace::editSelected(int row, std::string_view field,
                                                          std::u16string_view value) {
    return editSelectedFields(row, {{std::string(field), std::u16string(value), std::nullopt}});
}

ActionWorkspace::EditOutcome ActionWorkspace::editSelectedFields(
        int row, const std::vector<FieldEdit>& edits,
        std::string_view expectedRevision) {
    if (!expectedRevision.empty() && expectedRevision != revision())
        return EditOutcome::Rejected;
    constexpr std::size_t maxFields = 16;
    if (!isSelected(row) || actions_.size() != windows_.size() ||
        edits.empty() || edits.size() > maxFields) return EditOutcome::Rejected;
    const auto position = static_cast<std::size_t>(row);
    input::Action candidate = actions_[position];
    bool changed = false;
    for (std::size_t i = 0; i < edits.size(); ++i) {
        const auto& edit = edits[i];
        if (edit.name.empty()) return EditOutcome::Rejected;
        for (std::size_t j = 0; j < i; ++j)
            if (edits[j].name == edit.name) return EditOutcome::Rejected;
        // Compare against the original model, not against an already-mutated
        // candidate. This is an optimistic concurrency check on every field.
        if (edit.expected) {
            const auto actual = action_edit::read(actions_[position], edit.name);
            if (!actual || *actual != *edit.expected) return EditOutcome::Rejected;
        }
        const auto proposal = action_edit::apply(candidate, edit.name, edit.value);
        if (!proposal) return EditOutcome::Rejected;
        changed = changed || proposal.changed;
        candidate = std::move(proposal.action);
    }
    if (!changed) return EditOutcome::Unchanged;

    input::StepWindow binding = windows_[position];
    if ((!binding.elementName.empty() || !binding.elementAutomationId.empty()) &&
        (!std::holds_alternative<input::Click>(candidate) ||
         std::get<input::Click>(candidate).button != input::Button::Left ||
         (binding.elementOperation != 0 && std::get<input::Click>(candidate).count != 1))) {
        binding.elementName.clear();
        binding.elementAutomationId.clear();
        binding.elementClass.clear();
        binding.elementRole = 0;
        binding.elementProvider = 0;
        binding.elementBrowser = false;
        binding.elementTab.clear();
        binding.elementOperation = 0;
        binding.elementValue.clear();
    }
    // Exactly one history checkpoint, regardless of the number of fields.
    checkpoint();
    actions_[position] = std::move(candidate);
    windows_[position] = std::move(binding);
    return EditOutcome::Updated;
}


bool ActionWorkspace::bindVerifiedUia(
        int row, const accessibility::binding::OriginalWindow& window,
        const accessibility::candidates::Candidate& selector) {
    if (!isSelected(row) || actions_.size() != windows_.size()) return false;
    const auto index = static_cast<std::size_t>(row);
    if (!accessibility::binding::canCommit(actions_[index], windows_[index], window, selector))
        return false;
    input::StepWindow proposed = windows_[index];
    if (!accessibility::binding::assign(proposed, selector)) return false;
    if (proposed == windows_[index]) return true;
    checkpoint();
    windows_[index] = std::move(proposed);
    return true;
}

ActionWorkspace::EditOutcome ActionWorkspace::setBrowserTabScope(
        int row, bool enable, std::string_view tabTitle) {
    if (!isSelected(row) || actions_.size() != windows_.size())
        return EditOutcome::Rejected;
    const auto index = static_cast<std::size_t>(row);
    const auto replacement = clicknova::browser::scope::propose(
        actions_[index], windows_[index], enable, tabTitle);
    if (!replacement) return EditOutcome::Rejected;
    if (*replacement == windows_[index]) return EditOutcome::Unchanged;
    checkpoint();
    windows_[index] = std::move(*replacement);
    return EditOutcome::Updated;
}

ActionWorkspace::BatchOutcome ActionWorkspace::applyBatch(
        const std::vector<int>& selection, action_batch::Operation operation,
        const PlanValidator& isValid) {
    auto proposal = action_batch::propose(actions_, windows_, selection, operation);
    const auto outcome = [&proposal] {
        return BatchOutcome{proposal.status, proposal.error, proposal.selection};
    };
    if (proposal.status != action_batch::Status::Updated) return outcome();
    // A missing validator is an integration error, not permission to reorder
    // potentially unsafe mouse/keyboard holds.
    if (!isValid || (isValid(actions_) && !proposal.actions.empty() &&
                     !isValid(proposal.actions))) {
        proposal.status = action_batch::Status::Rejected;
        return outcome();
    }
    checkpoint();
    actions_ = std::move(proposal.actions);
    windows_ = std::move(proposal.windows);
    reconcileSelection(proposal.selection.empty() ? -1 : proposal.selection.front());
    return outcome();
}

} // namespace clicknova::gui::qml
