// SPDX-License-Identifier: MIT
#include "clicknova/quick_clicker_policy.hpp"
#include <limits>

namespace clicknova::quick {
namespace {
constexpr bool inRange(int value, int low, int high) noexcept {
    return value >= low && value <= high;
}
}
std::string_view validateClickerOptions(const ClickerOptions& x) noexcept {
    if (!inRange(static_cast<int>(x.action), 0, 3)) return "Unknown quick-click action";
    if (!inRange(x.button, 0, 4)) return "Mouse button must be Left, Right, Middle, X1 or X2";
    if (!inRange(x.intervalMs, 10, 60000)) return "Click interval must be 10..60000 ms";
    if (!inRange(x.holdMs, 0, 60000)) return "Click hold must be 0..60000 ms";
    if (!x.unlimited && (x.count == 0 || x.count > 2147483647ULL))
        return "Finite click count must be 1..2147483647";
    if (!inRange(x.position.x, -131072, 131072) || !inRange(x.position.y, -131072, 131072))
        return "Click coordinates are outside the supported range";
    if (!inRange(x.destination.x, -131072, 131072) || !inRange(x.destination.y, -131072, 131072))
        return "Movement coordinates are outside the supported range";
    if (!inRange(x.motionDurationMs, 0, 60000)) return "Movement duration must be 0..60000 ms";
    if (x.delayMs < 0) return "Start delay cannot be negative";
    if (x.startAtClock && (!inRange(x.clockHour, 0, 23) || !inRange(x.clockMinute, 0, 59) ||
                           !inRange(x.clockSecond, 0, 59)))
        return "Start clock must use a valid 24-hour time";
    if ((x.action == ActionMode::Move || x.action == ActionMode::Drag) && !x.destinationConfigured)
        return "Configure a destination before starting movement";
    if (x.action == ActionMode::HoldUntilStop && x.unlimited)
        return "Hold-until-Stop does not support unlimited repetition";
    return {};
}
bool ClickCycleCounter::completeCycle() noexcept {
    if (completed_ != std::numeric_limits<std::uint64_t>::max()) ++completed_;
    return !options_.unlimited && completed_ >= options_.count;
}
} // namespace clicknova::quick
