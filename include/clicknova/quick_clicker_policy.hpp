// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string_view>

namespace clicknova::quick {

enum class ActionMode : int { Click = 0, Move = 1, Drag = 2, HoldUntilStop = 3 };
struct ClickerPoint { int x{0}; int y{0}; };

// This is a value object, not a view/controller. Native HWNDs do not belong in
// a persisted configuration; the runtime target must be bound separately.
struct ClickerOptions {
    ActionMode action{ActionMode::Click};
    int button{0};                         // Left, Right, Middle, X1, X2
    int intervalMs{200};
    int holdMs{0};
    std::uint64_t count{100};
    bool unlimited{false};
    bool fixedPosition{false};
    ClickerPoint position{};
    bool restrictToWindow{false};
    bool destinationConfigured{false};
    ClickerPoint destination{};
    int motionDurationMs{600};
    bool keepAwake{true};
    std::int64_t delayMs{3000};
    bool startAtClock{false};
    int clockHour{18};
    int clockMinute{30};
    int clockSecond{0};
};

// Empty means valid. Pure and deterministic; safe to use from unit tests.
std::string_view validateClickerOptions(const ClickerOptions& options) noexcept;

class ClickCycleCounter final {
public:
    explicit ClickCycleCounter(const ClickerOptions& options) noexcept : options_(options) {}
    // Returns true when the specified number of successful cycles is finished.
    bool completeCycle() noexcept;
    void reset() noexcept { completed_ = 0; }
    std::uint64_t completed() const noexcept { return completed_; }
private:
    ClickerOptions options_;
    std::uint64_t completed_{0};
};
} // namespace clicknova::quick
