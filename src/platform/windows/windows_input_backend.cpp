// SPDX-License-Identifier: Apache-2.0
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "clicknova/windows_input_backend.hpp"
#include <algorithm>
#include <cstdint>
#include <climits>

namespace clicknova::input {
namespace {
bool extendedKey(int vk) noexcept {
    switch (vk) {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT:
    case VK_UP: case VK_DOWN: case VK_DIVIDE: case VK_NUMLOCK:
    case VK_RCONTROL: case VK_RMENU: case VK_SNAPSHOT:
        return true;
    default: return false;
    }
}

DWORD buttonFlag(Button b, bool down) noexcept {
    switch (b) {
    case Button::Left: return down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    case Button::Right: return down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    case Button::Middle: return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    case Button::X1: case Button::X2: return down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    }
    return 0;
}
} // namespace

std::optional<PointerPosition> WindowsInputBackend::cursorPosition() {
    POINT p{};
    if (!GetCursorPos(&p)) return std::nullopt;
    return PointerPosition{p.x, p.y};
}

std::optional<PointerPosition> WindowsInputBackend::clientToScreen(int x, int y) {
    if (!target_) return std::nullopt;
    int screenX = 0, screenY = 0;
    if (clicknova::platform::resolveClientPoint(*target_, x, y, screenX, screenY)
        != clicknova::platform::TargetCheck::Ready) return std::nullopt;
    return PointerPosition{screenX, screenY};
}

bool WindowsInputBackend::dispatch(const Event& event) {
    if (target_) {
        if (clicknova::platform::checkTarget(*target_) != clicknova::platform::TargetCheck::Ready)
            return false;
        // Do not press/click into an overlapping application or outside the
        // selected window. A screen-relative move must also stay inside it.
        if (event.kind == EventKind::ButtonPress || event.kind == EventKind::WheelTurn ||
            event.kind == EventKind::PointerMove) {
            POINT point{};
            if (event.kind == EventKind::PointerMove && event.absolute) {
                point = POINT{static_cast<LONG>(event.x), static_cast<LONG>(event.y)};
            } else {
                if (!GetCursorPos(&point)) return false;
                if (event.kind == EventKind::PointerMove) {
                    // Relative movement must not wander outside the target client area.
                    const auto x = static_cast<std::int64_t>(point.x) + event.x;
                    const auto y = static_cast<std::int64_t>(point.y) + event.y;
                    if (x < LONG_MIN || x > LONG_MAX || y < LONG_MIN || y > LONG_MAX) return false;
                    point = POINT{static_cast<LONG>(x), static_cast<LONG>(y)};
                }
            }
            if (clicknova::platform::checkScreenPoint(*target_, point.x, point.y)
                != clicknova::platform::TargetCheck::Ready) return false;
        }
    }
    return sendRaw(event);
}

bool WindowsInputBackend::releaseOnCleanup(const Event& event) {
    // Only release events bypass the focus guard. This prevents sticky keys
    // when the selected window disappears during a drag or long key hold.
    if (event.kind != EventKind::ButtonRelease && event.kind != EventKind::KeyRelease)
        return false;
    return sendRaw(event);
}

bool WindowsInputBackend::sendRaw(const Event& event) {
    INPUT raw{};
    switch (event.kind) {
    case EventKind::PointerMove: {
        raw.type = INPUT_MOUSE;
        if (!event.absolute) {
            raw.mi.dwFlags = MOUSEEVENTF_MOVE;
            raw.mi.dx = event.x;
            raw.mi.dy = event.y;
        } else {
            const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
            const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
            const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            if (width <= 0 || height <= 0) return false;
            // SendInput accepts 0..65535 across the entire virtual desktop.
            const auto normalized = [](int coordinate, int start, int extent) -> LONG {
                const auto offset = std::clamp(static_cast<std::int64_t>(coordinate) - start,
                                               std::int64_t{0}, static_cast<std::int64_t>(extent - 1));
                return static_cast<LONG>((offset * 65535) / std::max(1, extent - 1));
            };
            raw.mi.dx = normalized(event.x, left, width);
            raw.mi.dy = normalized(event.y, top, height);
            raw.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        }
        break;
    }
    case EventKind::ButtonPress:
    case EventKind::ButtonRelease: {
        raw.type = INPUT_MOUSE;
        raw.mi.dwFlags = buttonFlag(event.button, event.kind == EventKind::ButtonPress);
        if (!raw.mi.dwFlags) return false;
        if (event.button == Button::X1) raw.mi.mouseData = XBUTTON1;
        if (event.button == Button::X2) raw.mi.mouseData = XBUTTON2;
        break;
    }
    case EventKind::WheelTurn:
        if (event.delta == 0 || event.delta < -1200 || event.delta > 1200) return false;
        raw.type = INPUT_MOUSE;
        if (event.axis == Axis::Vertical) raw.mi.dwFlags = MOUSEEVENTF_WHEEL;
        else if (event.axis == Axis::Horizontal) raw.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        else return false;
        raw.mi.mouseData = static_cast<DWORD>(event.delta);
        break;
    case EventKind::UnicodePress:
    case EventKind::UnicodeRelease:
        if (event.unicodeUnit == 0) return false;
        raw.type = INPUT_KEYBOARD;
        raw.ki.wVk = 0;
        raw.ki.wScan = static_cast<WORD>(event.unicodeUnit);
        raw.ki.dwFlags = KEYEVENTF_UNICODE |
                         (event.kind == EventKind::UnicodeRelease ? KEYEVENTF_KEYUP : 0u);
        break;
    case EventKind::KeyPress:
    case EventKind::KeyRelease:
        if (event.virtualKey < 0x08 || event.virtualKey > 0xFE) return false;
        raw.type = INPUT_KEYBOARD;
        raw.ki.wVk = static_cast<WORD>(event.virtualKey);
        raw.ki.dwFlags = (event.kind == EventKind::KeyRelease ? KEYEVENTF_KEYUP : 0u) |
                         (extendedKey(event.virtualKey) ? KEYEVENTF_EXTENDEDKEY : 0u);
        break;
    }
    return SendInput(1, &raw, sizeof(INPUT)) == 1;
}
} // namespace clicknova::input
