// SPDX-License-Identifier: Apache-2.0
#include "clicknova/x11_input_backend.hpp"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

namespace { constexpr int x11KeyPressEvent = KeyPress; constexpr int x11ButtonPressEvent = ButtonPress; }
#undef KeyPress
#undef KeyRelease
#undef ButtonPress
#undef ButtonRelease
#undef Status
using namespace clicknova::input;
namespace {
int failures{};
void check(bool good, const char* label) {
    if (!good) { std::cerr << "x11_input_tests: " << label << '\n'; ++failures; }
}
}
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--headless") {
        unsetenv("DISPLAY");
        auto pressed = std::make_shared<std::atomic_bool>(false);
        X11EmergencyStop stop;
        check(!stop.start([]{}), "headless emergency must refuse");
        X11InputBackend backend(pressed);
        check(!backend.ready(), "headless injection must refuse");
        return failures ? 1 : 0;
    }
    Display* d = XOpenDisplay(nullptr);
    if (!d) { std::cerr << "X11 server missing\n"; return 1; }
    Window root = DefaultRootWindow(d);
    Window window = XCreateSimpleWindow(d, root, 10, 10, 350, 260, 0, 0, 0xffffff);
    XSelectInput(d, window, KeyPressMask | KeyReleaseMask | ButtonPressMask |
        ButtonReleaseMask | PointerMotionMask);
    XMapWindow(d, window);
    XSync(d, False);
    XSetInputFocus(d, window, RevertToPointerRoot, CurrentTime);
    XSync(d, False);
    std::atomic<int> fired{0};
    std::stop_source cancellation;
    X11EmergencyStop stop;
    check(stop.start([&] { ++fired; cancellation.request_stop(); }), "global F12 grab");
    if (!stop.active()) { XDestroyWindow(d, window); XCloseDisplay(d); return 1; }
    // A second grab must fail; otherwise emergency stop could be silently lost.
    X11EmergencyStop second;
    check(!second.start([]{}), "global F12 must be exclusive");
    X11InputBackend backend(stop.triggered());
    check(backend.ready(), "XTEST runtime available");
    // A forged window identity must never be accepted as a target.
    clicknova::platform::WindowTarget forged{
        static_cast<std::uintptr_t>(window), 1234567, L"fake", L""};
    X11InputBackend protectedBackend(stop.triggered(), forged);
    Event blocked; blocked.kind = EventKind::ButtonPress;
    check(!protectedBackend.dispatch(blocked), "foreign PID cannot be targeted");
    if (backend.ready()) {
        Event move; move.kind = EventKind::PointerMove;
        move.absolute = true; move.x = 40; move.y = 40;
        check(backend.dispatch(move), "absolute pointer motion");
        Event relative = move; relative.absolute = false; relative.x = 3; relative.y = 4;
        check(backend.dispatch(relative), "relative pointer motion");
        Event click; click.kind = EventKind::ButtonPress;
        check(backend.dispatch(click), "left button down");
        click.kind = EventKind::ButtonRelease;
        check(backend.dispatch(click), "left button up");
        Event wheel; wheel.kind = EventKind::WheelTurn; wheel.delta = 120;
        check(backend.dispatch(wheel), "vertical wheel");
        wheel.axis = Axis::Horizontal;
        check(backend.dispatch(wheel), "horizontal wheel");
        wheel.delta = 90;
        check(!backend.dispatch(wheel), "nonrepresentable wheel must fail");
        Event key; key.kind = EventKind::KeyPress; key.virtualKey = 0x41;
        check(backend.dispatch(key), "keyboard A down");
        key.kind = EventKind::KeyRelease;
        check(backend.dispatch(key), "keyboard A up");
        key.virtualKey = 0x7b; key.kind = EventKind::KeyPress;
        check(!backend.dispatch(key), "reserved F12 must not be synthesized");
        Event text; text.kind = EventKind::UnicodePress; text.unicodeUnit = u'a';
        check(backend.dispatch(text), "unicode ASCII press");
        text.kind = EventKind::UnicodeRelease;
        check(backend.dispatch(text), "unicode ASCII release");
        // The target must receive real X11 events (rather than backend no-ops).
        XSync(d, False);
        int mouse{}, keyboard{}, scroll{};
        // XTEST dispatch is asynchronous across two X11 connections. A
        // single XPending check occasionally races ahead of key delivery on
        // Xvfb; poll the real event queue for a bounded time instead.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline &&
               (mouse < 3 || scroll < 2 || keyboard < 2)) {
            if (!XPending(d)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            XEvent event{}; XNextEvent(d, &event);
            if (event.type == x11ButtonPressEvent) { ++mouse; if (event.xbutton.button >= 4) ++scroll; }
            if (event.type == x11KeyPressEvent) ++keyboard;
        }
        check(mouse >= 3, "X11 target received click and scrolling");
        check(scroll >= 2, "X11 target received wheel events");
        check(keyboard >= 2, "X11 target received key events");
        // Exercise the real plan executor through the native backend, not only
        // single dispatched events (including held key/button cleanup).
        Plan smokePlan;
        smokePlan.actions = {MovePointer{MoveMode::Absolute, 45, 45, 0},
                             Click{Button::Right, 1, 10}, KeyTap{0x41}};
        SteadyWaiter waiter;
        const auto execution = play(smokePlan, backend, waiter);
        check(execution.status == Status::Completed && execution.actionsCompleted == 3,
              "actual playback engine uses X11 input driver");
        // Awaiting a long-running step must also be cancellable by F12.
        Plan waitingPlan;
        waitingPlan.actions.emplace_back(Wait{5000});
        Result cancelled{};
        std::thread waiting([&] {
            cancelled = play(waitingPlan, backend, waiter, cancellation.get_token());
        });
        void* lib = dlopen("libXtst.so.6", RTLD_NOW | RTLD_LOCAL);
        using FakeKey = int(*)(Display*, unsigned int, Bool, unsigned long);
        auto fakeKey = lib ? reinterpret_cast<FakeKey>(dlsym(lib, "XTestFakeKeyEvent")) : nullptr;
        if (fakeKey) {
            auto f12 = XKeysymToKeycode(d, XK_F12);
            check(fakeKey(d, f12, True, 0) != 0, "send physical-style F12 down");
            check(fakeKey(d, f12, False, 0) != 0, "send physical-style F12 up");
            XFlush(d);
            for (int i = 0; i < 120 && !stop.triggered()->load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(stop.triggered()->load() && fired.load() == 1, "F12 triggers independent stop");
            cancellation.request_stop(); // avoid a hanging test even if emergency failed
            waiting.join();
            check(cancelled.status == Status::Cancelled,
                  "F12 cancels the real action engine while waiting");
            Event clickAfter; clickAfter.kind = EventKind::ButtonPress;
            check(!backend.dispatch(clickAfter), "no injection after emergency");
            clickAfter.kind = EventKind::ButtonRelease;
            check(backend.releaseOnCleanup(clickAfter), "cleanup release bypasses stop");
        } else {
            cancellation.request_stop();
            waiting.join();
            check(false, "cannot load XTEST for emergency exercise");
        }
        if (lib) dlclose(lib);
    }
    stop.stop();
    XDestroyWindow(d, window);
    XCloseDisplay(d);
    return failures ? 1 : 0;
}
