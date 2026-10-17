// SPDX-License-Identifier: Apache-2.0
#include "native_accessibility.hpp"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace clicknova::accessibility::native {
namespace {
#ifdef _WIN32
// Compatible with the public Java Access Bridge x64 data ABI defined in
// OpenJDK AccessBridgePackages.h. No JDK headers, import library or Java
// installation is required to compile ClickNova; the bridge is optional.
// Windows x64: JavaVMID is 'long' (32-bit), Java object handles are 64-bit.
using JavaObject = std::int64_t;
struct ContextInfo {
    wchar_t name[1024]{};
    wchar_t description[1024]{};
    wchar_t role[256]{};
    wchar_t roleEnglish[256]{};
    wchar_t states[256]{};
    wchar_t statesEnglish[256]{};
    LONG indexInParent{};
    LONG childrenCount{};
    LONG x{}, y{}, width{}, height{};
    BOOL component{}, action{}, selection{}, text{}, interfaces{};
};
struct ActionInfo { wchar_t name[256]{}; };
struct Actions { LONG count{}; ActionInfo items[256]{}; };
struct ActionsToDo { LONG count{}; ActionInfo items[32]{}; };
static_assert(sizeof(wchar_t) == 2, "JAB requires Windows UTF-16 wchar_t");
static_assert(sizeof(ContextInfo) == 6188, "Java Access Bridge context ABI mismatch");

bool validTarget(const clicknova::platform::WindowTarget& window) {
    const auto hwnd = reinterpret_cast<HWND>(window.handle);
    DWORD pid = 0;
    return hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd) &&
        GetWindowThreadProcessId(hwnd, &pid) && pid && pid == window.processId &&
        pid != GetCurrentProcessId();
}
std::wstring bounded(const wchar_t* value, std::size_t size) {
    std::size_t length = 0;
    while (length < size && value[length]) ++length;
    return std::wstring(value, length);
}
std::wstring fold(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}
// The role_en_US field is language-independent even when the JVM UI is Russian.
Role classify(const ContextInfo& info) {
    const auto role = fold(bounded(info.roleEnglish, 256));
    if (role == L"push button" || role == L"button") return Role::Button;
    if (role == L"text" || role == L"password text") return Role::Edit;
    if (role == L"check box") return Role::CheckBox;
    if (role == L"combo box") return Role::ComboBox;
    if (role == L"hyperlink" || role == L"link") return Role::Link;
    if (role == L"label") return Role::Text;
    if (role == L"menu item") return Role::MenuItem;
    if (role == L"radio button") return Role::RadioButton;
    if (role == L"list item") return Role::ListItem;
    if (role == L"tree item") return Role::TreeItem;
    if (role == L"slider") return Role::Slider;
    if (role == L"spin box") return Role::Spinner;
    return Role::Any;
}
bool stateEnabled(const ContextInfo& info) {
    const auto state = L"," + fold(bounded(info.statesEnglish, 256)) + L",";
    // All action entry points fail closed when provider doesn't report enabled.
    return state.find(L"enabled") != std::wstring::npos &&
        state.find(L"disabled") == std::wstring::npos;
}
struct Bridge {
    using Start = void (*)();
    using IsJava = BOOL (*)(HWND);
    using ContextFromWindow = BOOL (*)(HWND, LONG*, JavaObject*);
    using Info = BOOL (*)(LONG, JavaObject, ContextInfo*);
    using Child = JavaObject (*)(LONG, JavaObject, LONG);
    using Release = void (*)(LONG, JavaObject);
    using GetActions = BOOL (*)(LONG, JavaObject, Actions*);
    using DoActions = BOOL (*)(LONG, JavaObject, ActionsToDo*, LONG*);
    using Text = BOOL (*)(LONG, JavaObject, const wchar_t*);
    using Focus = BOOL (*)(LONG, JavaObject);
    HMODULE dll{};
    Start start{};
    IsJava isJava{};
    ContextFromWindow context{};
    Info info{};
    Child child{};
    Release release{};
    GetActions actions{};
    DoActions doActions{};
    Text setText{};
    Focus focus{};
    Bridge() {
        // Avoid loading a DLL from the working directory. Standard installed
        // bridge: system32; alternate: the JDK/JRE explicitly configured by
        // JAVA_HOME. Cross-bitness is not supported by this 64-bit backend.
        dll = LoadLibraryExW(L"WindowsAccessBridge-64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll) {
            wchar_t home[32768]{};
            DWORD length = GetEnvironmentVariableW(L"JAVA_HOME", home, 32768);
            if (!length) length = GetEnvironmentVariableW(L"JDK_HOME", home, 32768);
            if (length > 0 && length < 32768) {
                std::wstring path(home, length);
                if (!path.empty() && path.back() != L'\\') path += L'\\';
                path += L"bin\\WindowsAccessBridge-64.dll";
                dll = LoadLibraryExW(path.c_str(), nullptr,
                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            }
        }
        if (!dll) return;
        start = symbol<Start>("Windows_run");
        isJava = symbol<IsJava>("isJavaWindow");
        context = symbol<ContextFromWindow>("getAccessibleContextFromHWND");
        info = symbol<Info>("getAccessibleContextInfo");
        child = symbol<Child>("getAccessibleChildFromContext");
        release = symbol<Release>("releaseJavaObject");
        actions = symbol<GetActions>("getAccessibleActions");
        doActions = symbol<DoActions>("doAccessibleActions");
        setText = symbol<Text>("setTextContents");
        focus = symbol<Focus>("requestFocus");
        if (usable()) start();
    }
    ~Bridge() { if (dll) FreeLibrary(dll); }
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    template<typename T> T symbol(const char* name) { return reinterpret_cast<T>(GetProcAddress(dll, name)); }
    bool usable() const {
        return start && isJava && context && info && child && release;
    }
};
struct Handle {
    Bridge* bridge{};
    LONG vm{};
    JavaObject object{};
    Handle() = default;
    Handle(Bridge& b, LONG v, JavaObject o) : bridge(&b), vm(v), object(o) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : bridge(other.bridge), vm(other.vm), object(other.object) {
        other.object = 0;
    }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { reset(); bridge = other.bridge; vm = other.vm;
            object = other.object; other.object = 0; }
        return *this;
    }
    void reset() { if (object && bridge) bridge->release(vm, object); object = 0; }
};
Handle rootFor(Bridge& bridge, const clicknova::platform::WindowTarget& window) {
    LONG vm = 0;
    JavaObject root = 0;
    const HWND hwnd = reinterpret_cast<HWND>(window.handle);
    if (!validTarget(window) || !bridge.usable() || !bridge.isJava(hwnd) ||
        !bridge.context(hwnd, &vm, &root) || !root) return {};
    return Handle(bridge, vm, root);
}
struct Node {
    Handle handle;
    std::wstring path;
    int depth{};
};
bool matches(const Element& item, const Query& query) {
    return (query.role == Role::Any || query.role == item.role) &&
        (query.text.empty() || fold(item.name).find(fold(query.text)) != std::wstring::npos ||
         fold(item.automationId).find(fold(query.text)) != std::wstring::npos) &&
        (query.includeAnonymous || !query.text.empty() || !item.name.empty() || item.role != Role::Any);
}
// A Java selector is an index-in-parent path (not a remote object handle).
// It is re-resolved and checked against name/role before performing an action.
bool pathSteps(const std::wstring& path, std::vector<LONG>& steps) {
    if (!path.starts_with(L"jab") || (path.size() > 3 && path[3] != L'/')) return false;
    std::size_t at = 3;
    while (at < path.size()) {
        if (path[at++] != L'/') return false;
        LONG index = 0;
        const auto start = at;
        while (at < path.size() && path[at] >= L'0' && path[at] <= L'9') {
            index = index * 10 + path[at++] - L'0';
            if (index > 127) return false;
        }
        if (at == start || steps.size() >= 12) return false;
        steps.push_back(index);
    }
    return true;
}
#endif
} // namespace

bool isJavaWindow(const clicknova::platform::WindowTarget& window) {
#ifdef _WIN32
    if (!validTarget(window)) return false;
    Bridge bridge;
    return bridge.usable() && bridge.isJava(reinterpret_cast<HWND>(window.handle)) != FALSE;
#else
    (void)window;
    return false;
#endif
}

std::optional<ScreenPoint> pointJava(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& path, Role role) {
#ifdef _WIN32
    if (!validTarget(window) || clicknova::platform::checkTarget(window) != clicknova::platform::TargetCheck::Ready)
        return std::nullopt;
    std::vector<LONG> indices;
    if (!pathSteps(path, indices)) return std::nullopt;
    Bridge bridge;
    if (!bridge.usable()) return std::nullopt;
    Handle node = rootFor(bridge, window);
    if (!node.object) return std::nullopt;
    for (LONG index : indices) {
        ContextInfo info{};
        if (!bridge.info(node.vm, node.object, &info) || index >= info.childrenCount)
            return std::nullopt;
        JavaObject child = bridge.child(node.vm, node.object, index);
        if (!child) return std::nullopt;
        node = Handle(bridge, node.vm, child);
    }
    ContextInfo info{};
    if (!bridge.info(node.vm, node.object, &info) ||
        (!name.empty() && bounded(info.name, 1024) != name) ||
        (role != Role::Any && classify(info) != role) ||
        !stateEnabled(info) || info.width <= 0 || info.height <= 0) return std::nullopt;
    return ScreenPoint{info.x + info.width/2, info.y + info.height/2};
#else
    (void)window; (void)name; (void)path; (void)role;
    return std::nullopt;
#endif
}

Result searchJava(const clicknova::platform::WindowTarget& window, const Query& query) {
#ifdef _WIN32
    if (!validTarget(window)) return {{}, "Invalid or closed window."};
    Bridge bridge;
    if (!bridge.usable()) return {{}, "Java Access Bridge is not installed or enabled."};
    Handle root = rootFor(bridge, window);
    if (!root.object) return {{}, "This window has no accessible Java context."};
    Result result;
    std::vector<Node> pending;
    pending.push_back({std::move(root), L"jab", 0});
    std::size_t visited = 0;
    while (!pending.empty() && visited < 2500 && result.elements.size() < query.limit) {
        Node current = std::move(pending.back());
        pending.pop_back();
        ++visited;
        ContextInfo info{};
        if (!bridge.info(current.handle.vm, current.handle.object, &info)) continue;
        Element item;
        item.provider = Provider::Java;
        item.name = bounded(info.name, 1024);
        item.automationId = current.path;
        item.localizedRole = bounded(info.role, 256);
        item.role = classify(info);
        item.enabled = stateEnabled(info);
        if (item.name.size() <= 512 && matches(item, query))
            result.elements.push_back(std::move(item));
        if (current.depth < 12 && info.childrenCount > 0) {
            const LONG children = std::min(info.childrenCount, 128L);
            if (info.childrenCount > children) result.truncated = true;
            for (LONG i = children; i-- > 0;) {
                if (pending.size() >= 2500) { result.truncated = true; break; }
                JavaObject child = bridge.child(current.handle.vm, current.handle.object, i);
                if (!child) continue;
                pending.push_back({Handle(bridge, current.handle.vm, child),
                    current.path + L"/" + std::to_wstring(i), current.depth + 1});
            }
        }
    }
    if (!pending.empty() || visited >= 2500) result.truncated = true;
    return result;
#else
    (void)window; (void)query;
    return {{}, "Java Access Bridge is supported only on Windows."};
#endif
}

bool performJava(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& path, Role role,
    Operation operation, const std::wstring& value, bool allowBackground) {
#ifdef _WIN32
    if (!validTarget(window) || !supportsProvider(operation, role, Provider::Java) ||
        (allowBackground ? clicknova::platform::checkAccessibleTarget(window) :
          clicknova::platform::checkTarget(window)) != clicknova::platform::TargetCheck::Ready) return false;
    std::vector<LONG> indices;
    if (!pathSteps(path, indices)) return false;
    Bridge bridge;
    if (!bridge.usable()) return false;
    Handle node = rootFor(bridge, window);
    if (!node.object) return false;
    for (const LONG index : indices) {
        ContextInfo parent{};
        if (!bridge.info(node.vm, node.object, &parent) || index >= parent.childrenCount) return false;
        JavaObject child = bridge.child(node.vm, node.object, index);
        if (!child) return false;
        node = Handle(bridge, node.vm, child);
    }
    ContextInfo info{};
    if (!bridge.info(node.vm, node.object, &info) ||
        (!name.empty() && bounded(info.name, 1024) != name) ||
        (role != Role::Any && classify(info) != role) || !stateEnabled(info) ||
        (allowBackground ? clicknova::platform::checkAccessibleTarget(window) :
          clicknova::platform::checkTarget(window)) != clicknova::platform::TargetCheck::Ready) return false;
    if (operation == Operation::Focus) return bridge.focus && bridge.focus(node.vm, node.object) != FALSE;
    if (operation == Operation::SetValue) {
        if (!bridge.setText || value.size() >= 1024 ||
            fold(bounded(info.roleEnglish, 256)) == L"password text") return false;
        return bridge.setText(node.vm, node.object, value.c_str()) != FALSE;
    }
    if (operation == Operation::Invoke) {
        if (!bridge.actions || !bridge.doActions) return false;
        Actions available{};
        if (!bridge.actions(node.vm, node.object, &available) || available.count < 1 ||
            available.count > 256) return false;
        std::wstring selected;
        if (available.count == 1) selected = bounded(available.items[0].name, 256);
        else {
            for (LONG i = 0; i < available.count; ++i) {
                const auto action = bounded(available.items[i].name, 256);
                if (fold(action) == L"click" || fold(action) == L"press") {
                    if (!selected.empty()) return false;
                    selected = action;
                }
            }
        }
        if (selected.empty() || selected.size() >= 256) return false;
        ActionsToDo toDo{};
        toDo.count = 1;
        std::copy(selected.begin(), selected.end(), toDo.items[0].name);
        LONG failed = -1;
        return bridge.doActions(node.vm, node.object, &toDo, &failed) != FALSE;
    }
    return false;
#else
    (void)window; (void)name; (void)path; (void)role; (void)operation; (void)value; (void)allowBackground;
    return false;
#endif
}
} // namespace clicknova::accessibility::native
