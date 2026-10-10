// SPDX-License-Identifier: Apache-2.0
#include "clicknova/sequence_preview.hpp"

namespace clicknova::sequence {

std::optional<Preview> preview(const Plan& plan) {
    if (!validate(plan).empty()) return std::nullopt;

    Preview result;
    for (const Step& step : plan.steps) {
        result.clicksPerPass += static_cast<std::uint64_t>(step.clickCount);
        result.waitMsPerPass += static_cast<std::uint64_t>(step.delayMs);
        result.waitMsPerPass += static_cast<std::uint64_t>(step.clickCount - 1) *
                                static_cast<std::uint64_t>(step.intervalMs);
    }
    if (!plan.unlimited) {
        const auto passes = static_cast<std::uint64_t>(plan.repetitions);
        result.totalClicks = result.clicksPerPass * passes;
        result.totalWaitMs = result.waitMsPerPass * passes +
                             static_cast<std::uint64_t>(plan.passDelayMs) * (passes - 1);
    }
    return result;
}

} // namespace clicknova::sequence
