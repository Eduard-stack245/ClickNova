// SPDX-License-Identifier: Apache-2.0
// Real native X11 integration test. Runs with a second-process window in Xvfb.
#include "clicknova/window_target.hpp"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr char title[] = "ClickNova X11 isolated test target";
void setWindowProperties(Display* display, Window w, unsigned long process) {
    const Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    const Atom name = XInternAtom(display, "_NET_WM_NAME", False);
    const Atom pid = XInternAtom(display, "_NET_WM_PID", False);
    XChangeProperty(display, w, name, utf8, 8, PropModeReplace,
        reinterpret_cast<const unsigned char*>(title), sizeof(title) - 1);
    XChangeProperty(display, w, pid, XA_CARDINAL, 32, PropModeReplace,
        reinterpret_cast<const unsigned char*>(&process), 1);
}
}
int main(int argc, char** argv) {
    using namespace clicknova::platform;
    if (argc > 1 && std::string(argv[1]) == "--headless") {
        // Even with X11 compiled in, no X server must never become a fake target.
        unsetenv("DISPLAY");
        assert(listWindows().empty());
        assert(!foregroundWindow());
        assert(!findWindow(L"target", L""));
        assert(!windowUnderCursor());
        WindowTarget fake{42, 123, L"fake", L"test"};
        assert(checkTarget(fake) == TargetCheck::Unsupported);
        assert(checkAccessibleTarget(fake) == TargetCheck::Unsupported);
        assert(checkScreenPoint(fake, 0, 0) == TargetCheck::Unsupported);
        assert(!activateWindow(fake));
        return 0;
    }
    int childReady[2]{}, childExit[2]{};
    assert(pipe(childReady) == 0 && pipe(childExit) == 0);
    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(childReady[0]); close(childExit[1]);
        Display* display = XOpenDisplay(nullptr);
        if (!display) _exit(11);
        Window target = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                            50, 50, 320, 240, 0, 0, 0);
        setWindowProperties(display, target, static_cast<unsigned long>(getpid()));
        XMapWindow(display, target);
        XSync(display, False);
        const std::uint64_t id = static_cast<std::uint64_t>(target);
        if (write(childReady[1], &id, sizeof(id)) != sizeof(id)) _exit(12);
        char signal = 0;
        if (read(childExit[0], &signal, 1) != 1) _exit(13);
        XDestroyWindow(display, target);
        XCloseDisplay(display);
        _exit(0);
    }
    close(childReady[1]); close(childExit[0]);
    std::uint64_t id{};
    assert(read(childReady[0], &id, sizeof(id)) == sizeof(id));
    Display* display = XOpenDisplay(nullptr);
    assert(display != nullptr);
    const Window remote = static_cast<Window>(id);

    // The child is a real window belonging to another process.
    auto entries = listWindows();
    auto found = findWindow(std::wstring(title, title + sizeof(title)-1), L"");
    assert(found && found->handle == id && found->processId == static_cast<std::uint32_t>(child));
    assert(checkAccessibleTarget(*found) == TargetCheck::Ready);
    assert(findWindow(found->title, found->process).has_value());
    assert(checkCapturePoint(*found, 100, 100) == TargetCheck::Ready);
    assert(checkCapturePoint(*found, 9999, 9999) == TargetCheck::OutOfBounds);

    XSetInputFocus(display, remote, RevertToPointerRoot, CurrentTime);
    XSync(display, False);
    assert(checkTarget(*found) == TargetCheck::Ready);
    assert(foregroundWindow() && foregroundWindow()->handle == id);
    assert(checkScreenPoint(*found, 100, 100) == TargetCheck::Ready);
    assert(checkScreenPoint(*found, 9999, 9999) == TargetCheck::OutOfBounds);
    int x = 0, y = 0;
    assert(resolveClientPoint(*found, 20, 40, x, y) == TargetCheck::Ready);
    assert(x == 70 && y == 90);
    XWarpPointer(display, None, remote, 0, 0, 0, 0, 20, 40);
    XSync(display, False);
    assert(windowUnderCursor() && windowUnderCursor()->handle == id);

    WindowTarget wrongPid = *found;
    wrongPid.processId += 1;
    assert(checkTarget(wrongPid) == TargetCheck::Missing);
    assert(!activateWindow(wrongPid));
    WindowTarget wrongTitle = *found;
    wrongTitle.title += L" - changed";
    assert(checkAccessibleTarget(wrongTitle) == TargetCheck::Missing);
    assert(!findWindow(L"Not a real window", L"").has_value());

    // ClickNova's own windows may never be selected as targets.
    const Window ours = XCreateSimpleWindow(display, DefaultRootWindow(display),
                                            600, 450, 150, 100, 0, 0, 0);
    setWindowProperties(display, ours, static_cast<unsigned long>(getpid()));
    XMapWindow(display, ours);
    XSync(display, False);
    for (const auto& window : listWindows())
        assert(window.handle != static_cast<std::uintptr_t>(ours));
    XDestroyWindow(display, ours);
    XCloseDisplay(display);

    assert(write(childExit[1], "x", 1) == 1);
    close(childExit[1]); close(childReady[0]);
    int status = 0;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}
