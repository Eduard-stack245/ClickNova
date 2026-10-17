// SPDX-License-Identifier: Apache-2.0
#include "native_accessibility.hpp"

#include <algorithm>
#include <cwctype>
#include <functional>
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
#include <oleacc.h>
#include <oleauto.h>
#include <wrl/client.h>
#endif

namespace clicknova::accessibility::native {
namespace {
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
struct Apartment {
    HRESULT result{CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)};
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool ok() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};
bool validTarget(const clicknova::platform::WindowTarget& window) {
    const auto hwnd = reinterpret_cast<HWND>(window.handle);
    DWORD pid = 0;
    return hwnd && IsWindow(hwnd) && IsWindowVisible(hwnd) &&
        GetWindowThreadProcessId(hwnd, &pid) && pid && pid == window.processId &&
        pid != GetCurrentProcessId();
}
VARIANT self() { VARIANT v; VariantInit(&v); v.vt = VT_I4; v.lVal = CHILDID_SELF; return v; }
std::wstring nameOf(IAccessible* object, VARIANT id) {
    BSTR text = nullptr;
    if (FAILED(object->get_accName(id, &text)) || !text) return {};
    std::wstring name(text, SysStringLen(text));
    SysFreeString(text);
    return name;
}
Role classify(LONG role) {
    switch (role) {
    case ROLE_SYSTEM_PUSHBUTTON: return Role::Button;
    case ROLE_SYSTEM_TEXT: return Role::Edit;
    case ROLE_SYSTEM_CHECKBUTTON: return Role::CheckBox;
    case ROLE_SYSTEM_COMBOBOX: return Role::ComboBox;
    case ROLE_SYSTEM_LINK: return Role::Link;
    case ROLE_SYSTEM_STATICTEXT: return Role::Text;
    case ROLE_SYSTEM_MENUITEM: return Role::MenuItem;
    case ROLE_SYSTEM_RADIOBUTTON: return Role::RadioButton;
    case ROLE_SYSTEM_LISTITEM: return Role::ListItem;
    case ROLE_SYSTEM_OUTLINEITEM: return Role::TreeItem;
    case ROLE_SYSTEM_SLIDER: return Role::Slider;
    case ROLE_SYSTEM_SPINBUTTON: return Role::Spinner;
    default: return Role::Any;
    }
}
Role roleOf(IAccessible* object, VARIANT id, std::wstring* localized = nullptr) {
    VARIANT value; VariantInit(&value);
    const bool ok = SUCCEEDED(object->get_accRole(id, &value));
    Role role = Role::Any;
    if (ok && value.vt == VT_I4) {
        role = classify(value.lVal);
        if (localized) {
            wchar_t buf[128]{};
            if (GetRoleTextW(value.lVal, buf, 128)) *localized = buf;
        }
    } else if (ok && value.vt == VT_BSTR && localized && value.bstrVal)
        *localized = value.bstrVal;
    VariantClear(&value);
    return role;
}
bool enabledOf(IAccessible* object, VARIANT id) {
    VARIANT state; VariantInit(&state);
    const bool ok = SUCCEEDED(object->get_accState(id, &state)) && state.vt == VT_I4 &&
        !(state.lVal & (STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_INVISIBLE));
    VariantClear(&state);
    return ok;
}
std::wstring fold(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
bool matches(const Element& item, const Query& query) {
    return (query.role == Role::Any || query.role == item.role) &&
        (query.text.empty() || fold(item.name).find(fold(query.text)) != std::wstring::npos ||
         fold(item.automationId).find(fold(query.text)) != std::wstring::npos) &&
        (query.includeAnonymous || !query.text.empty() || !item.name.empty() || item.role != Role::Any);
}
struct Node {
    ComPtr<IAccessible> object;
    LONG id{CHILDID_SELF};
    std::wstring path;
    int depth{0};
};
// Each 'd' index denotes an IDispatch child in AccessibleChildren; an 'i'
// suffix denotes the terminal VARIANT child ID on its parent. All indices
// are re-resolved at playback and checked against name and role.
std::vector<Node> children(const Node& node, bool& truncated) {
    std::vector<Node> result;
    if (node.id != CHILDID_SELF || node.depth >= 20) return result;
    LONG total = 0;
    if (FAILED(node.object->get_accChildCount(&total)) || total <= 0) return result;
    const LONG count = std::min(total, 512L);
    if (total > count) truncated = true;
    std::vector<VARIANT> variants(static_cast<std::size_t>(count));
    for (auto& v : variants) VariantInit(&v);
    LONG fetched = 0;
    const HRESULT hr = AccessibleChildren(node.object.Get(), 0, count, variants.data(), &fetched);
    if (SUCCEEDED(hr)) {
        for (LONG i = 0; i < std::min(fetched, count); ++i) {
            auto& child = variants[static_cast<std::size_t>(i)];
            Node next;
            next.depth = node.depth + 1;
            if (child.vt == VT_DISPATCH && child.pdispVal &&
                SUCCEEDED(child.pdispVal->QueryInterface(IID_PPV_ARGS(next.object.GetAddressOf())))) {
                next.id = CHILDID_SELF;
                next.path = node.path + L"/d" + std::to_wstring(i);
            } else if (child.vt == VT_I4 && child.lVal > 0) {
                next.object = node.object;
                next.id = child.lVal;
                next.path = node.path + L"/i" + std::to_wstring(child.lVal);
            }
            if (next.object) result.push_back(std::move(next));
        }
    }
    for (auto& v : variants) VariantClear(&v);
    return result;
}
ComPtr<IAccessible> rootFor(const clicknova::platform::WindowTarget& window) {
    ComPtr<IAccessible> root;
    if (!validTarget(window)) return root;
    AccessibleObjectFromWindow(reinterpret_cast<HWND>(window.handle), static_cast<DWORD>(OBJID_CLIENT),
        IID_PPV_ARGS(root.GetAddressOf()));
    return root;
}
bool resolve(IAccessible* root, const std::wstring& path, Node& result) {
    if (path != L"msaa" && !path.starts_with(L"msaa/")) return false;
    result.object = root;
    result.id = CHILDID_SELF;
    result.path = L"msaa";
    std::size_t pos = 4;
    for (int depth = 0; pos < path.size() && depth < 20; ++depth) {
        if (path[pos] != L'/') return false;
        ++pos;
        if (pos >= path.size()) return false;
        const wchar_t kind = path[pos++];
        if (kind != L'd' && kind != L'i') return false;
        const auto end = path.find(L'/', pos);
        const auto length = (end == std::wstring::npos ? path.size() : end) - pos;
        if (!length || length > 5) return false;
        unsigned value = 0;
        for (std::size_t i = pos; i < pos + length; ++i) {
            if (path[i] < L'0' || path[i] > L'9') return false;
            value = value * 10 + (path[i] - L'0');
            if (value > 65535) return false;
        }
        pos += length;
        bool truncated = false;
        const auto nodes = children(result, truncated);
        const std::wstring wanted = result.path + L"/" + kind + std::to_wstring(value);
        const auto it = std::find_if(nodes.begin(), nodes.end(), [&](const Node& n) { return n.path == wanted; });
        if (it == nodes.end()) return false;
        result = *it;
        if (result.id != CHILDID_SELF && pos != path.size()) return false;
    }
    return pos == path.size();
}
#endif
} // namespace


std::optional<ScreenPoint> pointMsaa(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& path, Role role) {
#ifdef _WIN32
    if (!validTarget(window) || clicknova::platform::checkTarget(window) != clicknova::platform::TargetCheck::Ready)
        return std::nullopt;
    Apartment apartment;
    if (!apartment.ok()) return std::nullopt;
    auto root = rootFor(window);
    Node node;
    if (!root || !resolve(root.Get(), path, node)) return std::nullopt;
    VARIANT id = self(); id.lVal = node.id;
    if ((!name.empty() && nameOf(node.object.Get(), id) != name) ||
        (role != Role::Any && roleOf(node.object.Get(), id) != role) ||
        !enabledOf(node.object.Get(), id)) return std::nullopt;
    LONG x=0, y=0, w=0, h=0;
    if (FAILED(node.object->accLocation(&x, &y, &w, &h, id)) || w<=0 || h<=0)
        return std::nullopt;
    return ScreenPoint{x + w/2, y + h/2};
#else
    (void)window; (void)name; (void)path; (void)role;
    return std::nullopt;
#endif
}

Result searchMsaa(const clicknova::platform::WindowTarget& window, const Query& query) {
#ifdef _WIN32
    if (!validTarget(window)) return {{}, "Invalid or closed window."};
    Apartment apartment;
    if (!apartment.ok()) return {{}, "MSAA COM unavailable."};
    auto root = rootFor(window);
    if (!root) return {{}, "MSAA tree is unavailable."};
    Result result;
    std::vector<Node> pending{{root, CHILDID_SELF, L"msaa", 0}};
    std::size_t visited = 0;
    while (!pending.empty() && visited < 10000 && result.elements.size() < query.limit) {
        Node node = std::move(pending.back());
        pending.pop_back();
        ++visited;
        VARIANT id = self(); id.lVal = node.id;
        Element item;
        item.provider = Provider::Msaa;
        item.name = nameOf(node.object.Get(), id);
        item.automationId = node.path;
        item.role = roleOf(node.object.Get(), id, &item.localizedRole);
        item.enabled = enabledOf(node.object.Get(), id);
        if (item.name.size() <= 512 && matches(item, query))
            result.elements.push_back(std::move(item));
        if (node.depth < 20 && node.id == CHILDID_SELF) {
            bool cut = false;
            auto next = children(node, cut);
            result.truncated |= cut;
            for (auto it = next.rbegin(); it != next.rend(); ++it) {
                if (pending.size() >= 2500) { result.truncated = true; break; }
                pending.push_back(std::move(*it));
            }
        }
    }
    if (!pending.empty() || visited >= 2500) result.truncated = true;
    return result;
#else
    (void)window; (void)query;
    return {{}, "MSAA is supported only on Windows."};
#endif
}

bool performMsaa(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& path, Role role,
    Operation operation, const std::wstring& value, bool allowBackground) {
#ifdef _WIN32
    if (!validTarget(window) || !supportsProvider(operation, role, Provider::Msaa) ||
        (allowBackground ? clicknova::platform::checkAccessibleTarget(window) :
          clicknova::platform::checkTarget(window)) != clicknova::platform::TargetCheck::Ready) return false;
    Apartment apartment;
    if (!apartment.ok()) return false;
    auto root = rootFor(window);
    if (!root) return false;
    Node node;
    if (!resolve(root.Get(), path, node)) return false;
    VARIANT id = self(); id.lVal = node.id;
    if ((!name.empty() && nameOf(node.object.Get(), id) != name) ||
        (role != Role::Any && roleOf(node.object.Get(), id) != role) ||
        !enabledOf(node.object.Get(), id) ||
        (allowBackground ? clicknova::platform::checkAccessibleTarget(window) :
          clicknova::platform::checkTarget(window)) != clicknova::platform::TargetCheck::Ready) return false;
    switch (operation) {
    case Operation::Invoke: return SUCCEEDED(node.object->accDoDefaultAction(id));
    case Operation::SetValue: {
        // accValue is a provider-defined property; only edit/combobox and
        // never password inputs (STATE_SYSTEM_PROTECTED).
        VARIANT flags; VariantInit(&flags);
        bool protectedText = true;
        if (SUCCEEDED(node.object->get_accState(id, &flags)) && flags.vt == VT_I4)
            protectedText = (flags.lVal & STATE_SYSTEM_PROTECTED) != 0;
        VariantClear(&flags);
        if (protectedText) return false;
        BSTR bstr = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
        if (!bstr) return false;
        const HRESULT hr = node.object->put_accValue(id, bstr);
        SysFreeString(bstr);
        return SUCCEEDED(hr);
    }
    case Operation::Focus:
        return SUCCEEDED(node.object->accSelect(SELFLAG_TAKEFOCUS, id));
    case Operation::Select:
        return SUCCEEDED(node.object->accSelect(SELFLAG_TAKESELECTION, id));
    default: return false;
    }
#else
    (void)window; (void)name; (void)path; (void)role; (void)operation; (void)value; (void)allowBackground;
    return false;
#endif
}
} // namespace clicknova::accessibility::native
