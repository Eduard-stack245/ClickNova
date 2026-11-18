// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/input_actions.hpp"
#include <cstddef>
#include <vector>

namespace clicknova::gui::qml::action_batch {

// A pure C++20 proposal, independent of Qt and of UI selection widgets.
// The caller validates the proposed plan and records Undo only on success.
enum class Operation { Duplicate, Erase, MoveUp, MoveDown };
enum class Status { Rejected, Unchanged, Updated };
enum class Error { None, EmptySelection, InvalidSelection, UnalignedBindings, LimitExceeded };

struct Proposal {
    Status status{Status::Rejected};
    Error error{Error::None};
    std::vector<input::Action> actions;
    std::vector<input::StepWindow> windows;
    std::vector<int> selection;
};

// Preconditions: zero-based indices, 1..512 actions, paired action/window
// vectors. Duplicate places a copy after every selected action, preserving
// their order, target binding and manual group metadata. Moves by one logical
// row; adjacent selected rows behave as one block.
Proposal propose(const std::vector<input::Action>& actions,
                 const std::vector<input::StepWindow>& windows,
                 const std::vector<int>& selection,
                 Operation operation,
                 std::size_t maxActions = 512);

} // namespace clicknova::gui::qml::action_batch
