// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/input_actions.hpp"
#include <cstddef>
#include <vector>

namespace clicknova::input {

// Presentation only: adjacent actions with the same semantic command form a
// collapsible group. Arguments (coordinates, text, duration, selected window)
// remain different per step, and the playback sequence is never changed.
struct AdjacentStepRun {
    std::size_t first{};
    std::size_t end{}; // exclusive
};

inline bool sameStepCommand(const Action& left, const Action& right) {
    if (left.index() != right.index()) return false;
    const auto* lhs = std::get_if<ElementTask>(&left);
    if (!lhs) return true;
    const auto* rhs = std::get_if<ElementTask>(&right);
    return rhs && lhs->command == rhs->command;
}

inline std::vector<AdjacentStepRun> adjacentStepRuns(const std::vector<Action>& actions) {
    std::vector<AdjacentStepRun> groups;
    for (std::size_t first = 0; first < actions.size();) {
        std::size_t end = first + 1;
        while (end < actions.size() && sameStepCommand(actions[first], actions[end])) ++end;
        if (end - first >= 2) groups.push_back({first, end});
        first = end;
    }
    return groups;
}

// Named groups are a consecutive display span of steps carrying the same UUID.
// Reordering a subset can split a group into several spans without moving or
// silently duplicating actions. The editor can regroup such spans explicitly.
struct NamedStepRun {
    std::size_t first{};
    std::size_t end{}; // exclusive
    std::string id;
    std::string title;
};

inline std::vector<NamedStepRun> namedStepRuns(const std::vector<StepWindow>& windows) {
    std::vector<NamedStepRun> spans;
    for (std::size_t first = 0; first < windows.size();) {
        if (windows[first].manualGroupId.empty()) { ++first; continue; }
        const auto id = windows[first].manualGroupId;
        std::size_t end = first + 1;
        while (end < windows.size() && windows[end].manualGroupId == id) ++end;
        spans.push_back({first, end, id, windows[first].manualGroupTitle});
        first = end;
    }
    return spans;
}

// Automatic grouping stays inside a named group and never steals its header
// (the first row always remains the editable representative of that group).
inline std::vector<AdjacentStepRun> visibleAutomaticRuns(
    const std::vector<Action>& actions, const std::vector<StepWindow>& windows) {
    std::vector<AdjacentStepRun> spans;
    for (std::size_t first = 0; first < actions.size();) {
        const bool named = first < windows.size() && !windows[first].manualGroupId.empty();
        const auto id = named ? windows[first].manualGroupId : std::string{};
        std::size_t end = first + 1;
        while (end < actions.size()) {
            const auto candidateId = end < windows.size() ? windows[end].manualGroupId : std::string{};
            if (candidateId != id) break;
            ++end;
        }
        const std::size_t begin = named ? first + 1 : first;
        for (std::size_t row = begin; row < end;) {
            std::size_t finish = row + 1;
            while (finish < end && sameStepCommand(actions[row], actions[finish])) ++finish;
            if (finish - row >= 2) spans.push_back({row, finish});
            row = finish;
        }
        first = end;
    }
    return spans;
}

} // namespace clicknova::input
