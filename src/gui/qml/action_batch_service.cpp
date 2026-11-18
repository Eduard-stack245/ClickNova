// SPDX-License-Identifier: MIT
#include "action_batch_service.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace clicknova::gui::qml::action_batch {

Proposal propose(const std::vector<input::Action>& actions,
                 const std::vector<input::StepWindow>& windows,
                 const std::vector<int>& selection,
                 Operation operation,
                 std::size_t maxActions) {
    Proposal result;
    if (actions.size() != windows.size()) {
        result.error = Error::UnalignedBindings;
        return result;
    }
    if (selection.empty()) {
        result.error = Error::EmptySelection;
        return result;
    }
    if (actions.size() > maxActions || maxActions > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        result.error = Error::LimitExceeded;
        return result;
    }
    result.selection = selection;
    std::sort(result.selection.begin(), result.selection.end());
    result.selection.erase(std::unique(result.selection.begin(), result.selection.end()), result.selection.end());
    if (result.selection.front() < 0 ||
        static_cast<std::size_t>(result.selection.back()) >= actions.size()) {
        result.error = Error::InvalidSelection;
        result.selection.clear();
        return result;
    }
    if (operation == Operation::Duplicate && result.selection.size() > maxActions - actions.size()) {
        result.error = Error::LimitExceeded;
        result.selection.clear();
        return result;
    }

    result.actions = actions;
    result.windows = windows;
    if (operation == Operation::Erase) {
        std::vector<input::Action> remaining;
        std::vector<input::StepWindow> remainingWindows;
        remaining.reserve(actions.size() - result.selection.size());
        remainingWindows.reserve(remaining.capacity());
        std::size_t chosen = 0;
        for (std::size_t i = 0; i < actions.size(); ++i) {
            if (chosen < result.selection.size() && static_cast<std::size_t>(result.selection[chosen]) == i) {
                ++chosen;
                continue;
            }
            remaining.push_back(actions[i]);
            remainingWindows.push_back(windows[i]);
        }
        const std::size_t cursor = static_cast<std::size_t>(result.selection.front());
        result.actions = std::move(remaining);
        result.windows = std::move(remainingWindows);
        result.selection = result.actions.empty() ? std::vector<int>{} :
            std::vector<int>{static_cast<int>(std::min(cursor, result.actions.size() - 1))};
        result.status = Status::Updated;
        return result;
    }

    if (operation == Operation::Duplicate) {
        std::vector<input::Action> expanded;
        std::vector<input::StepWindow> expandedWindows;
        expanded.reserve(actions.size() + result.selection.size());
        expandedWindows.reserve(expanded.capacity());
        std::vector<int> duplicateSelection;
        duplicateSelection.reserve(result.selection.size());
        std::size_t chosen = 0;
        for (std::size_t i = 0; i < actions.size(); ++i) {
            expanded.push_back(actions[i]);
            expandedWindows.push_back(windows[i]);
            if (chosen < result.selection.size() && static_cast<std::size_t>(result.selection[chosen]) == i) {
                duplicateSelection.push_back(static_cast<int>(expanded.size()));
                expanded.push_back(actions[i]);
                expandedWindows.push_back(windows[i]);
                ++chosen;
            }
        }
        result.actions = std::move(expanded);
        result.windows = std::move(expandedWindows);
        result.selection = std::move(duplicateSelection);
        result.status = Status::Updated;
        return result;
    }

    // A byte selection mask avoids vector<bool> proxy issues in std::swap.
    std::vector<std::uint8_t> selected(actions.size(), 0);
    for (const int row : result.selection) selected[static_cast<std::size_t>(row)] = 1;
    bool moved = false;
    if (operation == Operation::MoveUp) {
        for (std::size_t i = 1; i < selected.size(); ++i) {
            if (selected[i] && !selected[i - 1]) {
                std::swap(result.actions[i], result.actions[i - 1]);
                std::swap(result.windows[i], result.windows[i - 1]);
                std::swap(selected[i], selected[i - 1]);
                moved = true;
            }
        }
    } else if (operation == Operation::MoveDown) {
        for (std::size_t i = selected.size(); i > 1; --i) {
            const std::size_t row = i - 1;
            if (selected[row - 1] && !selected[row]) {
                std::swap(result.actions[row], result.actions[row - 1]);
                std::swap(result.windows[row], result.windows[row - 1]);
                std::swap(selected[row], selected[row - 1]);
                moved = true;
            }
        }
    } else {
        result.error = Error::InvalidSelection;
        result.selection.clear();
        result.actions.clear();
        result.windows.clear();
        return result;
    }
    if (!moved) {
        result.status = Status::Unchanged;
        return result;
    }
    result.selection.clear();
    for (std::size_t i = 0; i < selected.size(); ++i) {
        if (selected[i]) result.selection.push_back(static_cast<int>(i));
    }
    result.status = Status::Updated;
    return result;
}

} // namespace clicknova::gui::qml::action_batch
