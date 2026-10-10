// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/sequence.hpp"
#include <optional>
#include <vector>

namespace clicknova::sequence {
// Creates at most 128 individually editable clicks from a finite click run.
// The ordinary auto clicker waits the interval before its first click too.
std::optional<std::vector<Step>> expandClicksToActions(
    int x, int y, int button, int intervalMs, int count, Space space);
} // namespace clicknova::sequence
