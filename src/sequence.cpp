// SPDX-License-Identifier: Apache-2.0
#include "clicknova/sequence.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace clicknova::sequence {
std::string validate(const Plan& plan) {
    if (plan.steps.empty()) return "Add at least one sequence step.";
    if (plan.steps.size() > 128) return "Sequence exceeds the 128-step limit.";
    if (plan.repetitions < 1 || plan.repetitions > 1000000)
        return "Repeat count must be between 1 and 1,000,000.";
    for (const Step& step : plan.steps) {
        if (step.button < 0 || step.button > 2) return "Unsupported mouse button.";
        if (step.delayMs < 10 || step.delayMs > 60000) return "Delay must be between 10 and 60,000 ms.";
        if (step.x < -131072 || step.x > 131072 || step.y < -131072 || step.y > 131072)
            return "Point coordinates are out of range.";
        if (step.space != Space::Screen && step.space != Space::Client)
            return "Unsupported coordinate space.";
        if (step.space == Space::Client && (step.x < 0 || step.y < 0))
            return "Client coordinates cannot be negative.";
    }
    return {};
}
Cursor::Cursor(Plan plan) : plan_(std::move(plan)) {
    const auto error = validate(plan_);
    if (!error.empty()) throw std::invalid_argument(error);
}
std::optional<Step> Cursor::peek() const {
    if (!plan_.unlimited && completedPasses_ >= static_cast<std::uint64_t>(plan_.repetitions))
        return std::nullopt;
    if (completedClicks_ == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    return plan_.steps[index_];
}
void Cursor::advance() {
    if (!peek()) return;
    ++completedClicks_;
    if (++index_ == plan_.steps.size()) {
        index_ = 0;
        ++completedPasses_;
    }
}
} // namespace clicknova::sequence
