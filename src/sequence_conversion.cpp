// SPDX-License-Identifier: Apache-2.0
#include "clicknova/sequence_conversion.hpp"

namespace clicknova::sequence {
std::optional<std::vector<Step>> expandClicksToActions(
    int x, int y, int button, int intervalMs, int count, Space space) {
    if (count < 1 || count > 128 || button < 0 || button > 2 ||
        intervalMs < 10 || intervalMs > 60000 ||
        x < -131072 || x > 131072 || y < -131072 || y > 131072 ||
        (space != Space::Screen && space != Space::Client) ||
        (space == Space::Client && (x < 0 || y < 0))) return std::nullopt;
    std::vector<Step> steps;
    steps.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Step step;
        step.x = x;
        step.y = y;
        step.button = button;
        step.delayMs = intervalMs;
        step.intervalMs = intervalMs;
        step.clickCount = 1;
        step.space = space;
        step.configured = true;
        steps.push_back(step);
    }
    return steps;
}
} // namespace clicknova::sequence
