// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>

namespace clicknova::quick {
struct Point { int x{0}; int y{0}; };
struct Motion {
    Point from;
    Point to;
    int durationMs{500};
};

// The caller owns the clock and the stop signal. This module never sends input.
// Invalid paths are rejected rather than clamping to an unintended target.
std::optional<Point> sample(const Motion& motion, int elapsedMs) noexcept;
}
