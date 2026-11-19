// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/input_actions.hpp"
#include <chrono>
#include <cstdint>
#include <vector>

namespace clicknova::input::timing {
struct StepEstimate {
    // The predictable part of a step, including waiting between clicks.
    std::int64_t durationMs{0};
    std::int64_t completedAfterMs{0};
    // A step may take anything up to the advertised timeout.
    bool variable{false};
    // An unbounded operation invalidates absolute forecasts for later steps.
    bool unknown{false};
    bool cumulativeKnown{true};
};
// The start point is the first action (not the GUI's three-second countdown).
// Estimates never drive playback. They intentionally exclude input dispatch,
// provider scheduling, OS delays, slow scripts and interruption.
std::vector<StepEstimate> estimate(const Plan& plan,
    std::chrono::system_clock::time_point firstActionAt);
}
