// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/input_actions.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace clicknova::recording::review {
// Summary of the editable plan, not an estimate of Windows execution time.
struct Summary {
    std::size_t actions{};
    std::uint64_t mousePresses{};
    std::uint64_t keyPresses{};
    std::size_t moves{};
    std::size_t wheelActions{};
    std::size_t waits{};
    std::uint64_t scheduledMilliseconds{};
};
Summary summarize(const std::vector<input::Action>& actions);

// Removing one half of a held-input pair also removes its matching half.
// It must never leave a key or button held when the resulting plan plays.
// When supplied, removedIndexes reports original indexes in ascending order.
// This allows the UI to keep window-origin labels aligned after pair removal.
bool eraseWithPair(std::vector<input::Action>& actions, std::size_t index,
                   std::vector<std::size_t>* removedIndexes = nullptr);

// Reject changes that break balanced press/release pairs or other constraints.
bool moveValidated(std::vector<input::Action>& actions, std::size_t from, std::size_t to);
bool replaceValidated(std::vector<input::Action>& actions, std::size_t index, input::Action replacement);
} // namespace clicknova::recording::review
