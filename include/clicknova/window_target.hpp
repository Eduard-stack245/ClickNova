// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace clicknova::platform {

// A transient window identity. Handles are never serialized or restored across runs.
struct WindowTarget {
    std::uintptr_t handle{};
    std::uint32_t processId{};
    std::wstring title;
    std::wstring process; // basename of executable when accessible
};

enum class TargetCheck {
    Ready,
    Unsupported,
    Missing,
    Hidden,
    Minimized,
    NotForeground,
    OutOfBounds,
    Obscured
};

struct TargetSnapshot {
    bool exists{};
    bool visible{};
    bool minimized{};
    bool foreground{};
    bool insideClient{};
    bool unobscured{};
};

// Pure policy, covered by tests independently of Win32 and Qt.
TargetCheck evaluateWindowTarget(const TargetSnapshot& snapshot);
const char* targetCheckMessage(TargetCheck status);

// On Linux with X11 support these functions inspect real X11 windows; without
// DISPLAY/X11 they return no candidates / Unsupported, not fake success.
std::vector<WindowTarget> listWindows();
std::optional<WindowTarget> foregroundWindow();
// Find a fresh live HWND from a persisted title/executable name; no cached HWND.
std::optional<WindowTarget> findWindow(const std::wstring& title, const std::wstring& process);
// Explicit foreground activation; Windows focus-stealing protection may reject it.
bool activateWindow(const WindowTarget& target);
// Identifies the top-level visible window below the cursor without activating it.
// A ClickNova-owned, minimized, or untitled window is not a valid target.
std::optional<WindowTarget> windowUnderCursor();
TargetCheck checkTarget(const WindowTarget& window);
// Native accessibility patterns can act on a non-foreground window. Require a
// live, same-process, visible, non-minimized window, but never force focus.
TargetCheck checkAccessibleTarget(const WindowTarget& window);
TargetCheck checkScreenPoint(const WindowTarget& window, int screenX, int screenY);
// Validates capture geometry, not the focus/occlusion requirements for playback.
TargetCheck checkCapturePoint(const WindowTarget& window, int screenX, int screenY);
TargetCheck resolveClientPoint(const WindowTarget& window, int clientX, int clientY,
                               int& screenX, int& screenY);

} // namespace clicknova::platform
