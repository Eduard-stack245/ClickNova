// SPDX-License-Identifier: Apache-2.0
#include "clicknova/sequence_to_input.hpp"
#include <utility>

namespace clicknova::sequence {
std::optional<clicknova::input::Plan> toInputPlan(const Plan& plan, std::string* error, bool requireDefaultWindow) {
    const auto fail = [&](std::string message) -> std::optional<clicknova::input::Plan> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };
    if (const auto issue = validate(plan); !issue.empty()) return fail(issue);
    if (plan.unlimited) return fail("Select a finite number of sequence passes before importing.");
    clicknova::input::Plan result;
    result.repetitions = plan.repetitions;
    result.passDelayMs = plan.passDelayMs;
    result.requireTargetWindow = requireDefaultWindow;
    for (const auto& step : plan.steps) {
        if (step.ownWindow)
            return fail("Per-action window overrides cannot use a single shared target. Select one window for the entire sequence before importing.");
        if (step.space == Space::Client) result.requireTargetWindow = true;
        if (!step.configured) return fail("Choose a point for every click before importing.");
        if (step.delayMs > 0) result.actions.emplace_back(clicknova::input::Wait{step.delayMs});
        result.actions.emplace_back(clicknova::input::MovePointer{
            step.space == Space::Client ? clicknova::input::MoveMode::WindowClient
                                         : clicknova::input::MoveMode::Absolute, step.x, step.y, 0});
        result.actions.emplace_back(clicknova::input::Click{
            static_cast<clicknova::input::Button>(step.button), step.clickCount, step.intervalMs});
    }
    if (const auto issue = clicknova::input::validate(result); !issue.empty())
        return fail("Converted plan is not supported: " + issue);
    if (error) error->clear();
    return result;
}
} // namespace clicknova::sequence
