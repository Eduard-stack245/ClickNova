// SPDX-License-Identifier: Apache-2.0
#include "clicknova/sequence_preview.hpp"
#include <limits>

namespace clicknova::sequence {
namespace {
constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    return b > kMax - a ? kMax : a + b;
}
std::uint64_t mul(std::uint64_t a, std::uint64_t b) {
    return a && b > kMax / a ? kMax : a * b;
}
} // namespace

std::optional<Preview> preview(const Plan& plan) {
    if (!validate(plan).empty()) return std::nullopt;

    Preview result;
    for (const Step& step : plan.steps) {
        result.clicksPerPass = add(result.clicksPerPass, static_cast<std::uint64_t>(step.clickCount));
        result.waitMsPerPass = add(result.waitMsPerPass, static_cast<std::uint64_t>(step.delayMs));
        result.waitMsPerPass = add(result.waitMsPerPass,
            mul(static_cast<std::uint64_t>(step.clickCount - 1),
                static_cast<std::uint64_t>(step.intervalMs)));
    }
    if (!plan.unlimited) {
        const auto passes = static_cast<std::uint64_t>(plan.repetitions);
        result.totalClicks = mul(result.clicksPerPass, passes);
        result.totalWaitMs = add(mul(result.waitMsPerPass, passes),
                                 mul(static_cast<std::uint64_t>(plan.passDelayMs), passes - 1));
    }
    return result;
}

} // namespace clicknova::sequence
