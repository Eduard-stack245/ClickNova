// SPDX-License-Identifier: MIT
#include "clicknova/step_timing.hpp"
#include <algorithm>
#include <limits>
#include <type_traits>
#include <variant>

namespace clicknova::input::timing {
namespace {
std::int64_t add(std::int64_t a, std::int64_t b) {
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    return b > max - a ? max : a + b;
}
std::int64_t mul(std::int64_t a, std::int64_t b) {
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    return a > 0 && b > max/a ? max : a*b;
}
} // namespace

std::vector<StepEstimate> estimate(const Plan& plan,
    std::chrono::system_clock::time_point firstActionAt) {
    std::vector<StepEstimate> entries;
    entries.reserve(plan.actions.size());
    std::int64_t cumulative = 0;
    bool predictable = true;
    for (std::size_t i = 0; i < plan.actions.size(); ++i) {
        StepEstimate item{};
        const auto& a = plan.actions[i];
        std::visit([&](const auto& action) {
            using T = std::decay_t<decltype(action)>;
            if constexpr (std::is_same_v<T, Click>) {
                item.durationMs = mul(std::max<std::int64_t>(0, action.count-1),
                                      std::max<std::int64_t>(0, action.intervalMs));
            } else if constexpr (std::is_same_v<T, Wait> ||
                                 std::is_same_v<T, MouseHold> ||
                                 std::is_same_v<T, KeyHold> ||
                                 std::is_same_v<T, MovePointer>) {
                item.durationMs = std::max<std::int64_t>(0, action.durationMs);
            } else if constexpr (std::is_same_v<T, WaitUntil>) {
                if (predictable && cumulative <= 100LL*365*86400000) {
                    const auto proposed = firstActionAt + std::chrono::milliseconds(cumulative);
                    const auto time = millisecondsUntil(action, proposed);
                    if (time >= 0) item.durationMs = time;
                    else item.unknown = true;
                } else item.unknown = true;
            } else if constexpr (std::is_same_v<T, RunScript>) {
                item.unknown = true;
            } else if constexpr (std::is_same_v<T, ElementTask>) {
                item.variable = true;
                if (action.command == ElementCommand::WaitVisible) {
                    item.durationMs = std::max(0, action.timeoutMs);
                } else if (action.command == ElementCommand::DoubleActivate) {
                    item.durationMs = std::max(0, action.doubleIntervalMs);
                }
            }
        }, a);
        if (i < plan.stepWindows.size() && !plan.stepWindows[i].title.empty() &&
            plan.stepWindows[i].enforce && plan.stepWindows[i].waitMs > 0) {
            item.durationMs = add(item.durationMs, plan.stepWindows[i].waitMs);
            item.variable = true;
        }
        if (item.unknown) predictable = false;
        if (predictable) cumulative = add(cumulative, item.durationMs);
        item.cumulativeKnown = predictable;
        item.completedAfterMs = cumulative;
        entries.push_back(item);
    }
    return entries;
}
} // namespace clicknova::input::timing
