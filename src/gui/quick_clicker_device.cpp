// SPDX-License-Identifier: MIT
#include "quick_clicker_service.hpp"
#include "clicknova/windows_awake.hpp"
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include "clicknova/windows_input_backend.hpp"
#endif

namespace clicknova::gui {
namespace {
#ifdef _WIN32
bool pointIsOverOwnProcess(POINT point) noexcept {
    const HWND underPointer = ::WindowFromPoint(point);
    if (!underPointer) return false;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(underPointer, &pid);
    return pid == ::GetCurrentProcessId();
}
#endif
class NativeQuickClickerDevice final : public IQuickClickerDevice {
public:
    bool validateTarget(const platform::WindowTarget& target,
                        bool allowNotForeground, QString& error) override {
#ifdef _WIN32
        if (target.processId == ::GetCurrentProcessId()) {
            error = QStringLiteral("ClickNova cannot target its own window");
            return false;
        }
        const auto state = platform::checkTarget(target);
        if (state == platform::TargetCheck::Ready ||
            (allowNotForeground && state == platform::TargetCheck::NotForeground)) return true;
        error = QString::fromUtf8(platform::targetCheckMessage(state));
#else
        (void)target; (void)allowNotForeground;
        error = QStringLiteral("Native quick clicking is unavailable on this platform");
#endif
        return false;
    }
    bool resolveClientPoint(const platform::WindowTarget& target, quick::ClickerPoint client,
                            quick::ClickerPoint& screen, QString& error) override {
#ifdef _WIN32
        int sx = 0, sy = 0;
        const auto result = platform::resolveClientPoint(target, client.x, client.y, sx, sy);
        if (result != platform::TargetCheck::Ready) {
            error = QString::fromUtf8(platform::targetCheckMessage(result));
            return false;
        }
        screen = {sx, sy};
        return true;
#else
        (void)target; (void)client; (void)screen;
        error = QStringLiteral("Native quick clicking is unavailable on this platform");
        return false;
#endif
    }
    bool checkScreenPoint(const platform::WindowTarget& target, quick::ClickerPoint point,
                          QString& error) override {
#ifdef _WIN32
        const auto result = platform::checkScreenPoint(target, point.x, point.y);
        if (result == platform::TargetCheck::Ready) return true;
        error = QString::fromUtf8(platform::targetCheckMessage(result));
#else
        (void)target; (void)point;
        error = QStringLiteral("Native quick clicking is unavailable on this platform");
#endif
        return false;
    }
    bool cursorPosition(quick::ClickerPoint& out, QString& error) override {
#ifdef _WIN32
        POINT p{};
        if (::GetCursorPos(&p)) { out = {static_cast<int>(p.x), static_cast<int>(p.y)}; return true; }
        error = QStringLiteral("Cannot read cursor position");
#else
        error = QStringLiteral("Native quick clicking is unavailable on this platform");
#endif
        return false;
    }
    bool movePointer(quick::ClickerPoint point, QString& error) override {
#ifdef _WIN32
        if (held_ && pointIsOverOwnProcess(POINT{point.x, point.y})) {
            error = QStringLiteral("Drag cannot cross ClickNova controls");
            return false;
        }
        if (::SetCursorPos(point.x, point.y)) return true;
        error = QStringLiteral("Cannot move pointer");
#else
        (void)point;
        error = QStringLiteral("Native quick clicking is unavailable on this platform");
#endif
        return false;
    }
    bool press(int button, QString& error) override {
#ifdef _WIN32
        POINT point{};
        if (!::GetCursorPos(&point)) {
            error = QStringLiteral("Cannot verify cursor position before clicking");
            return false;
        }
        if (pointIsOverOwnProcess(point)) {
            error = QStringLiteral("Move the cursor outside ClickNova before clicking");
            return false;
        }
        clicknova::input::WindowsInputBackend backend;
        clicknova::input::Event event;
        event.kind = clicknova::input::EventKind::ButtonPress;
        event.button = static_cast<clicknova::input::Button>(button);
        if (backend.dispatch(event)) { held_ = true; return true; }
        error = QStringLiteral("Windows rejected mouse press");
#else
        (void)button;
        error = QStringLiteral("Native quick clicking is unavailable on this platform");
#endif
        return false;
    }
    bool release(int button, bool cleanup, QString& error) override {
#ifdef _WIN32
        clicknova::input::WindowsInputBackend backend;
        clicknova::input::Event event;
        event.kind = clicknova::input::EventKind::ButtonRelease;
        event.button = static_cast<clicknova::input::Button>(button);
        if (cleanup ? backend.releaseOnCleanup(event) : backend.dispatch(event)) { held_ = false; return true; }
        error = QStringLiteral("Windows rejected mouse release");
#else
        (void)button; (void)cleanup;
        error = QStringLiteral("Native quick clicking is unavailable on this platform");
#endif
        return false;
    }
    void keepAwake(bool enabled) override { awake_.setEnabled(enabled); }
private:
    bool held_{false};
    clicknova::platform::WindowsAwakeGuard awake_{false};
};
} // namespace
std::unique_ptr<IQuickClickerDevice> makeDefaultQuickClickerDevice() {
    return std::make_unique<NativeQuickClickerDevice>();
}
} // namespace clicknova::gui
