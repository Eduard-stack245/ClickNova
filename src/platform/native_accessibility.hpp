// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/ui_element_search.hpp"
#include <optional>

namespace clicknova::accessibility::native {
// These backends are read-only during discovery. Element selectors are stable
// only as a guarded hierarchy path, never as retained cross-process pointers.
Result searchMsaa(const clicknova::platform::WindowTarget& window, const Query& query);
Result searchJava(const clicknova::platform::WindowTarget& window, const Query& query);
std::optional<ScreenPoint> pointMsaa(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& path, Role role);
std::optional<ScreenPoint> pointJava(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& path, Role role);
bool isJavaWindow(const clicknova::platform::WindowTarget& window);
bool performMsaa(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& path, Role role,
    Operation operation, const std::wstring& value, bool allowBackground);
bool performJava(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& path, Role role,
    Operation operation, const std::wstring& value, bool allowBackground);
} // namespace clicknova::accessibility::native
