// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <cinttypes>
#include <optional>
#include <string>
#include <vector>

namespace clicknova::sequence {

enum class Space : int { Screen = 0, Client = 1 };
struct Step {
    int x{0};
    int y{0};
    int button{0}; // 0=left, 1=right, 2=middle
    int delayMs{200}; // Delay before entering this action (only once per action).
    Space space{Space::Screen};
    int clickCount{1}; // Number of clicks at this step.
    int intervalMs{200}; // Delay between clicks within this action.
    bool configured{true}; // New blank actions cannot execute until a point is chosen.
    bool ownWindow{false}; // Override the sequence's default target for this step.
    // Transient window identity: never persist the handle or PID in a profile.
    std::uintptr_t windowHandle{0};
    std::uint32_t processId{0};
    std::string windowTitle;
};
struct Plan {
    std::vector<Step> steps;
    int repetitions{1}; // Complete passes, not individual clicks.
    bool unlimited{false};
    int passDelayMs{0}; // Additional wait between completed passes.
};

// Pure, platform-independent validation, including bounds for imported settings.
std::string validate(const Plan& plan);

class Cursor {
public:
    explicit Cursor(Plan plan);
    std::optional<Step> peek() const;
    // Delay until next injection, accounting for action and repetition boundaries.
    int nextDelayMs() const;
    // Advance only AFTER successful click injection.
    void advance();
    std::uint64_t clicksCompleted() const { return completedClicks_; }
    std::uint64_t passesCompleted() const { return completedPasses_; }
private:
    Plan plan_;
    std::size_t index_{0};
    int clickIndex_{0};
    std::uint64_t completedClicks_{0};
    std::uint64_t completedPasses_{0};
};
} // namespace clicknova::sequence
