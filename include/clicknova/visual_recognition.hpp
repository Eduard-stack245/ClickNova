// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/ui_element_search.hpp"
#include <optional>

namespace clicknova::accessibility::visual {
// Visual selectors never hold native element or screen-coordinate handles.
// Only a validated, user-chosen PNG/JPEG/BMP file and a normalized anchor
// are stored in the scenario. Image pixels remain local to this machine.
bool imageAvailable();
bool ocrAvailable();
bool validTemplatePath(const std::wstring& path);
// Versioned, persisted selector with user-adjustable matching settings.
// Legacy cv:<path> and ocr:<text> selectors remain supported.
std::wstring makeSelector(Provider provider, const std::wstring& payload,
                          int confidence, int radiusPercent);
bool parseSelector(Provider provider, const std::wstring& selector,
                   std::wstring& payload, int& confidence, int& radiusPercent);
bool parseAnchor(const std::wstring& anchor, int& x, int& y);
std::wstring makeAnchor(int centerX, int centerY, int width, int height);
Result search(const clicknova::platform::WindowTarget& window, const Query& query);
// Read-only match against the chosen anchor; ambiguity fails closed.
bool present(const clicknova::platform::WindowTarget& window, Provider provider,
             const std::wstring& name, const std::wstring& selector,
             const std::wstring& anchor);
// Resolve the current visible center of a unique matched region; never use
// the saved normalized anchor directly as a screen coordinate.
std::optional<ScreenPoint> locate(const clicknova::platform::WindowTarget& window,
    Provider provider, const std::wstring& name, const std::wstring& selector,
    const std::wstring& anchor);
// Re-capture, re-recognize and send guarded input ONLY to the selected,
// currently foreground window. A missing or ambiguous match fails closed.
bool click(const clicknova::platform::WindowTarget& window, Provider provider,
           const std::wstring& name, const std::wstring& selector,
           const std::wstring& anchor);
} // namespace clicknova::accessibility::visual
