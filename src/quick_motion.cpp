// SPDX-License-Identifier: Apache-2.0
#include "clicknova/quick_motion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace clicknova::quick {
std::optional<Point> sample(const Motion& motion, int elapsedMs) noexcept {
    if (motion.durationMs < 0 || motion.durationMs > 60000 || elapsedMs < 0) return std::nullopt;
    const double t = motion.durationMs == 0
        ? 1.0 : std::clamp(static_cast<double>(elapsedMs) / motion.durationMs, 0.0, 1.0);
    const auto mix = [t](int from, int to) {
        return static_cast<int>(std::llround(static_cast<double>(from) +
            (static_cast<double>(to) - static_cast<double>(from)) * t));
    };
    return Point{mix(motion.from.x, motion.to.x), mix(motion.from.y, motion.to.y)};
}
}
