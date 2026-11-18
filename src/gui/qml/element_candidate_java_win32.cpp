// SPDX-License-Identifier: MIT
#include "element_candidate_java_win32.hpp"
#include "clicknova/java_preview_policy.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>

namespace clicknova::gui::qml {
namespace {
namespace ca = accessibility::candidates;
namespace jp = accessibility::java_preview;
// Narrow, 64-bit WindowsAccessBridge ABI. Layout corresponds to the published
// JDK AccessBridgePackages.h AccessibleContextInfo. No JDK SDK needed to build;
// no vendor header/file is redistributed. Validate against the installed JDK
// before deployment; static assertions reject unexpected wchar_t/ABI sizes.
static_assert(sizeof(void*) == 8, "Java Access Bridge adapter requires 64-bit Windows");
static_assert(sizeof(wchar_t) == 2, "Windows JAB uses UTF-16 wchar_t");
using JavaObject = std::int64_t; // OpenJDK JOBJECT64 in non-legacy mode
struct ContextInfo {
    wchar_t name[1024];
    wchar_t description[1024];
    wchar_t role[256];
    wchar_t role_en_US[256];
    wchar_t states[256];
    wchar_t states_en_US[256];
    std::int32_t indexInParent;
    std::int32_t childrenCount;
    std::int32_t x, y, width, height;
    BOOL accessibleComponent, accessibleAction, accessibleSelection;
    BOOL accessibleText, accessibleInterfaces;
};
static_assert(sizeof(ContextInfo) == 6188, "Unexpected AccessBridge ContextInfo ABI layout");
struct PrivateInfo {
    ContextInfo data{};
    ~PrivateInfo() { SecureZeroMemory(&data, sizeof(data)); }
    PrivateInfo(const PrivateInfo&) = delete;
    PrivateInfo& operator=(const PrivateInfo&) = delete;
    PrivateInfo() = default;
};

bool validWindow(ca::Target target) noexcept {
    const auto window = reinterpret_cast<HWND>(target.nativeHandle);
    if (!window || !target.processId || !IsWindow(window) || !IsWindowVisible(window) ||
        IsIconic(window) || GetAncestor(window, GA_ROOT) != window) return false;
    DWORD pid{};
    GetWindowThreadProcessId(window, &pid);
    return pid == target.processId;
}
std::wstring systemDllPath() {
    wchar_t system[MAX_PATH + 1]{};
    const auto n = GetSystemDirectoryW(system, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    return std::wstring(system, n) + L"\\WindowsAccessBridge-64.dll";
}
std::wstring javaHomeDllPath() {
    wchar_t home[32768]{};
    const auto n = GetEnvironmentVariableW(L"JAVA_HOME", home, 32768);
    if (n < 3 || n >= 32768 || home[1] != L':' ||
        (home[2] != L'\\' && home[2] != L'/')) return {};
    return std::wstring(home, n) + L"\\bin\\WindowsAccessBridge-64.dll";
}

// JAB uses hidden HWND messages to negotiate its Java VM bridge. The worker
// has no Qt event loop, so service only its own Win32 queue for a short,
// bounded startup window and between synchronous JAB API calls. A blocking
// DLL call is still bounded by the *parent process* timeout, not this pump.
void pumpBridgeMessages() noexcept {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message != WM_QUIT) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}
struct Bridge final {
    using Run = void (*)();
    using IsJavaWindow = BOOL (*)(HWND);
    using GetRoot = BOOL (*)(HWND, long*, JavaObject*);
    using GetInfo = BOOL (*)(long, JavaObject, ContextInfo*);
    using GetChild = JavaObject (*)(long, JavaObject, int);
    using Release = void (*)(long, JavaObject);
    HMODULE library{};
    Run run{};
    IsJavaWindow isJavaWindow{};
    GetRoot root{};
    GetInfo info{};
    GetChild child{};
    Release release{};
    Bridge() {
        // Explicit full paths only, never PATH or current-directory DLL search.
        for (const auto& path : {javaHomeDllPath(), systemDllPath()}) {
            if (path.empty()) continue;
            library = LoadLibraryExW(path.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (library) break;
        }
        if (!library) return;
        run = reinterpret_cast<Run>(GetProcAddress(library, "Windows_run"));
        isJavaWindow = reinterpret_cast<IsJavaWindow>(GetProcAddress(library, "isJavaWindow"));
        root = reinterpret_cast<GetRoot>(GetProcAddress(library, "getAccessibleContextFromHWND"));
        info = reinterpret_cast<GetInfo>(GetProcAddress(library, "getAccessibleContextInfo"));
        child = reinterpret_cast<GetChild>(GetProcAddress(library, "getAccessibleChildFromContext"));
        release = reinterpret_cast<Release>(GetProcAddress(library, "releaseJavaObject"));
        if (!ready()) {
            FreeLibrary(library);library=nullptr;return;
        }
        // JAB initialization creates a window for interprocess bridge traffic.
        // All calls remain inside the short-lived isolated worker.
        run();
        // Let the short-lived bridge receive initial JVM presence messages.
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(150);
        do {
            pumpBridgeMessages();
            if (std::chrono::steady_clock::now() >= deadline) break;
            Sleep(10);
        } while (true);
    }
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    ~Bridge() { if (library) FreeLibrary(library); }
    bool ready() const noexcept {
        return library && run && isJavaWindow && root && info && child && release;
    }
};
struct ContextRef {
    Bridge* bridge{};
    long vmId{};
    JavaObject value{};
    unsigned depth{};
    ContextRef(Bridge* b, long vm, JavaObject v, unsigned d): bridge(b), vmId(vm), value(v), depth(d) {}
    ContextRef(const ContextRef&) = delete;
    ContextRef& operator=(const ContextRef&) = delete;
    ContextRef(ContextRef&& other) noexcept:
        bridge(other.bridge), vmId(other.vmId), value(std::exchange(other.value, 0)), depth(other.depth) {}
    ContextRef& operator=(ContextRef&&) = delete;
    ~ContextRef() { if (value && bridge && bridge->release) bridge->release(vmId, value); }
};
// Reject unterminated or maliciously oversized metadata. Never copy names or
// descriptions; only role_en_US and states_en_US are inspected transiently.
std::wstring_view shortString(const wchar_t (&input)[256]) noexcept {
    for (std::size_t i = 0; i < 256; ++i)
        if (!input[i]) return {input, i};
    return {};
}
class JavaProvider final : public ca::IProvider {
public:
    ca::Result enumerate(ca::Target target, ca::Limits limits, std::stop_token stop) override {
        if (!ca::validLimits(limits)) return {ca::Status::Failed, {}, 0, false};
        if (!validWindow(target)) return {ca::Status::UnsafeTarget, {}, 0, false};
        Bridge bridge;
        if (!bridge.ready()) return {ca::Status::Unavailable, {}, 0, false};
        const auto hwnd = reinterpret_cast<HWND>(target.nativeHandle);
        if (!bridge.isJavaWindow(hwnd)) return {ca::Status::Unavailable, {}, 0, false};
        long vmId{};
        JavaObject root{};
        if (!bridge.root(hwnd, &vmId, &root) || !root) {
            if (root) bridge.release(vmId, root); // defensive partial API failure
            return {ca::Status::Unavailable, {}, 0, false};
        }
        ca::Result found;
        found.status = ca::Status::Complete;
        const auto started = std::chrono::steady_clock::now();
        const auto elapsed = [&] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-started).count();
        };
        std::deque<ContextRef> todo;
        todo.emplace_back(&bridge, vmId, root, 0);
        // One context per JVM is released via releaseJavaObject even when an
        // error, cancellation or deadline interrupts traversal.
        while (!todo.empty()) {
            pumpBridgeMessages();
            if (stop.stop_requested()) { found.status = ca::Status::Cancelled; break; }
            if (!validWindow(target)) { found.status = ca::Status::UnsafeTarget; break; }
            if (elapsed() >= limits.maxElapsedMs || found.visited >= limits.maxVisited ||
                found.items.size() >= limits.maxCandidates) {found.truncated = true; break;}
            ContextRef node(std::move(todo.front()));
            todo.pop_front();
            ++found.visited;
            PrivateInfo item;
            if (!bridge.info(node.vmId, node.value, &item.data)) {
                found.status = ca::Status::Failed; break;
            }
            const auto role = shortString(item.data.role_en_US);
            const auto states = shortString(item.data.states_en_US);
            const auto type = jp::genericRole(role);
            // Exclude password text entirely, not merely its label/value.
            if (type < 0) continue;
            if (node.depth > 0) {
                found.items.push_back(jp::preview(role, states));
            }
            const int children = item.data.childrenCount;
            if (children < 0) { found.status = ca::Status::Failed; break; }
            if (children > 0 && node.depth >= limits.maxDepth) {
                found.truncated = true; break;
            }
            if (children > static_cast<int>(limits.maxVisited)) {
                found.truncated = true; break;
            }
            // Avoid pinning arbitrarily many JAB handles in memory.
            for (int i = 0; i < children; ++i) {
                if (stop.stop_requested()) {found.status = ca::Status::Cancelled; break;}
                if (!validWindow(target)) {found.status = ca::Status::UnsafeTarget; break;}
                if (elapsed() >= limits.maxElapsedMs ||
                    found.visited + todo.size() >= limits.maxVisited ||
                    found.items.size() + todo.size() >= limits.maxCandidates) {
                    found.truncated = true;break;
                }
                const JavaObject c = bridge.child(node.vmId, node.value, i);
                if (!c) {found.status = ca::Status::Failed; break;}
                todo.emplace_back(&bridge, node.vmId, c, node.depth+1);
            }
            if (found.status != ca::Status::Complete || found.truncated) break;
        }
        if (stop.stop_requested()) found.status = ca::Status::Cancelled;
        if (!validWindow(target)) found.status = ca::Status::UnsafeTarget;
        // Destroy the JAB references before unloading its DLL.
        todo.clear();
        return ca::normalize(std::move(found), limits);
    }
    ca::VerifyResult verify(ca::Target, const ca::Candidate&, std::stop_token) override {
        return {ca::VerifyStatus::Unbindable, {}};
    }
};
} // namespace
std::shared_ptr<accessibility::candidates::IProvider> createNativeJavaCandidateProvider() {
    return std::make_shared<JavaProvider>();
}
} // namespace clicknova::gui::qml
#else
namespace clicknova::gui::qml {
std::shared_ptr<accessibility::candidates::IProvider> createNativeJavaCandidateProvider() { return {}; }
}
#endif
