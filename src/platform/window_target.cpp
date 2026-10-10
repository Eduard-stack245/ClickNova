// SPDX-License-Identifier: Apache-2.0
#include "clicknova/window_target.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <utility>

namespace clicknova::platform {
namespace {

HWND toHandle(std::uintptr_t value) { return reinterpret_cast<HWND>(value); }

bool isSameWindow(const WindowTarget& window) {
    HWND hwnd = toHandle(window.handle);
    if (!hwnd || !IsWindow(hwnd)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid != 0 && pid == window.processId;
}

std::optional<WindowTarget> identify(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return std::nullopt;
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return std::nullopt;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid || pid == GetCurrentProcessId()) return std::nullopt;
    std::wstring title(static_cast<std::size_t>(length + 1), L'\0');
    const int actual = GetWindowTextW(hwnd, title.data(), length + 1);
    if (actual <= 0) return std::nullopt;
    title.resize(static_cast<std::size_t>(actual));
    return WindowTarget{reinterpret_cast<std::uintptr_t>(hwnd),
                        static_cast<std::uint32_t>(pid), std::move(title)};
}

bool insideClient(HWND hwnd, POINT point) {
    if (!ScreenToClient(hwnd, &point)) return false;
    RECT bounds{};
    return GetClientRect(hwnd, &bounds) &&
           point.x >= bounds.left && point.x < bounds.right &&
           point.y >= bounds.top && point.y < bounds.bottom;
}

bool visibleAtPoint(HWND hwnd, POINT point) {
    HWND hit = WindowFromPoint(point);
    return hit && GetAncestor(hit, GA_ROOT) == GetAncestor(hwnd, GA_ROOT);
}

} // namespace

std::vector<WindowTarget> listWindows() {
    std::vector<WindowTarget> result;
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        if (GetWindow(hwnd, GW_OWNER) || (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW))
            return TRUE;
        auto* windows = reinterpret_cast<std::vector<WindowTarget>*>(param);
        if (auto candidate = identify(hwnd)) windows->push_back(std::move(*candidate));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    std::sort(result.begin(), result.end(), [](const WindowTarget& a, const WindowTarget& b) {
        return a.title < b.title;
    });
    return result;
}

std::optional<WindowTarget> foregroundWindow() {
    return identify(GetAncestor(GetForegroundWindow(), GA_ROOT));
}

std::optional<WindowTarget> windowUnderCursor() {
    POINT point{};
    if (!GetCursorPos(&point)) return std::nullopt;
    HWND hovered = WindowFromPoint(point);
    if (!hovered) return std::nullopt;
    return identify(GetAncestor(hovered, GA_ROOT));
}

TargetCheck checkTarget(const WindowTarget& window) {
    HWND hwnd = toHandle(window.handle);
    return evaluateWindowTarget(TargetSnapshot{
        isSameWindow(window),
        hwnd && IsWindowVisible(hwnd) != 0,
        hwnd && IsIconic(hwnd) != 0,
        hwnd && GetAncestor(GetForegroundWindow(), GA_ROOT) == GetAncestor(hwnd, GA_ROOT),
        true,
        true
    });
}

TargetCheck checkScreenPoint(const WindowTarget& window, int screenX, int screenY) {
    const auto status = checkTarget(window);
    if (status != TargetCheck::Ready) return status;
    const POINT point{static_cast<LONG>(screenX), static_cast<LONG>(screenY)};
    return evaluateWindowTarget(TargetSnapshot{
        true, true, false, true,
        insideClient(toHandle(window.handle), point),
        visibleAtPoint(toHandle(window.handle), point)
    });
}

TargetCheck checkCapturePoint(const WindowTarget& window, int screenX, int screenY) {
    HWND hwnd = toHandle(window.handle);
    if (!isSameWindow(window) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
        return TargetCheck::Missing;
    const POINT point{static_cast<LONG>(screenX), static_cast<LONG>(screenY)};
    return insideClient(hwnd, point) ? TargetCheck::Ready : TargetCheck::OutOfBounds;
}

TargetCheck resolveClientPoint(const WindowTarget& window, int clientX, int clientY,
                               int& screenX, int& screenY) {
    const auto status = checkTarget(window);
    if (status != TargetCheck::Ready) return status;
    RECT bounds{};
    HWND hwnd = toHandle(window.handle);
    if (!GetClientRect(hwnd, &bounds)) return TargetCheck::Missing;
    if (clientX < bounds.left || clientX >= bounds.right ||
        clientY < bounds.top || clientY >= bounds.bottom) return TargetCheck::OutOfBounds;
    POINT point{static_cast<LONG>(clientX), static_cast<LONG>(clientY)};
    if (!ClientToScreen(hwnd, &point)) return TargetCheck::Missing;
    screenX = point.x;
    screenY = point.y;
    return checkScreenPoint(window, screenX, screenY);
}

} // namespace clicknova::platform
#else
namespace clicknova::platform {
std::vector<WindowTarget> listWindows() { return {}; }
std::optional<WindowTarget> foregroundWindow() { return std::nullopt; }
std::optional<WindowTarget> windowUnderCursor() { return std::nullopt; }
TargetCheck checkTarget(const WindowTarget&) { return TargetCheck::Unsupported; }
TargetCheck checkScreenPoint(const WindowTarget&, int, int) { return TargetCheck::Unsupported; }
TargetCheck checkCapturePoint(const WindowTarget&, int, int) { return TargetCheck::Unsupported; }
TargetCheck resolveClientPoint(const WindowTarget&, int, int, int&, int&) { return TargetCheck::Unsupported; }
} // namespace clicknova::platform
#endif
