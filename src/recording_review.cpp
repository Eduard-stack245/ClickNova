// SPDX-License-Identifier: Apache-2.0
#include "clicknova/recording_review.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

namespace clicknova::recording::review {
namespace {
constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
std::uint64_t saturatingAdd(std::uint64_t lhs, std::uint64_t rhs) {
    return rhs > kMax - lhs ? kMax : lhs + rhs;
}
std::uint64_t saturatingMultiply(std::uint64_t lhs, std::uint64_t rhs) {
    return lhs && rhs > kMax / lhs ? kMax : lhs * rhs;
}
struct HoldIdentity {
    bool key{};
    int code{};
    bool down{};
};
std::optional<HoldIdentity> identity(const input::Action& action) {
    return std::visit([](const auto& a) -> std::optional<HoldIdentity> {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::is_same_v<T, input::ButtonDown>) return HoldIdentity{false, static_cast<int>(a.button), true};
        else if constexpr (std::is_same_v<T, input::ButtonUp>) return HoldIdentity{false, static_cast<int>(a.button), false};
        else if constexpr (std::is_same_v<T, input::KeyDown>) return HoldIdentity{true, a.virtualKey, true};
        else if constexpr (std::is_same_v<T, input::KeyUp>) return HoldIdentity{true, a.virtualKey, false};
        else return std::nullopt;
    }, action);
}
bool valid(const std::vector<input::Action>& actions) {
    return input::validate(input::Plan{actions, 1, 0, false, {}}).empty();
}
} // namespace

Summary summarize(const std::vector<input::Action>& actions) {
    Summary out;
    out.actions = actions.size();
    for (const auto& action : actions) {
        std::visit([&](const auto& a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, input::Click>) {
                out.mousePresses = saturatingAdd(out.mousePresses, static_cast<std::uint64_t>(a.count));
                out.scheduledMilliseconds = saturatingAdd(out.scheduledMilliseconds, saturatingMultiply(
                    static_cast<std::uint64_t>(a.intervalMs),
                    static_cast<std::uint64_t>(std::max<std::int64_t>(a.count - 1, 0))));
            } else if constexpr (std::is_same_v<T, input::ButtonDown>) {
                ++out.mousePresses;
            } else if constexpr (std::is_same_v<T, input::MouseHold>) {
                ++out.mousePresses;
                out.scheduledMilliseconds = saturatingAdd(out.scheduledMilliseconds, static_cast<std::uint64_t>(a.durationMs));
            } else if constexpr (std::is_same_v<T, input::KeyTap> || std::is_same_v<T, input::KeyDown>) {
                ++out.keyPresses;
            } else if constexpr (std::is_same_v<T, input::KeyHold>) {
                ++out.keyPresses;
                out.scheduledMilliseconds = saturatingAdd(out.scheduledMilliseconds, static_cast<std::uint64_t>(a.durationMs));
            } else if constexpr (std::is_same_v<T, input::MovePointer>) {
                ++out.moves;
                out.scheduledMilliseconds = saturatingAdd(out.scheduledMilliseconds, static_cast<std::uint64_t>(a.durationMs));
            } else if constexpr (std::is_same_v<T, input::Wheel>) {
                ++out.wheelActions;
            } else if constexpr (std::is_same_v<T, input::Wait>) {
                ++out.waits;
                out.scheduledMilliseconds = saturatingAdd(out.scheduledMilliseconds, static_cast<std::uint64_t>(a.durationMs));
            } else if constexpr (std::is_same_v<T, input::TypeText>) {
                out.keyPresses += a.text.size();
            }
        }, action);
    }
    return out;
}

bool eraseWithPair(std::vector<input::Action>& actions, std::size_t index,
                   std::vector<std::size_t>* removedIndexes) {
    if (index >= actions.size()) return false;
    const auto target = identity(actions[index]);
    auto candidate = actions;
    std::vector<std::size_t> removed{index};
    if (target) {
        std::optional<std::size_t> partner;
        if (target->down) {
            for (std::size_t i = index + 1; i < candidate.size(); ++i) {
                const auto found = identity(candidate[i]);
                if (found && found->key == target->key && found->code == target->code && !found->down) { partner = i; break; }
            }
        } else {
            for (std::size_t i = index; i > 0; --i) {
                const auto found = identity(candidate[i - 1]);
                if (found && found->key == target->key && found->code == target->code && found->down) { partner = i - 1; break; }
            }
        }
        if (partner) {
            removed = {std::min(index, *partner), std::max(index, *partner)};
            candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(std::max(index, *partner)));
            candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(std::min(index, *partner)));
        } else {
            candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(index));
        }
    } else {
        candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(index));
    }
    if (!valid(candidate) && !candidate.empty()) return false;
    actions = std::move(candidate);
    if (removedIndexes) *removedIndexes = std::move(removed);
    return true;
}

bool moveValidated(std::vector<input::Action>& actions, std::size_t from, std::size_t to) {
    if (from >= actions.size() || to >= actions.size()) return false;
    auto candidate = actions;
    const auto item = std::move(candidate[from]);
    candidate.erase(candidate.begin() + static_cast<std::ptrdiff_t>(from));
    candidate.insert(candidate.begin() + static_cast<std::ptrdiff_t>(to), item);
    if (!valid(candidate)) return false;
    actions = std::move(candidate);
    return true;
}

bool replaceValidated(std::vector<input::Action>& actions, std::size_t index, input::Action replacement) {
    if (index >= actions.size()) return false;
    auto candidate = actions;
    candidate[index] = std::move(replacement);
    if (!valid(candidate)) return false;
    actions = std::move(candidate);
    return true;
}
} // namespace clicknova::recording::review
