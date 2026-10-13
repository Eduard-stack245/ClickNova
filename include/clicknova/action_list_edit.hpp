// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/input_actions.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>
#include <utility>

namespace clicknova::input {
// Reorders the real plan, preserving individual action payloads.
// Playback validation remains responsible for rejecting unbalanced holds.
// Bounded undo history for list edits. The UI passes its selection separately
// so undo/redo restores both playback order and the selected editor row.
class ActionListHistory {
public:
    static constexpr std::size_t kMaxEntries = 48;
    struct State {
        std::vector<Action> actions;
        int selectedRow{-1};
        std::vector<StepWindow> windows;
    };

    void record(const std::vector<Action>& actions, int selectedRow) {
        push(undo_, State{actions, selectedRow});
        redo_.clear();
    }
    bool undo(std::vector<Action>& actions, int& selectedRow) {
        if (undo_.empty()) return false;
        push(redo_, State{std::move(actions), selectedRow});
        auto previous = std::move(undo_.back());
        undo_.pop_back();
        actions = std::move(previous.actions);
        selectedRow = previous.selectedRow;
        return true;
    }
    bool redo(std::vector<Action>& actions, int& selectedRow) {
        if (redo_.empty()) return false;
        push(undo_, State{std::move(actions), selectedRow});
        auto next = std::move(redo_.back());
        redo_.pop_back();
        actions = std::move(next.actions);
        selectedRow = next.selectedRow;
        return true;
    }
    void record(const std::vector<Action>& actions, const std::vector<StepWindow>& windows, int row) {
        push(undo_, State{actions, row, windows});
        redo_.clear();
    }
    bool undo(std::vector<Action>& actions, std::vector<StepWindow>& windows, int& row) {
        if (undo_.empty()) return false;
        push(redo_, State{std::move(actions), row, std::move(windows)});
        auto previous = std::move(undo_.back()); undo_.pop_back();
        actions = std::move(previous.actions);
        windows = std::move(previous.windows);
        row = previous.selectedRow;
        return true;
    }
    bool redo(std::vector<Action>& actions, std::vector<StepWindow>& windows, int& row) {
        if (redo_.empty()) return false;
        push(undo_, State{std::move(actions), row, std::move(windows)});
        auto next = std::move(redo_.back()); redo_.pop_back();
        actions = std::move(next.actions);
        windows = std::move(next.windows);
        row = next.selectedRow;
        return true;
    }
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    void clear() { undo_.clear(); redo_.clear(); }
private:
    static void push(std::vector<State>& states, State state) {
        if (states.size() == kMaxEntries) states.erase(states.begin());
        states.push_back(std::move(state));
    }
    std::vector<State> undo_;
    std::vector<State> redo_;
};

inline bool moveAction(std::vector<Action>& actions, std::size_t from, std::size_t to) {
    if (from >= actions.size() || to >= actions.size()) return false;
    if (from == to) return true;
    auto first = actions.begin();
    if (from < to)
        std::rotate(first + from, first + from + 1, first + to + 1);
    else
        std::rotate(first + to, first + from, first + from + 1);
    return true;
}
}
