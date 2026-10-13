// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/input_actions.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace clicknova::recording {
// Events are captured only after explicit Record. Text (when enabled) and
// window titles remain in memory during capture; an explicitly started
// recorder may checkpoint the session in the private local recovery directory.
enum class Kind { Move, MouseDown, MouseUp, Wheel, KeyDown, KeyUp, Text };
struct WindowInfo {
    std::u16string title;
    std::u16string process;
};
struct Sample {
    Kind kind{Kind::Move};
    std::uint64_t milliseconds{0}; // Paused time must already be subtracted.
    int x{0};
    int y{0};
    input::Button button{input::Button::Left};
    input::Axis axis{input::Axis::Vertical};
    int delta{0};
    int virtualKey{0};
    // Only populated when "record typed text" is explicitly enabled.
    std::u16string text;
    WindowInfo window;
};
struct Conversion {
    input::Plan plan;
    // Same indexes as plan.actions. These are labels, NOT automatic playback targets.
    std::vector<WindowInfo> windows;
    std::string error;
    bool completedHolds{false};
};
// Creates a safe, editable action list with timing, pointer paths, and balanced holds.
Conversion convert(const std::vector<Sample>& samples);
} // namespace clicknova::recording
