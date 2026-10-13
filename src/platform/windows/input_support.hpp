// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QApplication>
#include <QKeySequence>
#include <QKeyCombination>
#include <QTimer>
#include <functional>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <QAbstractNativeEventFilter>
#endif

namespace clicknova::windows {
struct ShortcutOptions {
    QKeySequence start{QStringLiteral("F6")};
    QKeySequence stop{QStringLiteral("F7")};
    QKeySequence pick{QStringLiteral("F8")};
    bool enableStart{true};
    bool enablePick{true};
};

#ifdef _WIN32
// Use the real foreground input stream. This is NOT targeted background input.
inline bool sendClick(DWORD buttonDown, DWORD buttonUp) {
    INPUT events[2]{};
    events[0].type = INPUT_MOUSE;
    events[0].mi.dwFlags = buttonDown;
    events[1].type = INPUT_MOUSE;
    events[1].mi.dwFlags = buttonUp;
    const UINT sent = SendInput(2, events, sizeof(INPUT));
    if (sent == 1) {
        // A partial injection may leave a button pressed; attempt to release it.
        INPUT release{};
        release.type = INPUT_MOUSE;
        release.mi.dwFlags = buttonUp;
        SendInput(1, &release, sizeof(INPUT));
    }
    return sent == 2;
}

// A temporary low-level hook captures the user's next left-click and consumes
// both down and up events, so choosing a point does not click the target app.
// It is installed only while the user explicitly requests point picking.
class MousePointPicker final {
public:
    ~MousePointPicker() { cancel(); }
    bool begin(std::function<void(POINT)> callback, std::function<void()> onCancel) {
        if (active_ || hook_) return false;
        callback_ = std::move(callback);
        cancelled_ = std::move(onCancel);
        hook_ = SetWindowsHookExW(WH_MOUSE_LL, &MousePointPicker::handle, GetModuleHandleW(nullptr), 0);
        if (!hook_) { callback_ = {}; cancelled_ = {}; return false; }
        active_ = this;
        keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &MousePointPicker::handleKeyboard,
                                         GetModuleHandleW(nullptr), 0);
        if (!keyboardHook_) { cancel(); return false; }
        return true;
    }
    void cancel() {
        if (hook_) UnhookWindowsHookEx(hook_);
        if (keyboardHook_) UnhookWindowsHookEx(keyboardHook_);
        hook_ = nullptr;
        keyboardHook_ = nullptr;
        if (active_ == this) active_ = nullptr;
        callback_ = {};
        cancelled_ = {};
        interceptedDown_ = false;
    }
    bool active() const { return hook_ != nullptr; }
private:
    static LRESULT CALLBACK handle(int code, WPARAM message, LPARAM data) {
        if (code < 0 || !active_) return CallNextHookEx(nullptr, code, message, data);
        auto* picker = active_;
        if ((reinterpret_cast<const MSLLHOOKSTRUCT*>(data)->flags & LLMHF_INJECTED) != 0)
            return CallNextHookEx(picker->hook_, code, message, data);
        if (message == WM_LBUTTONDOWN && !picker->interceptedDown_) {
            picker->interceptedDown_ = true;
            picker->pickedPoint_ = reinterpret_cast<const MSLLHOOKSTRUCT*>(data)->pt;
            return 1;
        }
        if (message == WM_LBUTTONUP && picker->interceptedDown_) {
            const auto callback = picker->callback_;
            const POINT pt = picker->pickedPoint_;
            picker->cancel();
            QTimer::singleShot(0, qApp, [callback, pt] { if (callback) callback(pt); });
            return 1;
        }
        return CallNextHookEx(picker->hook_, code, message, data);
    }
    static LRESULT CALLBACK handleKeyboard(int code, WPARAM message, LPARAM data) {
        if (code < 0 || !active_) return CallNextHookEx(nullptr, code, message, data);
        auto* picker = active_;
        const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
        if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
            key->vkCode == VK_ESCAPE && (key->flags & LLKHF_INJECTED) == 0) {
            const auto cancelled = picker->cancelled_;
            picker->cancel();
            QTimer::singleShot(0, qApp, [cancelled] { if (cancelled) cancelled(); });
            return 1;
        }
        return CallNextHookEx(picker->keyboardHook_, code, message, data);
    }
    inline static MousePointPicker* active_{nullptr};
    HHOOK hook_{nullptr};
    HHOOK keyboardHook_{nullptr};
    bool interceptedDown_{false};
    POINT pickedPoint_{};
    std::function<void(POINT)> callback_;
    std::function<void()> cancelled_;
};

// Only explicitly supported hotkeys are registered. OS-reserved combinations
// and shortcuts in use by other applications are never silently overridden.
struct ShortcutBinding {
    UINT modifiers{0};
    UINT key{0};
    bool valid{false};
};

inline ShortcutBinding toNativeBinding(const QKeySequence& sequence) {
    if (sequence.count() != 1) return {};
    const QKeyCombination chord = sequence[0];
    const Qt::KeyboardModifiers modifiers = chord.keyboardModifiers();
    if (modifiers.testFlag(Qt::MetaModifier) || modifiers.testFlag(Qt::GroupSwitchModifier)) return {};
    const int code = static_cast<int>(chord.key());
    UINT virtualKey = 0;
    if (code >= Qt::Key_F1 && code <= Qt::Key_F11) {
        virtualKey = VK_F1 + static_cast<UINT>(code - Qt::Key_F1);
    } else if (code >= Qt::Key_A && code <= Qt::Key_Z) {
        virtualKey = 'A' + static_cast<UINT>(code - Qt::Key_A);
    } else if (code >= Qt::Key_0 && code <= Qt::Key_9) {
        virtualKey = '0' + static_cast<UINT>(code - Qt::Key_0);
    } else {
        return {};
    }
    UINT nativeMods = MOD_NOREPEAT;
    if (modifiers.testFlag(Qt::ControlModifier)) nativeMods |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier)) nativeMods |= MOD_ALT;
    if (modifiers.testFlag(Qt::ShiftModifier)) nativeMods |= MOD_SHIFT;
    // Do not intercept ordinary typing with bare alphanumeric shortcuts.
    if (code < Qt::Key_F1 && !(nativeMods & (MOD_CONTROL | MOD_ALT))) return {};
    return {nativeMods, virtualKey, true};
}

inline bool sameBinding(const ShortcutBinding& left, const ShortcutBinding& right) {
    return left.modifiers == right.modifiers && left.key == right.key;
}

inline bool validateShortcuts(const ShortcutOptions& options) {
    const auto stop = toNativeBinding(options.stop);
    const auto start = toNativeBinding(options.start);
    const auto pick = toNativeBinding(options.pick);
    if (!stop.valid || (options.enableStart && !start.valid) ||
        (options.enablePick && !pick.valid)) return false;
    if (options.enableStart && sameBinding(stop, start)) return false;
    if (options.enablePick && (sameBinding(stop, pick) ||
        (options.enableStart && sameBinding(start, pick)))) return false;
    return true;
}

class Hotkeys final : public QAbstractNativeEventFilter {
public:
    Hotkeys(std::function<void()> onStart, std::function<void()> onStop,
            std::function<void()> onPick)
        : onStart_(std::move(onStart)), onStop_(std::move(onStop)),
          onPick_(std::move(onPick)) {}
    ~Hotkeys() override { unregisterAll(); }

    bool configure(const ShortcutOptions& options) {
        if (!validateShortcuts(options)) return false;
        const auto original = options_;
        const bool previouslyConfigured = configured_;
        unregisterAll();
        options_ = options;
        configured_ = true;
        if (suspended_ || registerAll()) return true;
        unregisterAll();
        options_ = original;
        configured_ = previouslyConfigured;
        if (configured_ && !suspended_) registerAll();
        return false;
    }
    void suspend() { suspended_ = true; unregisterAll(); }
    void resume() {
        if (!suspended_) return;
        suspended_ = false;
        if (configured_) registerAll();
    }
    void setPicking(bool picking) {
        picking_ = picking;
        if (suspended_) return;
        unregisterAll();
        if (configured_) registerAll();
    }
    bool setRunning(bool running) {
        // Never claim the safety stop is active while shortcuts are suspended.
        if (running && suspended_) return false;
        if (running_ == running) return true;
        unregisterAll();
        running_ = running;
        if (configured_ && !suspended_ && !registerAll()) {
            unregisterAll();
            running_ = !running;
            if (configured_) registerAll();
            return false;
        }
        return true;
    }
    bool ready() const { return configured_ && (!options_.enableStart || registeredStart_); }
    bool pickerReady() const { return registeredPick_; }
    const ShortcutOptions& configured() const { return options_; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override {
        Q_UNUSED(result);
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") return false;
        const auto* msg = static_cast<MSG*>(message);
        if (msg->message != WM_HOTKEY) return false;
        if (registeredStop_ && msg->wParam == kStopId) { onStop_(); return true; }
        if (registeredStart_ && msg->wParam == kStartId) { onStart_(); return true; }
        if (registeredPick_ && msg->wParam == kPickId) { onPick_(); return true; }
        return false;
    }
private:
    static constexpr int kStartId = 0x0C06;
    static constexpr int kStopId = 0x0C07;
    static constexpr int kPickId = 0x0C08;
    void unregisterAll() {
        if (registeredStart_) UnregisterHotKey(nullptr, kStartId);
        if (registeredStop_) UnregisterHotKey(nullptr, kStopId);
        if (registeredPick_) UnregisterHotKey(nullptr, kPickId);
        registeredStart_ = registeredStop_ = registeredPick_ = false;
    }
    bool registerAll() {
        if (!configured_ || suspended_) return false;
        if (running_) {
            const auto stop = toNativeBinding(options_.stop);
            registeredStop_ = RegisterHotKey(nullptr, kStopId, stop.modifiers, stop.key) != 0;
            if (!registeredStop_) return false;
        } else if (!picking_ && options_.enableStart) {
            const auto start = toNativeBinding(options_.start);
            registeredStart_ = RegisterHotKey(nullptr, kStartId, start.modifiers, start.key) != 0;
            if (!registeredStart_) return false;
        }
        if (picking_ && options_.enablePick) {
            const auto pick = toNativeBinding(options_.pick);
            registeredPick_ = RegisterHotKey(nullptr, kPickId, pick.modifiers, pick.key) != 0;
            // Timed capture remains available when the pick shortcut is in use.
        }
        return true;
    }
    ShortcutOptions options_;
    bool configured_{false};
    bool suspended_{false};
    bool picking_{false};
    bool running_{false};
    bool registeredStart_{false};
    bool registeredStop_{false};
    bool registeredPick_{false};
    std::function<void()> onStart_, onStop_, onPick_;
};
#endif

} // namespace clicknova::windows
