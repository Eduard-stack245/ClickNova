// SPDX-License-Identifier: Apache-2.0
#include "clicknova/x11_input_backend.hpp"
#include "x11_error_sync.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <poll.h>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdint>
#include <thread>
#include <locale>
#include <codecvt>
#include <cstdlib>
#include <string>

namespace { constexpr int x11KeyPressEvent = KeyPress; }
#undef KeyPress
#undef KeyRelease
#undef ButtonPress
#undef ButtonRelease
#include <utility>

namespace clicknova::input {
namespace {
using Query = Bool(*)(Display*, int*, int*, int*, int*);
using FakeKey = int(*)(Display*, unsigned int, Bool, unsigned long);
using FakeButton = int(*)(Display*, unsigned int, Bool, unsigned long);
using FakeMotion = int(*)(Display*, int, int, int, unsigned long);
using FakeRelative = int(*)(Display*, int, int, unsigned long);
struct XTest {
    Query query{};
    FakeKey key{};
    FakeButton button{};
    FakeMotion motion{};
    FakeRelative relative{};
    explicit XTest(void* lib) {
        if (!lib) return;
        query = reinterpret_cast<Query>(dlsym(lib, "XTestQueryExtension"));
        key = reinterpret_cast<FakeKey>(dlsym(lib, "XTestFakeKeyEvent"));
        button = reinterpret_cast<FakeButton>(dlsym(lib, "XTestFakeButtonEvent"));
        motion = reinterpret_cast<FakeMotion>(dlsym(lib, "XTestFakeMotionEvent"));
        relative = reinterpret_cast<FakeRelative>(dlsym(lib, "XTestFakeRelativeMotionEvent"));
    }
    bool complete() const { return query && key && button && motion && relative; }
};
KeySym keySym(int vk) {
    if (vk >= 0x41 && vk <= 0x5a) return XK_a + (vk - 0x41);
    if (vk >= 0x30 && vk <= 0x39) return XK_0 + (vk - 0x30);
    if (vk >= 0x70 && vk <= 0x87) return XK_F1 + (vk - 0x70);
    if (vk >= 0x60 && vk <= 0x69) return XK_KP_0 + (vk - 0x60);
    switch (vk) {
    case 0x08: return XK_BackSpace;
    case 0x09: return XK_Tab;
    case 0x0d: return XK_Return;
    case 0x10: return XK_Shift_L;
    case 0x11: return XK_Control_L;
    case 0x12: return XK_Alt_L;
    case 0x13: return XK_Pause;
    case 0x14: return XK_Caps_Lock;
    case 0x1b: return XK_Escape;
    case 0x20: return XK_space;
    case 0x21: return XK_Page_Up;
    case 0x22: return XK_Page_Down;
    case 0x23: return XK_End;
    case 0x24: return XK_Home;
    case 0x25: return XK_Left;
    case 0x26: return XK_Up;
    case 0x27: return XK_Right;
    case 0x28: return XK_Down;
    case 0x2d: return XK_Insert;
    case 0x2e: return XK_Delete;
    case 0x5b: return XK_Super_L;
    case 0x5c: return XK_Super_R;
    case 0x6a: return XK_KP_Multiply;
    case 0x6b: return XK_KP_Add;
    case 0x6d: return XK_KP_Subtract;
    case 0x6e: return XK_KP_Decimal;
    case 0x6f: return XK_KP_Divide;
    case 0x90: return XK_Num_Lock;
    case 0x91: return XK_Scroll_Lock;
    case 0xa0: return XK_Shift_L;
    case 0xa1: return XK_Shift_R;
    case 0xa2: return XK_Control_L;
    case 0xa3: return XK_Control_R;
    case 0xa4: return XK_Alt_L;
    case 0xa5: return XK_Alt_R;
    case 0xba: return XK_semicolon;
    case 0xbb: return XK_equal;
    case 0xbc: return XK_comma;
    case 0xbd: return XK_minus;
    case 0xbe: return XK_period;
    case 0xbf: return XK_slash;
    case 0xc0: return XK_grave;
    case 0xdb: return XK_bracketleft;
    case 0xdc: return XK_backslash;
    case 0xdd: return XK_bracketright;
    case 0xde: return XK_apostrophe;
    default: return NoSymbol;
    }
}
unsigned int buttonNumber(Button b) {
    switch (b) {
    case Button::Left: return 1;
    case Button::Middle: return 2;
    case Button::Right: return 3;
    case Button::X1: return 8;
    case Button::X2: return 9;
    }
    return 0;
}
// Protect grab error handling from the read-only window adapter's process-wide handler.
thread_local bool grabFailed = false;
int grabError(Display*, XErrorEvent* event) {
    if (event->error_code == BadAccess || event->error_code == BadValue) grabFailed = true;
    else grabFailed = true;
    return 0;
}
} // namespace

X11InputBackend::X11InputBackend(std::shared_ptr<std::atomic_bool> emergency,
                                 std::optional<platform::WindowTarget> target)
    : emergency_(std::move(emergency)), defaultTarget_(std::move(target)), target_(defaultTarget_) {
    platform::x11::ensureThreadSafety();
    if (!emergency_) return;
    display_ = XOpenDisplay(nullptr);
    if (!display_) return;
    xtst_ = dlopen("libXtst.so.6", RTLD_NOW | RTLD_LOCAL);
    const XTest test(xtst_);
    int eventBase{}, errorBase{}, major{}, minor{};
    if (!test.complete() || !test.query(display_, &eventBase, &errorBase, &major, &minor)) {
        if (xtst_) dlclose(xtst_);
        xtst_ = nullptr;
        XCloseDisplay(display_);
        display_ = nullptr;
    }
}
X11InputBackend::~X11InputBackend() {
    if (display_) XCloseDisplay(display_);
    if (xtst_) dlclose(xtst_);
}
bool X11InputBackend::ready() const { return display_ && xtst_ && emergency_ && !emergency_->load(); }
bool X11InputBackend::boundToWindow() const { return target_.has_value(); }
std::optional<PointerPosition> X11InputBackend::cursorPosition() {
    if (!ready()) return {};
    for (int s = 0; s < ScreenCount(display_); ++s) {
        Window root{}, child{};
        int rx{}, ry{}, wx{}, wy{};
        unsigned int mask{};
        if (XQueryPointer(display_, RootWindow(display_, s), &root, &child,
            &rx, &ry, &wx, &wy, &mask)) return PointerPosition{rx, ry};
    }
    return {};
}
std::optional<PointerPosition> X11InputBackend::clientToScreen(int x, int y) {
    if (!ready() || !target_) return {};
    int sx{}, sy{};
    if (platform::resolveClientPoint(*target_, x, y, sx, sy) != platform::TargetCheck::Ready) return {};
    return PointerPosition{sx, sy};
}
bool X11InputBackend::prepareStepWindow(const StepWindow& step, std::stop_token stop) {
    if (stop.stop_requested() || !ready()) return false;
    target_ = defaultTarget_;
    if (!step.enforce || step.title.empty())
        return !target_ || platform::checkTarget(*target_) == platform::TargetCheck::Ready;
    std::wstring title, process;
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> codec;
        title = codec.from_bytes(step.title);
        process = codec.from_bytes(step.process);
    } catch (...) { return false; } // malformed persisted UTF-8: fail closed
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(step.waitMs);
    do {
        if (stop.stop_requested() || emergency_->load()) return false;
        auto candidate = platform::findWindow(title, process);
        if (candidate) {
            const bool focus = step.activate ? platform::activateWindow(*candidate)
                                             : platform::checkTarget(*candidate) == platform::TargetCheck::Ready;
            if (focus && platform::checkTarget(*candidate) == platform::TargetCheck::Ready) {
                target_ = std::move(candidate);
                return true;
            }
        }
        if (std::chrono::steady_clock::now() >= until) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    } while (true);
    return false;
}
bool X11InputBackend::dispatch(const Event& event) {
    if (!ready()) return false;
    if (target_) {
        if (platform::checkTarget(*target_) != platform::TargetCheck::Ready) return false;
        if (event.kind == EventKind::ButtonPress || event.kind == EventKind::WheelTurn ||
            event.kind == EventKind::PointerMove) {
            const auto current = cursorPosition();
            if (!current) return false;
            const auto x = event.kind == EventKind::PointerMove
                ? (event.absolute ? std::int64_t{event.x} : std::int64_t{current->x} + event.x)
                : std::int64_t{current->x};
            const auto y = event.kind == EventKind::PointerMove
                ? (event.absolute ? std::int64_t{event.y} : std::int64_t{current->y} + event.y)
                : std::int64_t{current->y};
            if (x < INT_MIN || x > INT_MAX || y < INT_MIN || y > INT_MAX ||
                platform::checkScreenPoint(*target_, static_cast<int>(x), static_cast<int>(y))
                    != platform::TargetCheck::Ready) return false;
        }
    }
    return send(event);
}
bool X11InputBackend::releaseOnCleanup(const Event& event) {
    // Stop/focus loss must never prevent releasing held inputs.
    if (event.kind != EventKind::ButtonRelease && event.kind != EventKind::KeyRelease &&
        event.kind != EventKind::UnicodeRelease) return false;
    return send(event);
}
bool X11InputBackend::key(int vk, bool press) {
    if (vk == 0x7b) return false; // F12 is exclusively reserved for emergency Stop
    const auto xtest = XTest(xtst_);
    auto existing = keysHeld_.find(vk);
    if (!press && existing == keysHeld_.end()) return false;
    KeyCode code = press ? XKeysymToKeycode(display_, keySym(vk)) : existing->second;
    if (!code) return false;
    if (!xtest.key(display_, code, press ? True : False, 0)) return false;
    if (press) keysHeld_[vk] = code;
    else keysHeld_.erase(existing);
    return true;
}
bool X11InputBackend::unicode(char16_t unit, bool press) {
    // XTEST cannot enter arbitrary Unicode absent an appropriate XKB layout.
    // No clipboard overwrite, XKB map mutation or unverified Ctrl+Shift+U hack.
    const auto xtest = XTest(xtst_);
    if (!press) {
        auto it = unicodeHeld_.find(unit);
        if (it == unicodeHeld_.end()) return false;
        const auto [code, shift] = it->second;
        const bool ok = xtest.key(display_, code, False, 0) != 0;
        if (shift) xtest.key(display_, shift, False, 0);
        unicodeHeld_.erase(it);
        return ok;
    }
    if (unit == 0 || (unit >= 0xD800 && unit <= 0xDFFF) || unit == 0x7F) return false;
    const KeySym sym = unit <= 0xFF ? static_cast<KeySym>(unit)
                                        : static_cast<KeySym>(0x01000000u | unit);
    KeyCode code = 0;
    unsigned int shift = 0;
    // Restrict to group 0, level 0/1; do not silently type a different character
    // when the target application's keyboard layout differs.
    int low = 0, high = 0;
    XDisplayKeycodes(display_, &low, &high);
    for (int i = low; i <= high && !code; ++i) {
        if (XkbKeycodeToKeysym(display_, static_cast<KeyCode>(i), 0, 0) == sym) code = static_cast<KeyCode>(i);
        else if (XkbKeycodeToKeysym(display_, static_cast<KeyCode>(i), 0, 1) == sym) {
            code = static_cast<KeyCode>(i);
            shift = XKeysymToKeycode(display_, XK_Shift_L);
            if (!shift) return false;
        }
    }
    if (!code) return false;
    if (shift && !xtest.key(display_, shift, True, 0)) return false;
    if (!xtest.key(display_, code, True, 0)) {
        if (shift) xtest.key(display_, shift, False, 0);
        return false;
    }
    unicodeHeld_[unit] = {code, shift};
    return true;
}
bool X11InputBackend::send(const Event& event) {
    if (!display_ || !xtst_) return false;
    const XTest xtest(xtst_);
    bool ok = false;
    switch (event.kind) {
    case EventKind::ButtonPress:
    case EventKind::ButtonRelease: {
        const auto b = buttonNumber(event.button);
        ok = b && xtest.button(display_, b, event.kind == EventKind::ButtonPress, 0);
        break;
    }
    case EventKind::WheelTurn: {
        if (!event.delta || event.delta % 120 || event.delta < -1200 || event.delta > 1200) return false;
        const unsigned int b = event.axis == Axis::Vertical
            ? (event.delta > 0 ? 4u : 5u) : (event.delta > 0 ? 7u : 6u);
        ok = true;
        for (int i = 0; i < std::abs(event.delta) / 120; ++i)
            ok = xtest.button(display_, b, True, 0) && xtest.button(display_, b, False, 0) && ok;
        break;
    }
    case EventKind::PointerMove: {
        if (event.absolute) {
            ok = false;
            for (int s = 0; s < ScreenCount(display_); ++s) {
                if (event.x >= 0 && event.y >= 0 &&
                    event.x < DisplayWidth(display_, s) && event.y < DisplayHeight(display_, s)) {
                    ok = xtest.motion(display_, s, event.x, event.y, 0) != 0;
                    break;
                }
            }
        } else ok = xtest.relative(display_, event.x, event.y, 0) != 0;
        break;
    }
    case EventKind::KeyPress: ok = key(event.virtualKey, true); break;
    case EventKind::KeyRelease: ok = key(event.virtualKey, false); break;
    case EventKind::UnicodePress: ok = unicode(event.unicodeUnit, true); break;
    case EventKind::UnicodeRelease: ok = unicode(event.unicodeUnit, false); break;
    }
    if (ok) XFlush(display_);
    return ok;
}

X11EmergencyStop::~X11EmergencyStop() { stop(); }
bool X11EmergencyStop::start(std::function<void()> callback) {
    stop();
    platform::x11::ensureThreadSafety();
    triggered_->store(false);
    if (!callback) return false;
    // XWayland cannot reliably grab keys outside its own X11 clients.
    // Only a genuine X11 desktop is eligible for the global emergency stop.
    if (const char* session = std::getenv("XDG_SESSION_TYPE")) {
        if (std::string(session) == "wayland") return false;
    }
    Display* candidate = XOpenDisplay(nullptr);
    if (!candidate) return false;
    const KeyCode code = XKeysymToKeycode(candidate, XK_F12);
    if (!code) { XCloseDisplay(candidate); return false; }
    bool okay = true;
    {
        std::lock_guard lock(platform::x11::errorHandlerMutex());
        grabFailed = false;
        auto* previous = XSetErrorHandler(grabError);
        // AnyModifier also intercepts CapsLock/NumLock-modified F12. If any
        // F12 combination is owned by another app, X11 refuses the grab and
        // we refuse to run automation (rather than losing emergency Stop).
        for (int s = 0; s < ScreenCount(candidate); ++s)
            XGrabKey(candidate, code, AnyModifier, RootWindow(candidate, s), False, GrabModeAsync, GrabModeAsync);
        XSync(candidate, False);
        okay = !grabFailed;
        XSetErrorHandler(previous);
    }
    if (!okay) {
        for (int s = 0; s < ScreenCount(candidate); ++s)
            XUngrabKey(candidate, code, AnyModifier, RootWindow(candidate, s));
        XCloseDisplay(candidate);
        return false;
    }
    display_ = candidate;
    keycode_ = code;
    onStop_ = std::move(callback);
    active_.store(true);
    watcher_ = std::jthread([this](std::stop_token stopToken) {
        while (!stopToken.stop_requested() && active_.load()) {
            pollfd fd{ConnectionNumber(display_), POLLIN, 0};
            const auto count = poll(&fd, 1, 50);
            if (count < 0) continue;
            if (count > 0 && (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                triggered_->store(true);
                if (onStop_) onStop_();
                break;
            }
            while (XPending(display_) > 0) {
                XEvent event{};
                XNextEvent(display_, &event);
                if (event.type == x11KeyPressEvent && event.xkey.keycode == keycode_) {
                    triggered_->store(true);
                    if (onStop_) onStop_();
                    return;
                }
            }
        }
    });
    return true;
}
void X11EmergencyStop::stop() {
    active_.store(false);
    if (watcher_.joinable()) { watcher_.request_stop(); watcher_.join(); }
    if (!display_) return;
    for (int s = 0; s < ScreenCount(display_); ++s)
        XUngrabKey(display_, keycode_, AnyModifier, RootWindow(display_, s));
    XFlush(display_);
    XCloseDisplay(display_);
    display_ = nullptr;
    keycode_ = 0;
    onStop_ = {};
}
} // namespace clicknova::input
