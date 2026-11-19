// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/input_actions.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace clicknova::gui::qml::action_edit {

// This application-service boundary deliberately uses no Qt types. The UI
// adapter converts values to UTF-16, while the domain rules remain testable
// with a standard C++20 compiler.
enum class Error { None, UnknownField, UnsupportedField, InvalidValue };

struct Result {
    input::Action action;
    Error error{Error::None};
    bool changed{false};
    explicit operator bool() const noexcept { return error == Error::None; }
};

// Native editor type IDs are stable in the UI and are *not* std::variant indexes.
int editorType(const input::Action& action) noexcept;
std::optional<std::u16string> read(const input::Action& action, std::string_view field);
Result apply(const input::Action& current, std::string_view field, std::u16string_view value);

} // namespace clicknova::gui::qml::action_edit
