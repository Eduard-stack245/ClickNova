// SPDX-License-Identifier: Apache-2.0
// Read-only X11 window discovery/geometry, plus explicit EWMH activation.
// Does NOT synthesize input. Wayland/native portals require separate backends.
#include "clicknova/window_target.hpp"
#include "x11_error_sync.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <locale>
#include <codecvt>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace clicknova::platform::x11 {
std::mutex& errorHandlerMutex() { static std::mutex mutex; return mutex; }
void ensureThreadSafety() { static std::once_flag once; std::call_once(once, [] { XInitThreads(); }); }
}

namespace clicknova::platform {
namespace {
// The Xlib error handler is process-global. Keep a short, serialized guard
// around each transaction and prevent BadWindow races from aborting ClickNova.
std::mutex& xMutex() { return x11::errorHandlerMutex(); }
int ignoreXError(Display*, XErrorEvent*) { return 0; }
struct Session {
    std::lock_guard<std::mutex> lock{xMutex()};
    Display* display{nullptr};
    int (*previous)(Display*, XErrorEvent*){nullptr};
    Session() { x11::ensureThreadSafety(); display = XOpenDisplay(nullptr); if (display) previous = XSetErrorHandler(ignoreXError); }
    ~Session() {
        if (display) {
            XSync(display, False);
            XSetErrorHandler(previous);
            XCloseDisplay(display);
        }
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
};

Atom atom(Display* d, const char* name) { return XInternAtom(d, name, True); }

std::vector<unsigned long> property(Display* d, Window w, Atom key, Atom kind = AnyPropertyType,
                                    long limit = 2048) {
    if (key == None) return {};
    Atom type = None;
    int format = 0;
    unsigned long count = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(d, w, key, 0, limit, False, kind,
        &type, &format, &count, &after, &data) != Success || !data) return {};
    std::vector<unsigned long> result;
    if (format == 32 && after == 0) {
        auto* values = reinterpret_cast<unsigned long*>(data);
        result.assign(values, values + count);
    }
    XFree(data);
    return result;
}

std::wstring toWide(const std::string& utf8) {
    try { return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(utf8); }
    catch (...) { return {}; } // malformed accessibility text is never trusted
}
std::wstring titleFor(Display* d, Window w) {
    const Atom nameAtom = atom(d, "_NET_WM_NAME");
    const Atom utf8 = atom(d, "UTF8_STRING");
    if (nameAtom != None && utf8 != None) {
        Atom type = None;
        int format = 0;
        unsigned long count = 0, remaining = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(d, w, nameAtom, 0, 1024, False, utf8,
            &type, &format, &count, &remaining, &data) == Success && data) {
            std::wstring value;
            if (format == 8 && remaining == 0 && count > 0 && count <= 4096)
                value = toWide({reinterpret_cast<const char*>(data), count});
            XFree(data);
            if (!value.empty()) return value;
        }
    }
    char* legacy = nullptr;
    if (XFetchName(d, w, &legacy) && legacy) {
        // ICCCM WM_NAME may use STRING (Latin-1). Restrict fallback to ASCII
        // rather than persisting a lossy/ambiguous non-UTF8 window selector.
        const std::string bytes{legacy};
        XFree(legacy);
        if (bytes.size() <= 4096 && std::all_of(bytes.begin(), bytes.end(), [](unsigned char c) { return c < 128; }))
            return std::wstring(bytes.begin(), bytes.end());
    }
    return {};
}

std::uint32_t pidFor(Display* d, Window w) {
    const auto pids = property(d, w, atom(d, "_NET_WM_PID"), XA_CARDINAL, 1);
    if (pids.size() != 1 || pids[0] == 0 || pids[0] > UINT32_MAX) return 0;
    return static_cast<std::uint32_t>(pids[0]);
}
std::wstring processFor(std::uint32_t pid) {
    if (!pid) return {};
    std::error_code error;
    const auto path = std::filesystem::read_symlink("/proc/" + std::to_string(pid) + "/exe", error);
    if (!error) return toWide(path.filename().string());
    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    if (std::getline(comm, name)) return toWide(name);
    return {};
}

bool viewable(Display* d, Window w) {
    XWindowAttributes attr{};
    return XGetWindowAttributes(d, w, &attr) && attr.map_state == IsViewable && attr.c_class == InputOutput;
}
bool minimized(Display* d, Window w) {
    const auto states = property(d, w, atom(d, "_NET_WM_STATE"), XA_ATOM, 64);
    const auto hidden = static_cast<unsigned long>(atom(d, "_NET_WM_STATE_HIDDEN"));
    return hidden != None && std::find(states.begin(), states.end(), hidden) != states.end();
}
std::optional<WindowTarget> identify(Display* d, Window w) {
    if (w == None || !viewable(d, w) || minimized(d, w)) return std::nullopt;
    const auto title = titleFor(d, w);
    if (title.empty()) return std::nullopt;
    const auto pid = pidFor(d, w);
    if (pid == static_cast<std::uint32_t>(getpid())) return std::nullopt;
    return WindowTarget{static_cast<std::uintptr_t>(w), pid, title, processFor(pid)};
}

std::vector<Window> windowsFor(Display* d) {
    std::vector<Window> result;
    for (int screen = 0; screen < ScreenCount(d); ++screen) {
        const Window root = RootWindow(d, screen);
        const auto clients = property(d, root, atom(d, "_NET_CLIENT_LIST"), XA_WINDOW, 4096);
        if (!clients.empty()) {
            for (auto item : clients) result.push_back(static_cast<Window>(item));
        } else {
            // A session without an EWMH window manager can still expose the
            // visible top-level windows. This is also useful for Xvfb tests.
            Window rootResult{}, parent{};
            Window* children = nullptr;
            unsigned int count = 0;
            if (XQueryTree(d, root, &rootResult, &parent, &children, &count)) {
                if (children && count <= 4096) result.insert(result.end(), children, children + count);
                if (children) XFree(children);
            }
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

Window activeWindow(Display* d) {
    for (int screen = 0; screen < ScreenCount(d); ++screen) {
        const auto root = RootWindow(d, screen);
        const auto active = property(d, root, atom(d, "_NET_ACTIVE_WINDOW"), XA_WINDOW, 1);
        if (active.size() == 1 && active.front() != None) return static_cast<Window>(active.front());
    }
    Window focused = None;
    int revert = RevertToNone;
    XGetInputFocus(d, &focused, &revert);
    if (focused == None || focused == PointerRoot) return None;
    // Focus often belongs to a descendant widget: return its outermost client.
    for (int i = 0; i < 64; ++i) {
        Window root{}, parent{};
        Window* children = nullptr;
        unsigned int count = 0;
        if (!XQueryTree(d, focused, &root, &parent, &children, &count)) return None;
        if (children) XFree(children);
        if (parent == None || parent == root) return focused;
        focused = parent;
    }
    return None;
}

bool sameIdentity(Display* d, const WindowTarget& target) {
    if (!target.handle || !target.processId || target.processId == static_cast<std::uint32_t>(getpid())) return false;
    const auto w = static_cast<Window>(target.handle);
    return pidFor(d, w) == target.processId && titleFor(d, w) == target.title;
}

bool pointInWindow(Display* d, Window w, int screenX, int screenY) {
    XWindowAttributes attr{};
    if (!XGetWindowAttributes(d, w, &attr)) return false;
    Window child = None;
    int originX = 0, originY = 0;
    if (!XTranslateCoordinates(d, w, attr.root, 0, 0, &originX, &originY, &child)) return false;
    const std::int64_t dx = static_cast<std::int64_t>(screenX) - originX;
    const std::int64_t dy = static_cast<std::int64_t>(screenY) - originY;
    return dx >= 0 && dy >= 0 && dx < attr.width && dy < attr.height;
}

bool unobscured(Display* d, Window w, int screenX, int screenY) {
    XWindowAttributes attr{};
    if (!XGetWindowAttributes(d, w, &attr)) return false;
    Window current = attr.root;
    // Follow the frontmost child *at the requested pixel*, descending through
    // possible WM frames. A different topmost child means the target is covered.
    for (int i = 0; i < 64; ++i) {
        Window child = None;
        int x = 0, y = 0;
        if (!XTranslateCoordinates(d, attr.root, current, screenX, screenY, &x, &y, &child) ||
            child == None || child == current) return false;
        if (child == w) return true;
        current = child;
    }
    return false;
}
TargetCheck accessible(Display* d, const WindowTarget& target) {
    if (!sameIdentity(d, target)) return TargetCheck::Missing;
    const auto w = static_cast<Window>(target.handle);
    if (minimized(d, w)) return TargetCheck::Minimized;
    if (!viewable(d, w)) return TargetCheck::Hidden;
    return TargetCheck::Ready;
}
TargetCheck foreground(Display* d, const WindowTarget& target) {
    const auto status = accessible(d, target);
    if (status != TargetCheck::Ready) return status;
    if (activeWindow(d) != static_cast<Window>(target.handle)) return TargetCheck::NotForeground;
    return TargetCheck::Ready;
}
}

std::vector<WindowTarget> listWindows() {
    Session session;
    std::vector<WindowTarget> matches;
    if (!session.display) return matches;
    for (auto window : windowsFor(session.display)) {
        if (auto candidate = identify(session.display, window)) matches.push_back(std::move(*candidate));
    }
    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        return a.title < b.title || (a.title == b.title && a.handle < b.handle);
    });
    return matches;
}
std::optional<WindowTarget> foregroundWindow() {
    Session session;
    if (!session.display) return std::nullopt;
    return identify(session.display, activeWindow(session.display));
}
std::optional<WindowTarget> findWindow(const std::wstring& title, const std::wstring& process) {
    if (title.empty()) return std::nullopt;
    std::optional<WindowTarget> unique;
    for (const auto& entry : listWindows()) {
        if (entry.title != title || (!process.empty() && entry.process != process)) continue;
        if (unique) return std::nullopt; // identical title/process: fail closed
        unique = entry;
    }
    return unique;
}
bool activateWindow(const WindowTarget& target) {
    Session session;
    if (!session.display || !sameIdentity(session.display, target)) return false;
    const auto key = atom(session.display, "_NET_ACTIVE_WINDOW");
    if (key == None) return false;
    XWindowAttributes attr{};
    const Window w = static_cast<Window>(target.handle);
    if (!viewable(session.display, w) || minimized(session.display, w) ||
        !XGetWindowAttributes(session.display, w, &attr)) return false;
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = w;
    event.xclient.message_type = key;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 1; // source application
    event.xclient.data.l[1] = CurrentTime;
    if (!XSendEvent(session.display, attr.root, False,
        SubstructureRedirectMask | SubstructureNotifyMask, &event)) return false;
    XFlush(session.display);
    // A WM is permitted to deny focus activation. Never report success solely
    // because the request was sent.
    for (int attempt = 0; attempt < 5; ++attempt) {
        XSync(session.display, False);
        if (activeWindow(session.display) == w) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return false;
}
std::optional<WindowTarget> windowUnderCursor() {
    Session session;
    if (!session.display) return std::nullopt;
    for (int s = 0; s < ScreenCount(session.display); ++s) {
        Window root = RootWindow(session.display, s), child = None;
        Window outRoot{}, outChild{};
        int rx = 0, ry = 0, wx = 0, wy = 0;
        unsigned int modifiers = 0;
        if (!XQueryPointer(session.display, root, &outRoot, &outChild,
            &rx, &ry, &wx, &wy, &modifiers) || outChild == None) continue;
        child = outChild;
        // If a WM reparented the window, use the EWMH-listed client inside
        // its frame at the pointer, never guess a different window by title.
        if (auto target = identify(session.display, child)) return target;
        for (const auto& w : windowsFor(session.display)) {
            if (pointInWindow(session.display, w, rx, ry) && unobscured(session.display, w, rx, ry))
                if (auto target = identify(session.display, w)) return target;
        }
    }
    return std::nullopt;
}
TargetCheck checkAccessibleTarget(const WindowTarget& target) {
    Session session;
    return session.display ? accessible(session.display, target) : TargetCheck::Unsupported;
}
TargetCheck checkTarget(const WindowTarget& target) {
    Session session;
    return session.display ? foreground(session.display, target) : TargetCheck::Unsupported;
}
TargetCheck checkScreenPoint(const WindowTarget& target, int x, int y) {
    Session session;
    if (!session.display) return TargetCheck::Unsupported;
    const auto status = foreground(session.display, target);
    if (status != TargetCheck::Ready) return status;
    const Window w = static_cast<Window>(target.handle);
    if (!pointInWindow(session.display, w, x, y)) return TargetCheck::OutOfBounds;
    return unobscured(session.display, w, x, y) ? TargetCheck::Ready : TargetCheck::Obscured;
}
TargetCheck checkCapturePoint(const WindowTarget& target, int x, int y) {
    Session session;
    if (!session.display) return TargetCheck::Unsupported;
    const auto status = accessible(session.display, target);
    if (status != TargetCheck::Ready) return status;
    return pointInWindow(session.display, static_cast<Window>(target.handle), x, y)
        ? TargetCheck::Ready : TargetCheck::OutOfBounds;
}
TargetCheck resolveClientPoint(const WindowTarget& target, int cx, int cy, int& sx, int& sy) {
    Session session;
    if (!session.display) return TargetCheck::Unsupported;
    const auto status = foreground(session.display, target);
    if (status != TargetCheck::Ready) return status;
    const Window w = static_cast<Window>(target.handle);
    XWindowAttributes attr{};
    if (!XGetWindowAttributes(session.display, w, &attr)) return TargetCheck::Missing;
    if (cx < 0 || cy < 0 || cx >= attr.width || cy >= attr.height) return TargetCheck::OutOfBounds;
    Window child = None;
    if (!XTranslateCoordinates(session.display, w, attr.root, cx, cy, &sx, &sy, &child))
        return TargetCheck::Missing;
    if (!unobscured(session.display, w, sx, sy)) return TargetCheck::Obscured;
    return TargetCheck::Ready;
}
} // namespace clicknova::platform
