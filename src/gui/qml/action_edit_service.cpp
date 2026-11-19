// SPDX-License-Identifier: MIT
#include "action_edit_service.hpp"
#include "clicknova/duration_expression.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace clicknova::gui::qml::action_edit {
namespace {
using namespace clicknova::input;

std::optional<std::int64_t> decimal(std::u16string_view text) {
    if (text.empty()) return std::nullopt;
    std::size_t cursor = 0;
    bool negative = false;
    if (text.front() == u'-') { negative = true; cursor = 1; }
    if (cursor == text.size()) return std::nullopt;
    std::uint64_t accumulated = 0;
    constexpr auto positiveLimit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const std::uint64_t limit = positiveLimit + static_cast<std::uint64_t>(negative);
    for (; cursor < text.size(); ++cursor) {
        const char16_t c = text[cursor];
        if (c < u'0' || c > u'9') return std::nullopt;
        const unsigned digit = static_cast<unsigned>(c - u'0');
        if (accumulated > (limit - digit) / 10) return std::nullopt;
        accumulated = accumulated * 10 + digit;
    }
    if (negative && accumulated == limit) return std::numeric_limits<std::int64_t>::min();
    const auto number = static_cast<std::int64_t>(accumulated);
    return negative ? -number : number;
}

std::optional<int> bounded(std::u16string_view source, int min, int max) {
    const auto number = decimal(source);
    if (!number || *number < min || *number > max) return std::nullopt;
    return static_cast<int>(*number);
}

std::u16string digits(std::int64_t value) {
    const auto ascii = std::to_string(value);
    return std::u16string(ascii.begin(), ascii.end());
}

std::u16string clockText(const WaitUntil& clock) {
    std::u16string text;
    for (int number : {clock.hour, clock.minute, clock.second}) {
        if (!text.empty()) text.push_back(u':');
        text.push_back(static_cast<char16_t>(u'0' + number / 10));
        text.push_back(static_cast<char16_t>(u'0' + number % 10));
    }
    return text;
}

std::optional<WaitUntil> clockValue(std::u16string_view text) {
    if (text.size() != 8 || text[2] != u':' || text[5] != u':') return std::nullopt;
    for (int index : {0, 1, 3, 4, 6, 7})
        if (text[index] < u'0' || text[index] > u'9') return std::nullopt;
    const auto number = [&](int at) { return (text[at] - u'0') * 10 + (text[at + 1] - u'0'); };
    const WaitUntil clock{number(0), number(3), number(6)};
    if (clock.hour > 23 || clock.minute > 59 || clock.second > 59) return std::nullopt;
    return clock;
}

std::optional<Action> defaultAction(int type) {
    switch (type) {
    case 0: return Click{Button::Left, 1, 200};
    case 1: return KeyTap{0x41};
    case 2: return KeyDown{0x41};
    case 3: return KeyUp{0x41};
    case 4: return ButtonDown{Button::Left};
    case 5: return ButtonUp{Button::Left};
    case 6: return Wheel{Axis::Vertical, 120};
    case 7: return Wait{500};
    case 8: return MouseHold{Button::Left, 500};
    case 9: return KeyHold{0x41, 500};
    case 10: return TypeText{u""};
    case 11: return MovePointer{MoveMode::Relative, 0, 0, 0};
    case 12: return RunScript{u""};
    case 13: return WaitUntil{12, 0, 0};
    default: return std::nullopt;
    }
}

bool recognized(std::string_view field) {
    for (std::string_view allowed : {
         "inputLabType", "inputLabMouseButton", "inputLabClickCount", "inputLabClickIntervalMs",
         "inputLabVirtualKey", "inputLabHoldMs", "inputLabWheelAxis", "inputLabWheelDelta",
         "inputLabMoveMode", "inputLabMoveX", "inputLabMoveY", "inputLabMoveDuration",
         "inputLabWaitExpression", "inputLabWaitUntilClock", "inputLabText", "inputLabScript"})
        if (field == allowed) return true;
    return false;
}

} // namespace

int editorType(const input::Action& action) noexcept {
    return std::visit([](const auto& value) -> int {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Click>) return 0;
        else if constexpr (std::is_same_v<T, KeyTap>) return 1;
        else if constexpr (std::is_same_v<T, KeyDown>) return 2;
        else if constexpr (std::is_same_v<T, KeyUp>) return 3;
        else if constexpr (std::is_same_v<T, ButtonDown>) return 4;
        else if constexpr (std::is_same_v<T, ButtonUp>) return 5;
        else if constexpr (std::is_same_v<T, Wheel>) return 6;
        else if constexpr (std::is_same_v<T, Wait>) return 7;
        else if constexpr (std::is_same_v<T, MouseHold>) return 8;
        else if constexpr (std::is_same_v<T, KeyHold>) return 9;
        else if constexpr (std::is_same_v<T, TypeText>) return 10;
        else if constexpr (std::is_same_v<T, MovePointer>) return 11;
        else if constexpr (std::is_same_v<T, RunScript>) return 12;
        else if constexpr (std::is_same_v<T, WaitUntil>) return 13;
        else return -1; // Native-only ElementTask.
    }, action);
}

std::optional<std::u16string> read(const input::Action& action, std::string_view field) {
    if (field == "inputLabType") {
        const int type = editorType(action);
        return type < 0 ? std::nullopt : std::optional<std::u16string>{digits(type)};
    }
    return std::visit([&](const auto& value) -> std::optional<std::u16string> {
        using T = std::decay_t<decltype(value)>;
        if (field == "inputLabMouseButton") {
            if constexpr (std::is_same_v<T, Click> || std::is_same_v<T, ButtonDown> ||
                          std::is_same_v<T, ButtonUp> || std::is_same_v<T, MouseHold>)
                return digits(static_cast<int>(value.button));
        }
        if constexpr (std::is_same_v<T, Click>) {
            if (field == "inputLabClickCount") return digits(value.count);
            if (field == "inputLabClickIntervalMs") return digits(value.intervalMs);
        }
        if constexpr (std::is_same_v<T, KeyTap> || std::is_same_v<T, KeyDown> ||
                      std::is_same_v<T, KeyUp> || std::is_same_v<T, KeyHold>) {
            if (field == "inputLabVirtualKey") return digits(value.virtualKey);
        }
        if constexpr (std::is_same_v<T, MouseHold> || std::is_same_v<T, KeyHold>) {
            if (field == "inputLabHoldMs") return digits(value.durationMs);
        }
        if constexpr (std::is_same_v<T, Wheel>) {
            if (field == "inputLabWheelAxis") return digits(static_cast<int>(value.axis));
            if (field == "inputLabWheelDelta") return digits(value.delta);
        }
        if constexpr (std::is_same_v<T, MovePointer>) {
            if (field == "inputLabMoveMode") return digits(static_cast<int>(value.mode));
            if (field == "inputLabMoveX") return digits(value.x);
            if (field == "inputLabMoveY") return digits(value.y);
            if (field == "inputLabMoveDuration") return digits(value.durationMs);
        }
        if constexpr (std::is_same_v<T, Wait>) {
            if (field == "inputLabWaitExpression") return digits(value.durationMs) + u" ms";
        }
        if constexpr (std::is_same_v<T, WaitUntil>) {
            if (field == "inputLabWaitUntilClock") return clockText(value);
        }
        if constexpr (std::is_same_v<T, TypeText>) {
            if (field == "inputLabText") return value.text;
        }
        if constexpr (std::is_same_v<T, RunScript>) {
            if (field == "inputLabScript") return value.code;
        }
        return std::nullopt;
    }, action);
}

Result apply(const input::Action& current, std::string_view field, std::u16string_view value) {
    Result result{current};
    if (!recognized(field)) { result.error = Error::UnknownField; return result; }
    if (editorType(current) == -1) { result.error = Error::UnsupportedField; return result; }
    if (field == "inputLabType") {
        const auto type = bounded(value, 0, 13);
        if (!type) { result.error = Error::InvalidValue; return result; }
        if (editorType(current) == *type) return result;
        result.action = *defaultAction(*type);
        result.changed = true;
        return result;
    }
    if (!read(current, field)) { result.error = Error::UnsupportedField; return result; }

    const auto number = decimal(value);
    auto assign = [&](auto* item, auto member, auto parsed) {
        if (item->*member != parsed) {
            item->*member = parsed;
            result.changed = true;
        }
    };
    auto requireNumber = [&](std::int64_t lo, std::int64_t hi) -> std::optional<std::int64_t> {
        if (!number || *number < lo || *number > hi) { result.error = Error::InvalidValue; return std::nullopt; }
        return number;
    };
    if (field == "inputLabMouseButton") {
        if (const auto num = requireNumber(0, 4)) {
            const Button button = static_cast<Button>(*num);
            if (auto* item = std::get_if<Click>(&result.action)) assign(item, &Click::button, button);
            if (auto* item = std::get_if<ButtonDown>(&result.action)) assign(item, &ButtonDown::button, button);
            if (auto* item = std::get_if<ButtonUp>(&result.action)) assign(item, &ButtonUp::button, button);
            if (auto* item = std::get_if<MouseHold>(&result.action)) assign(item, &MouseHold::button, button);
        }
    } else if (field == "inputLabClickCount") {
        if (const auto num = requireNumber(1, std::numeric_limits<std::int64_t>::max()))
            assign(std::get_if<Click>(&result.action), &Click::count, *num);
    } else if (field == "inputLabClickIntervalMs") {
        if (const auto num = requireNumber(0, 60000)) assign(std::get_if<Click>(&result.action), &Click::intervalMs, static_cast<int>(*num));
    } else if (field == "inputLabVirtualKey") {
        if (const auto num = requireNumber(8, 254)) {
            const int code = static_cast<int>(*num);
            if (auto* item = std::get_if<KeyTap>(&result.action)) assign(item, &KeyTap::virtualKey, code);
            if (auto* item = std::get_if<KeyDown>(&result.action)) assign(item, &KeyDown::virtualKey, code);
            if (auto* item = std::get_if<KeyUp>(&result.action)) assign(item, &KeyUp::virtualKey, code);
            if (auto* item = std::get_if<KeyHold>(&result.action)) assign(item, &KeyHold::virtualKey, code);
        }
    } else if (field == "inputLabHoldMs") {
        if (const auto num = requireNumber(1, 60000)) {
            const int duration = static_cast<int>(*num);
            if (auto* item = std::get_if<MouseHold>(&result.action)) assign(item, &MouseHold::durationMs, duration);
            if (auto* item = std::get_if<KeyHold>(&result.action)) assign(item, &KeyHold::durationMs, duration);
        }
    } else if (field == "inputLabWheelAxis") {
        if (const auto num = requireNumber(0, 1)) assign(std::get_if<Wheel>(&result.action), &Wheel::axis, static_cast<Axis>(*num));
    } else if (field == "inputLabWheelDelta") {
        if (const auto num = requireNumber(-1200, 1200)) assign(std::get_if<Wheel>(&result.action), &Wheel::delta, static_cast<int>(*num));
    } else if (field == "inputLabMoveMode") {
        if (const auto num = requireNumber(0, 2)) assign(std::get_if<MovePointer>(&result.action), &MovePointer::mode, static_cast<MoveMode>(*num));
    } else if (field == "inputLabMoveX") {
        if (const auto num = requireNumber(-32768, 32767)) assign(std::get_if<MovePointer>(&result.action), &MovePointer::x, static_cast<int>(*num));
    } else if (field == "inputLabMoveY") {
        if (const auto num = requireNumber(-32768, 32767)) assign(std::get_if<MovePointer>(&result.action), &MovePointer::y, static_cast<int>(*num));
    } else if (field == "inputLabMoveDuration") {
        if (const auto num = requireNumber(0, 60000)) assign(std::get_if<MovePointer>(&result.action), &MovePointer::durationMs, static_cast<int>(*num));
    } else if (field == "inputLabWaitExpression") {
        const auto duration = clicknova::duration::parse(value);
        if (!duration || *duration < 0) result.error = Error::InvalidValue;
        else assign(std::get_if<Wait>(&result.action), &Wait::durationMs, *duration);
    } else if (field == "inputLabWaitUntilClock") {
        const auto clock = clockValue(value);
        if (!clock) result.error = Error::InvalidValue;
        else {
            auto* item = std::get_if<WaitUntil>(&result.action);
            if (item->hour != clock->hour || item->minute != clock->minute || item->second != clock->second) {
                *item = *clock;
                result.changed = true;
            }
        }
    } else if (field == "inputLabText" || field == "inputLabScript") {
        if (value.size() > 65536) result.error = Error::InvalidValue;
        else {
            const std::u16string text(value);
            if (auto* item = std::get_if<TypeText>(&result.action)) assign(item, &TypeText::text, text);
            if (auto* item = std::get_if<RunScript>(&result.action)) assign(item, &RunScript::code, text);
        }
    }
    if (result.error != Error::None) result.changed = false;
    return result;
}

} // namespace clicknova::gui::qml::action_edit
