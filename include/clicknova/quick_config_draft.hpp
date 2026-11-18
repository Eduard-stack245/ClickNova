// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <initializer_list>
#include <string_view>

// Pure C++20 edit policy for QML quick-click settings. No Qt, no input injection.
// Incomplete drafts are allowed, but invalid individual values never propagate.
namespace clicknova::quick {

struct ConfigDraft {
    int action{0};
    int button{0};
    int intervalMs{200};
    int count{100};
    bool unlimited{false};
    int holdMs{0};
    int startMode{0};
    int delayValue{3};
    int delayUnit{0};
    int clockHour{18};
    int clockMinute{30};
    int clockSecond{0};
    bool keepAwake{true};
    int positionMode{0};
    int x{0};
    int y{0};
    int destinationX{0};
    int destinationY{0};
    int movementMs{600};
    int targetMode{0};
    bool destinationConfigured{false};

    bool operator==(const ConfigDraft&) const = default;
};

enum class DraftField {
    Invalid, Action, Button, Interval, Count, Unlimited, Hold, StartMode,
    DelayValue, DelayUnit, Clock, KeepAwake, PositionMode, X, Y,
    DestinationX, DestinationY, Movement, TargetMode, DestinationConfigured
};

constexpr DraftField draftField(std::string_view name) noexcept {
    using F = DraftField;
    if (name == "quickActionMode") return F::Action;
    if (name == "quickMouseButton") return F::Button;
    if (name == "quickClickInterval") return F::Interval;
    if (name == "quickClickCount") return F::Count;
    if (name == "quickUnlimited") return F::Unlimited;
    if (name == "quickHoldDuration") return F::Hold;
    if (name == "quickStartMode") return F::StartMode;
    if (name == "quickStartDelayValue") return F::DelayValue;
    if (name == "quickStartDelayUnit") return F::DelayUnit;
    if (name == "quickStartAtTime") return F::Clock;
    if (name == "quickKeepAwake") return F::KeepAwake;
    if (name == "positionMode") return F::PositionMode;
    if (name == "coordinateX") return F::X;
    if (name == "coordinateY") return F::Y;
    if (name == "quickDestinationX") return F::DestinationX;
    if (name == "quickDestinationY") return F::DestinationY;
    if (name == "quickMoveDuration") return F::Movement;
    if (name == "targetMode") return F::TargetMode;
    if (name == "destinationConfigured") return F::DestinationConfigured;
    return F::Invalid;
}

constexpr bool draftIntField(DraftField f) noexcept {
    using F = DraftField;
    switch (f) {
    case F::Action: case F::Button: case F::Interval: case F::Count:
    case F::Hold: case F::StartMode: case F::DelayValue: case F::DelayUnit:
    case F::PositionMode: case F::X: case F::Y: case F::DestinationX:
    case F::DestinationY: case F::Movement: case F::TargetMode: return true;
    default: return false;
    }
}
constexpr bool draftBoolField(DraftField f) noexcept {
    using F = DraftField;
    return f == F::Unlimited || f == F::KeepAwake || f == F::DestinationConfigured;
}

constexpr bool assignDraftInt(ConfigDraft& d, DraftField f, std::int64_t value) noexcept {
    using F = DraftField;
    const auto range = [value](std::int64_t low, std::int64_t high) {
        return value >= low && value <= high;
    };
    if (!draftIntField(f)) return false;
    switch (f) {
    case F::Action: if (!range(0, 3)) return false; d.action = static_cast<int>(value); break;
    case F::Button: if (!range(0, 4)) return false; d.button = static_cast<int>(value); break;
    case F::Interval: if (!range(10, 60000)) return false; d.intervalMs = static_cast<int>(value); break;
    case F::Count: if (!range(1, 2147483647)) return false; d.count = static_cast<int>(value); break;
    case F::Hold: if (!range(0, 60000)) return false; d.holdMs = static_cast<int>(value); break;
    case F::StartMode: if (!range(0, 1)) return false; d.startMode = static_cast<int>(value); break;
    case F::DelayValue: if (!range(0, 2147483647)) return false; d.delayValue = static_cast<int>(value); break;
    case F::DelayUnit: if (!range(0, 3)) return false; d.delayUnit = static_cast<int>(value); break;
    case F::PositionMode: if (!range(0, 1)) return false; d.positionMode = static_cast<int>(value); break;
    case F::X: if (!range(-131072, 131072)) return false; d.x = static_cast<int>(value); break;
    case F::Y: if (!range(-131072, 131072)) return false; d.y = static_cast<int>(value); break;
    case F::DestinationX:
        if (!range(-131072, 131072)) return false;
        d.destinationX = static_cast<int>(value); d.destinationConfigured = true; break;
    case F::DestinationY:
        if (!range(-131072, 131072)) return false;
        d.destinationY = static_cast<int>(value); d.destinationConfigured = true; break;
    case F::Movement: if (!range(0, 60000)) return false; d.movementMs = static_cast<int>(value); break;
    case F::TargetMode: if (!range(0, 1)) return false; d.targetMode = static_cast<int>(value); break;
    default: return false;
    }
    return true;
}
constexpr bool assignDraftBool(ConfigDraft& d, DraftField f, bool value) noexcept {
    using F = DraftField;
    if (!draftBoolField(f)) return false;
    switch (f) {
    case F::Unlimited: d.unlimited = value; return true;
    case F::KeepAwake: d.keepAwake = value; return true;
    case F::DestinationConfigured: d.destinationConfigured = value; return true;
    default: return false;
    }
}

constexpr bool assignDraftClock(ConfigDraft& d, std::string_view clock) noexcept {
    if (clock.size() != 8 || clock[2] != ':' || clock[5] != ':') return false;
    for (const int i : {0, 1, 3, 4, 6, 7}) {
        if (clock[static_cast<std::size_t>(i)] < '0' || clock[static_cast<std::size_t>(i)] > '9') return false;
    }
    const auto two = [clock](int index) {
        return (clock[static_cast<std::size_t>(index)] - '0') * 10 +
               (clock[static_cast<std::size_t>(index + 1)] - '0');
    };
    const int hh = two(0), mm = two(3), ss = two(6);
    if (hh > 23 || mm > 59 || ss > 59) return false;
    d.clockHour = hh; d.clockMinute = mm; d.clockSecond = ss;
    return true;
}
} // namespace clicknova::quick
