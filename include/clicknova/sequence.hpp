// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace clicknova::sequence {

enum class Space : int { Screen = 0, Client = 1 };
struct Step {
    int x{0};
    int y{0};
    int button{0}; // 0=left, 1=right, 2=middle
    int delayMs{200}; // Delay BEFORE this click.
    Space space{Space::Screen};
};
struct Plan {
    std::vector<Step> steps;
    int repetitions{1}; // Complete passes, not individual clicks.
    bool unlimited{false};
};

// Pure, platform-independent validation, including bounds for imported settings.
std::string validate(const Plan& plan);

class Cursor {
public:
    explicit Cursor(Plan plan);
    std::optional<Step> peek() const;
    // Advance only AFTER successful click injection.
    void advance();
    std::uint64_t clicksCompleted() const { return completedClicks_; }
    std::uint64_t passesCompleted() const { return completedPasses_; }
private:
    Plan plan_;
    std::size_t index_{0};
    std::uint64_t completedClicks_{0};
    std::uint64_t completedPasses_{0};
};
} // namespace clicknova::sequence
