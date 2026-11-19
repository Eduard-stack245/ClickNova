// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/input_actions.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace clicknova::gui::qml::action_revision {

template<class> inline constexpr bool unhandled_action_v = false;

// A content revision of the *whole* native action/binding list. It is used as
// an optimistic-concurrency token, not as an authentication/security token.
// Never expose serialized action text, scripts or window titles to QML.
class Digest final {
public:
    void byte(std::uint8_t value) noexcept {
        a_ = (a_ ^ value) * 1099511628211ULL;
        b_ = (b_ ^ value) * 14029467366897019727ULL;
    }
    void number(std::uint64_t value) noexcept {
        for (unsigned i = 0; i < 8; ++i) {
            byte(static_cast<std::uint8_t>(value));
            value >>= 8;
        }
    }
    void text(std::string_view value) noexcept {
        number(value.size());
        for (const unsigned char c : value) byte(c);
    }
    void text16(std::u16string_view value) noexcept {
        number(value.size());
        for (const char16_t c : value) {
            byte(static_cast<std::uint8_t>(c));
            byte(static_cast<std::uint8_t>(c >> 8));
        }
    }
    template<class T> void scalar(T value) noexcept {
        static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
        number(static_cast<std::uint64_t>(value));
    }
    std::string hex() const {
        char buffer[33]{};
        std::snprintf(buffer, sizeof(buffer), "%016llx%016llx",
                      static_cast<unsigned long long>(a_),
                      static_cast<unsigned long long>(b_));
        return buffer;
    }
private:
    std::uint64_t a_{14695981039346656037ULL};
    std::uint64_t b_{7809847782465536322ULL};
};

inline void append(Digest& d, const input::Action& action) {
    d.number(action.index());
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, input::Click>) {
            d.scalar(value.button); d.scalar(value.count); d.scalar(value.intervalMs);
        } else if constexpr (std::is_same_v<T, input::ButtonDown> ||
                             std::is_same_v<T, input::ButtonUp>) {
            d.scalar(value.button);
        } else if constexpr (std::is_same_v<T, input::MouseHold>) {
            d.scalar(value.button); d.scalar(value.durationMs);
        } else if constexpr (std::is_same_v<T, input::Wheel>) {
            d.scalar(value.axis); d.scalar(value.delta);
        } else if constexpr (std::is_same_v<T, input::KeyTap> ||
                             std::is_same_v<T, input::KeyDown> ||
                             std::is_same_v<T, input::KeyUp>) {
            d.scalar(value.virtualKey);
        } else if constexpr (std::is_same_v<T, input::KeyHold>) {
            d.scalar(value.virtualKey); d.scalar(value.durationMs);
        } else if constexpr (std::is_same_v<T, input::TypeText>) {
            d.text16(value.text);
        } else if constexpr (std::is_same_v<T, input::Wait>) {
            d.scalar(value.durationMs);
        } else if constexpr (std::is_same_v<T, input::MovePointer>) {
            d.scalar(value.mode); d.scalar(value.x); d.scalar(value.y);
            d.scalar(value.durationMs);
        } else if constexpr (std::is_same_v<T, input::RunScript>) {
            d.text16(value.code);
        } else if constexpr (std::is_same_v<T, input::WaitUntil>) {
            d.scalar(value.hour); d.scalar(value.minute); d.scalar(value.second);
        } else if constexpr (std::is_same_v<T, input::ElementTask>) {
            d.scalar(value.command); d.text16(value.text); d.scalar(value.axis);
            d.scalar(value.delta); d.scalar(value.timeoutMs); d.scalar(value.pollMs);
            d.scalar(value.doubleIntervalMs);
        } else {
            // Fail compilation if the native action catalogue grows without
            // also updating the revision contract. Silent omissions would
            // permit stale QML drafts after edits to new action types.
            static_assert(unhandled_action_v<T>, "Update document revision for new action variant");
        }
    }, action);
}

inline void append(Digest& d, const input::StepWindow& window) {
    d.text(window.title); d.text(window.process); d.scalar(window.activate);
    d.scalar(window.waitMs); d.scalar(window.enforce);
    d.text(window.elementName); d.text(window.elementAutomationId);
    d.text(window.elementClass); d.scalar(window.elementProvider);
    d.scalar(window.elementRole); d.scalar(window.elementBrowser);
    d.text(window.elementTab); d.scalar(window.elementOperation);
    d.text(window.elementValue); d.text(window.manualGroupId);
    d.text(window.manualGroupTitle); d.text(window.displayName);
}

inline std::string snapshot(const std::vector<input::Action>& actions,
                            const std::vector<input::StepWindow>& windows) {
    Digest d;
    d.number(actions.size());
    d.number(windows.size());
    for (const auto& action : actions) append(d, action);
    for (const auto& window : windows) append(d, window);
    return d.hex();
}

} // namespace clicknova::gui::qml::action_revision
