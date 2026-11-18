// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/action_list_edit.hpp"
#include "action_edit_service.hpp"
#include "clicknova/verified_uia_binding.hpp"
#include "action_batch_service.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clicknova::gui::qml {

// A Qt-free application-layer coordinator. It never reads a QTableWidget:
// Widgets and QML are projections of the same native action model.
// The owner retains vectors and the existing ActionListHistory instance, so
// model changes from either UI share one undo/redo timeline.
class ActionWorkspace final {
public:
    ActionWorkspace(std::vector<input::Action>& actions,
                    std::vector<input::StepWindow>& windows,
                    input::ActionListHistory& history) noexcept;

    int selectedRow() const noexcept { return selectedRow_; }
    bool select(int row) noexcept;
    void reconcileSelection(int desired) noexcept;
    bool isSelected(int row) const noexcept;

    // Content revision changes when the action order, parameters, or bindings
    // change, including changes made by the original Widgets editor.
    std::string revision() const;
    void checkpoint();
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return history_.canUndo(); }
    bool canRedo() const noexcept { return history_.canRedo(); }

    enum class EditOutcome { Rejected, Unchanged, Updated };
    // Selection, value validation and binding invalidation are atomic from the
    // caller's perspective; rejected edits never touch history or model state.
    EditOutcome editSelected(int row, std::string_view field,
                             std::u16string_view value);
    struct FieldEdit {
        std::string name;
        std::u16string value;
        // The inspector sends the exact value originally read from the native
        // model. An external Undo/import/replacement cannot be overwritten by
        // an old QML draft even when the selected positional row is unchanged.
        std::optional<std::u16string> expected;
    };
    // Validate the entire set on a copy, then commit with a single Undo entry.
    // Any invalid field or stale expected value leaves both vectors untouched.
    EditOutcome editSelectedFields(int row, const std::vector<FieldEdit>& edits,
                                   std::string_view expectedRevision = {});
    // Only the native verified callback calls this. Raw selectors never arrive
    // through QML, and a rejected binding creates no undo entry.
    bool bindVerifiedUia(int row, const accessibility::binding::OriginalWindow& window,
                         const accessibility::candidates::Candidate& selector);
    // Transactional browser-tab scope edit, shared with native Undo/Redo.
    // No user-entered title is emitted in a presentation summary or log.
    EditOutcome setBrowserTabScope(int row, bool enable, std::string_view tabTitle);

    // The validator is the authoritative playback validator from the native
    // engine. A batch edit cannot corrupt a previously valid hold/release plan.
    using PlanValidator = std::function<bool(const std::vector<input::Action>&)>;
    struct BatchOutcome {
        action_batch::Status status{action_batch::Status::Rejected};
        action_batch::Error error{action_batch::Error::None};
        std::vector<int> selection;
    };
    BatchOutcome applyBatch(const std::vector<int>& selection,
                            action_batch::Operation operation,
                            const PlanValidator& isValid);

private:
    bool validRow(int row) const noexcept;
    std::vector<input::Action>& actions_;
    std::vector<input::StepWindow>& windows_;
    input::ActionListHistory& history_;
    int selectedRow_{-1};
};

} // namespace clicknova::gui::qml
