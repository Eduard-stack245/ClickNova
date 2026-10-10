// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/sequence.hpp"

#include <cstdint>
#include <optional>

namespace clicknova::sequence {

// A deterministic preview of the requested clicks and scheduled waits.
// It does not include the application's startup countdown, OS scheduling,
// input injection time, or pauses caused by stopping a run.
struct Preview {
    std::uint64_t clicksPerPass{0};
    std::uint64_t waitMsPerPass{0};
    std::optional<std::uint64_t> totalClicks;
    std::optional<std::uint64_t> totalWaitMs;
};

// Invalid plans have no preview. Unlimited plans have no total values.
std::optional<Preview> preview(const Plan& plan);

} // namespace clicknova::sequence
