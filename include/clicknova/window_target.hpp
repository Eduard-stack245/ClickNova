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

// On Linux these functions return no candidates / Unsupported, not fake success.
std::vector<WindowTarget> listWindows();
std::optional<WindowTarget> foregroundWindow();
TargetCheck checkTarget(const WindowTarget& window);
TargetCheck checkScreenPoint(const WindowTarget& window, int screenX, int screenY);
TargetCheck resolveClientPoint(const WindowTarget& window, int clientX, int clientY,
                               int& screenX, int& screenY);

} // namespace clicknova::platform
