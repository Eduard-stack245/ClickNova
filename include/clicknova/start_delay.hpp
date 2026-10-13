// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <limits>
#include <optional>

namespace clicknova::timing {
// The UI supports seconds, minutes, hours, and days, including a zero delay.
// Return nullopt instead of overflowing the monotonic millisecond deadline.
constexpr std::optional<std::int64_t> startDelayMs(std::uint64_t amount, int unit) noexcept {
    constexpr std::int64_t multipliers[] = {1000, 60000, 3600000, 86400000};
    if (unit < 0 || unit >= 4) return std::nullopt;
    const auto scale = static_cast<std::uint64_t>(multipliers[unit]);
    if (amount > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / scale)
        return std::nullopt;
    return static_cast<std::int64_t>(amount * scale);
}

// Unlike ticking a fixed "seconds left" counter, this never drifts with
// background CPU load or pauses in the Windows UI message loop.
constexpr std::int64_t remainingDelayMs(std::int64_t targetMs,
                                         std::int64_t elapsedMs) noexcept {
    if (targetMs <= 0 || elapsedMs >= targetMs) return 0;
    if (elapsedMs <= 0) return targetMs;
    return targetMs - elapsedMs;
}
constexpr std::int64_t remainingWholeSeconds(std::int64_t targetMs,
                                              std::int64_t elapsedMs) noexcept {
    const auto ms = remainingDelayMs(targetMs, elapsedMs);
    return ms / 1000 + (ms % 1000 != 0 ? 1 : 0);
}
}  // namespace clicknova::timing
