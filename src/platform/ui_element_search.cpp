// SPDX-License-Identifier: Apache-2.0
#include "clicknova/ui_element_search.hpp"
#include "native_accessibility.hpp"
#include "clicknova/visual_recognition.hpp"

#include <algorithm>
#include <cwctype>
#include <charconv>
#include <cmath>
#include <string_view>
#include <unordered_set>
#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
// WIN32_LEAN_AND_MEAN omits the COM headers that define the `interface`
// macro used by the Windows SDK UIAutomationCore.h forward declarations.
// Initialize the COM declarations before including the umbrella UIA header.
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#endif

namespace clicknova::accessibility {
namespace {
std::wstring lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}
bool includes(const std::wstring& haystack, const std::wstring& needle) {
    return needle.empty() || lower(haystack).find(lower(needle)) != std::wstring::npos;
}
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
struct ComApartment {
    HRESULT result{CoInitializeEx(nullptr, COINIT_MULTITHREADED)};
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool usable() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
};
std::wstring getString(IUIAutomationElement* element,
                       HRESULT (STDMETHODCALLTYPE IUIAutomationElement::*getter)(BSTR*)) {
    BSTR value = nullptr;
    if (FAILED((element->*getter)(&value)) || !value) return {};
    std::wstring result(value, SysStringLen(value));
    SysFreeString(value);
    return result;
}
Role classify(CONTROLTYPEID id) {
    switch (id) {
    case UIA_ButtonControlTypeId: return Role::Button;
    case UIA_EditControlTypeId: return Role::Edit;
    case UIA_CheckBoxControlTypeId: return Role::CheckBox;
    case UIA_ComboBoxControlTypeId: return Role::ComboBox;
    case UIA_HyperlinkControlTypeId: return Role::Link;
    case UIA_TextControlTypeId: return Role::Text;
    case UIA_MenuItemControlTypeId: return Role::MenuItem;
    case UIA_RadioButtonControlTypeId: return Role::RadioButton;
    case UIA_ListItemControlTypeId: return Role::ListItem;
    case UIA_TreeItemControlTypeId: return Role::TreeItem;
    case UIA_SliderControlTypeId: return Role::Slider;
    case UIA_SpinnerControlTypeId: return Role::Spinner;
    default: return Role::Any;
    }
}
CONTROLTYPEID controlType(Role role) {
    switch (role) {
    case Role::Button: return UIA_ButtonControlTypeId;
    case Role::Edit: return UIA_EditControlTypeId;
    case Role::CheckBox: return UIA_CheckBoxControlTypeId;
    case Role::ComboBox: return UIA_ComboBoxControlTypeId;
    case Role::Link: return UIA_HyperlinkControlTypeId;
    case Role::Text: return UIA_TextControlTypeId;
    case Role::MenuItem: return UIA_MenuItemControlTypeId;
    case Role::RadioButton: return UIA_RadioButtonControlTypeId;
    case Role::ListItem: return UIA_ListItemControlTypeId;
    case Role::TreeItem: return UIA_TreeItemControlTypeId;
    case Role::Slider: return UIA_SliderControlTypeId;
    case Role::Spinner: return UIA_SpinnerControlTypeId;
    default: return 0;
    }
}
// A browser may have an arbitrary executable name. For unknown applications,
// recognize a real browser surface only when it exposes a URL/address control
// as well as selectable UIA tabs. A generic TabItem (e.g. an editor) alone
// is NOT sufficient evidence that it is a web browser.
bool hasBrowserAddressBar(IUIAutomation* automation, IUIAutomationElement* root) {
    VARIANT edit{}; edit.vt = VT_I4; edit.lVal = UIA_EditControlTypeId;
    VARIANT combo{}; combo.vt = VT_I4; combo.lVal = UIA_ComboBoxControlTypeId;
    ComPtr<IUIAutomationCondition> editCondition, comboCondition, either;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, edit,
            editCondition.GetAddressOf())) ||
        FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, combo,
            comboCondition.GetAddressOf())) ||
        FAILED(automation->CreateOrCondition(editCondition.Get(), comboCondition.Get(),
            either.GetAddressOf()))) return false;
    ComPtr<IUIAutomationElementArray> fields;
    if (FAILED(root->FindAll(TreeScope_Descendants, either.Get(), fields.GetAddressOf())) ||
        !fields) return false;
    int length = 0;
    if (FAILED(fields->get_Length(&length))) return false;
    for (int i = 0; i < std::min(length, 80); ++i) {
        ComPtr<IUIAutomationElement> field;
        if (FAILED(fields->GetElement(i, field.GetAddressOf())) || !field) continue;
        const auto name = lower(getString(field.Get(), &IUIAutomationElement::get_CurrentName));
        const auto id = lower(getString(field.Get(), &IUIAutomationElement::get_CurrentAutomationId));
        // UIA names are localized; use control IDs as well as explicit
        // multi-language labels, but do not treat a generic search box as an
        // address bar unless its current value is a web URL.
        const bool identifiable =
            id.find(L"omnibox") != std::wstring::npos ||
            id.find(L"urlbar") != std::wstring::npos ||
            id.find(L"addressbar") != std::wstring::npos ||
            id.find(L"locationbar") != std::wstring::npos ||
            name.find(L"address and search") != std::wstring::npos ||
            name.find(L"search or enter address") != std::wstring::npos ||
            name.find(L"address bar") != std::wstring::npos ||
            name.find(L"адресная строка") != std::wstring::npos ||
            name.find(L"adressleiste") != std::wstring::npos;
        if (identifiable) return true;
        // Do not retain, log or transmit the address/value: we only inspect
        // the beginning of a control already advertised as URL/location.
        const bool likelyLocation = id.find(L"url") != std::wstring::npos ||
            id.find(L"location") != std::wstring::npos ||
            name.find(L"url") != std::wstring::npos ||
            name.find(L"location") != std::wstring::npos;
        if (!likelyLocation) continue;
        ComPtr<IUIAutomationValuePattern> value;
        if (FAILED(field->GetCurrentPatternAs(UIA_ValuePatternId,
                IID_PPV_ARGS(value.GetAddressOf()))) || !value) continue;
        BSTR raw = nullptr;
        if (SUCCEEDED(value->get_CurrentValue(&raw)) && raw) {
            const std::wstring_view current(raw, SysStringLen(raw));
            const bool web = current.starts_with(L"https://") || current.starts_with(L"http://");
            SysFreeString(raw);
            if (web) return true;
        } else if (raw) SysFreeString(raw);
    }
    return false;
}
// Address-bar values are available only for the currently selected tab. Do
// not guess URLs for inactive tabs or switch tabs merely to fetch icons.
std::wstring activeAddressUrl(IUIAutomation* automation, IUIAutomationElement* root) {
    VARIANT edit{}; edit.vt = VT_I4; edit.lVal = UIA_EditControlTypeId;
    VARIANT combo{}; combo.vt = VT_I4; combo.lVal = UIA_ComboBoxControlTypeId;
    ComPtr<IUIAutomationCondition> e, c, both;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, edit,
            e.GetAddressOf())) ||
        FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, combo,
            c.GetAddressOf())) ||
        FAILED(automation->CreateOrCondition(e.Get(), c.Get(), both.GetAddressOf()))) return {};
    ComPtr<IUIAutomationElementArray> fields;
    if (FAILED(root->FindAll(TreeScope_Descendants, both.Get(),
            fields.GetAddressOf())) || !fields) return {};
    int count = 0;
    if (FAILED(fields->get_Length(&count))) return {};
    for (int i = 0; i < std::min(count, 100); ++i) {
        ComPtr<IUIAutomationElement> field;
        if (FAILED(fields->GetElement(i, field.GetAddressOf())) || !field) continue;
        const auto id = lower(getString(field.Get(), &IUIAutomationElement::get_CurrentAutomationId));
        const auto name = lower(getString(field.Get(), &IUIAutomationElement::get_CurrentName));
        const bool address = id.find(L"omnibox") != std::wstring::npos ||
            id.find(L"urlbar") != std::wstring::npos ||
            id.find(L"addressbar") != std::wstring::npos ||
            id.find(L"locationbar") != std::wstring::npos ||
            name.find(L"address and search") != std::wstring::npos ||
            name.find(L"search or enter address") != std::wstring::npos ||
            name.find(L"address bar") != std::wstring::npos ||
            name.find(L"адресная строка") != std::wstring::npos;
        if (!address) continue;
        ComPtr<IUIAutomationValuePattern> pattern;
        if (FAILED(field->GetCurrentPatternAs(UIA_ValuePatternId,
                IID_PPV_ARGS(pattern.GetAddressOf()))) || !pattern) continue;
        BSTR value = nullptr;
        if (FAILED(pattern->get_CurrentValue(&value)) || !value) continue;
        std::wstring url(value, SysStringLen(value));
        SysFreeString(value);
        if (url.size() > 4096 || url.find(L'\0') != std::wstring::npos) continue;
        if (url.starts_with(L"https://") || url.starts_with(L"http://")) return url;
    }
    return {};
}
#endif
} // namespace

const char* providerName(Provider provider) {
    switch (provider) {
    case Provider::Uia: return "UIA";
    case Provider::Msaa: return "MSAA";
    case Provider::Java: return "Java Access Bridge";
    case Provider::Visual: return "OpenCV";
    case Provider::Ocr: return "OCR";
    }
    return "Unknown";
}

bool supports(Operation operation, Role role) {
    switch (operation) {
    case Operation::Invoke: return role == Role::Button || role == Role::Link || role == Role::MenuItem;
    case Operation::SetValue: return role == Role::Edit || role == Role::ComboBox;
    case Operation::Check:
    case Operation::Uncheck: return role == Role::CheckBox;
    case Operation::Select: return role == Role::RadioButton || role == Role::ListItem || role == Role::TreeItem;
    case Operation::Expand:
    case Operation::Collapse: return role == Role::ComboBox || role == Role::TreeItem;
    case Operation::SetRangeValue: return role == Role::Slider || role == Role::Spinner;
    case Operation::Focus:
    case Operation::ScrollIntoView:
    case Operation::Scroll: return role >= Role::Any && role <= Role::Spinner;
    }
    return false;
}

bool supportsProvider(Operation operation, Role role, Provider provider) {
    if (!supports(operation, role)) return false;
    switch (provider) {
    case Provider::Uia: return true;
    case Provider::Msaa:
        return operation == Operation::Invoke || operation == Operation::SetValue ||
               operation == Operation::Select || operation == Operation::Focus ||
               operation == Operation::Scroll;
    case Provider::Java:
        return operation == Operation::Invoke || operation == Operation::SetValue ||
               operation == Operation::Focus || operation == Operation::Scroll;
    case Provider::Visual:
    case Provider::Ocr:
        return (operation == Operation::Invoke || operation == Operation::Scroll) && role == Role::Button;
    }
    return false;
}

bool validValue(Operation operation, const std::string& value) {
    if (value.size() > 4096 || value.find('\0') != std::string::npos) return false;
    if (operation == Operation::Scroll) {
        if (value.size() < 4 || value.size() > 8 ||
            (value.substr(0, 2) != "v:" && value.substr(0, 2) != "h:")) return false;
        int units = 0;
        const auto result = std::from_chars(value.data() + 2, value.data() + value.size(), units);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size() &&
            units != 0 && units >= -1200 && units <= 1200;
    }
    if (operation == Operation::SetValue) return true; // An empty string clears a field.
    if (operation == Operation::SetRangeValue) {
        if (value.empty()) return false;
        double number = 0;
        const auto converted = std::from_chars(value.data(), value.data() + value.size(),
            number, std::chars_format::general);
        return converted.ec == std::errc{} && converted.ptr == value.data() + value.size() &&
               std::isfinite(number);
    }
    return value.empty();
}

bool browserExecutable(const std::wstring& executable) {
    const std::wstring name = lower(executable);
    const auto pos = name.find_last_of(L"\\/");
    const std::wstring shortName = name.substr(pos == std::wstring::npos ? 0 : pos + 1);
    return shortName == L"chrome.exe" || shortName == L"msedge.exe" ||
           shortName == L"firefox.exe" || shortName == L"brave.exe" ||
           shortName == L"opera.exe" || shortName == L"vivaldi.exe" ||
           shortName == L"chromium.exe" || shortName == L"arc.exe";
}

// An accessible browser tab name is only a UIA selector, NOT a DOM address.
// The browser can hide tabs or expose no SelectionItemPattern at all.
std::vector<BrowserTab> listBrowserTabs(const clicknova::platform::WindowTarget& window) {
    std::vector<BrowserTab> result;
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(window.handle);
    DWORD pid = 0;
    if (!hwnd || !IsWindow(hwnd) ||
        !GetWindowThreadProcessId(hwnd, &pid) || !pid || pid != window.processId ||
        pid == GetCurrentProcessId()) return result;
    ComApartment apartment;
    if (!apartment.usable()) return result;
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(automation.GetAddressOf()))) || !automation) return result;
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(hwnd, root.GetAddressOf())) || !root) return result;
    VARIANT type; VariantInit(&type);
    type.vt = VT_I4;
    type.lVal = UIA_TabItemControlTypeId;
    ComPtr<IUIAutomationCondition> condition;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type,
            condition.GetAddressOf())) || !condition) return result;
    ComPtr<IUIAutomationElementArray> found;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.Get(), found.GetAddressOf())) ||
        !found) return result;
    int length = 0;
    if (FAILED(found->get_Length(&length)) || length < 0) return result;
    if (!browserExecutable(window.process) &&
        (length == 0 || !hasBrowserAddressBar(automation.Get(), root.Get()))) return result;
    std::unordered_set<std::wstring> seenTitles;
    std::unordered_set<std::wstring> duplicates;
    // Do not retain element pointers; the accessible tab list is refreshed on demand.
    for (int i = 0; i < std::min(length, 64); ++i) {
        ComPtr<IUIAutomationElement> candidate;
        if (FAILED(found->GetElement(i, candidate.GetAddressOf())) || !candidate) continue;
        std::wstring title = getString(candidate.Get(), &IUIAutomationElement::get_CurrentName);
        if (title.empty() || title.size() > 512 || title.find(L'\0') != std::wstring::npos) continue;
        // An element can be a TabItem without supporting selecting a browser tab.
        ComPtr<IUIAutomationSelectionItemPattern> pattern;
        if (FAILED(candidate->GetCurrentPatternAs(UIA_SelectionItemPatternId,
                IID_PPV_ARGS(pattern.GetAddressOf()))) || !pattern) continue;
        BOOL selected = FALSE;
        if (FAILED(pattern->get_CurrentIsSelected(&selected))) continue;
        // A duplicated name may be seen 3+ times; never reintroduce it.
        if (duplicates.contains(title)) continue;
        if (!seenTitles.insert(title).second) {
            duplicates.insert(title);
            result.erase(std::remove_if(result.begin(), result.end(), [&](const BrowserTab& tab) {
                return tab.title == title;
            }), result.end());
            continue;
        }
        result.push_back({std::move(title), selected != FALSE});
    }
    if (auto current = std::find_if(result.begin(), result.end(),
            [](const BrowserTab& tab) { return tab.active; }); current != result.end())
        current->activeUrl = activeAddressUrl(automation.Get(), root.Get());
#else
    (void)window;
#endif
    return result;
}

bool selectBrowserTab(const clicknova::platform::WindowTarget& window,
                      const std::wstring& title) {
    if (title.empty() || title.size() > 512 || title.find(L'\0') != std::wstring::npos)
        return false;
    if (!browserExecutable(window.process)) {
        const auto tabs = listBrowserTabs(window);
        if (std::none_of(tabs.begin(), tabs.end(),
                [&](const BrowserTab& tab) { return tab.title == title; })) return false;
    }
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(window.handle);
    DWORD pid = 0;
    if (!hwnd || !IsWindow(hwnd) || !GetWindowThreadProcessId(hwnd, &pid) ||
        !pid || pid != window.processId || pid == GetCurrentProcessId()) return false;
    ComApartment apartment;
    if (!apartment.usable()) return false;
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(automation.GetAddressOf()))) || !automation) return false;
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(hwnd, root.GetAddressOf())) || !root) return false;
    VARIANT name; VariantInit(&name);
    name.vt = VT_BSTR;
    name.bstrVal = SysAllocStringLen(title.data(), static_cast<UINT>(title.size()));
    if (!name.bstrVal) return false;
    ComPtr<IUIAutomationCondition> condition;
    const HRESULT created = automation->CreatePropertyCondition(UIA_NamePropertyId,
        name, condition.GetAddressOf());
    VariantClear(&name);
    if (FAILED(created) || !condition) return false;
    ComPtr<IUIAutomationElementArray> found;
    if (FAILED(root->FindAll(TreeScope_Descendants, condition.Get(), found.GetAddressOf())) ||
        !found) return false;
    int length = 0;
    if (FAILED(found->get_Length(&length)) || length < 1 || length > 64) return false;
    ComPtr<IUIAutomationSelectionItemPattern> selectedTab;
    for (int i = 0; i < length; ++i) {
        ComPtr<IUIAutomationElement> item;
        if (FAILED(found->GetElement(i, item.GetAddressOf())) || !item) continue;
        CONTROLTYPEID type = 0;
        if (FAILED(item->get_CurrentControlType(&type)) || type != UIA_TabItemControlTypeId)
            continue;
        ComPtr<IUIAutomationSelectionItemPattern> pattern;
        if (FAILED(item->GetCurrentPatternAs(UIA_SelectionItemPatternId,
                IID_PPV_ARGS(pattern.GetAddressOf()))) || !pattern) continue;
        if (selectedTab) return false; // Refuse ambiguous tab titles.
        selectedTab = pattern;
    }
    if (!selectedTab) return false;
    BOOL isSelected = FALSE;
    if (FAILED(selectedTab->get_CurrentIsSelected(&isSelected))) return false;
    if (!isSelected && FAILED(selectedTab->Select())) return false;
    isSelected = FALSE;
    return SUCCEEDED(selectedTab->get_CurrentIsSelected(&isSelected)) && isSelected;
#else
    (void)window;
    return false;
#endif
}

bool validQuery(const Query& query) {
    // Bound both string and result size before crossing into another process.
    if (query.text.size() > 512 || query.limit < 1 || query.limit > 600) return false;
    if (query.text.find(L'\0') != std::wstring::npos ||
        query.browserTab.size() > 512 || query.browserTab.find(L'\0') != std::wstring::npos ||
        (!query.browserTab.empty() && !query.browserOnly)) return false;
    return query.role >= Role::Any && query.role <= Role::Spinner &&
        query.visualTemplate.size() <= 500 &&
        (!query.visualOcr || (!query.text.empty() && query.text.size() <= 500)) &&
        query.imageConfidence >= 50 && query.imageConfidence <= 99 &&
        query.ocrConfidence >= 30 && query.ocrConfidence <= 99 &&
        query.visualRadius >= 1 && query.visualRadius <= 30;
}

static Result searchUia(const clicknova::platform::WindowTarget& window, const Query& query) {
    if (!validQuery(query)) return {{}, "Invalid accessibility search parameters."};
    if (query.browserOnly && !browserExecutable(window.process) && listBrowserTabs(window).empty())
        return {{}, "The window does not expose an identifiable browser surface."};
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(window.handle);
    DWORD currentPid = 0;
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) ||
        !GetWindowThreadProcessId(hwnd, &currentPid) ||
        currentPid != window.processId || currentPid == GetCurrentProcessId())
        return {{}, "The selected window is no longer available."};
    ComApartment apartment;
    if (!apartment.usable()) return {{}, "Could not initialize COM."};
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(automation.GetAddressOf()))) || !automation)
        return {{}, "Windows UI Automation is unavailable."};
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(hwnd, root.GetAddressOf())) || !root)
        return {{}, "Window does not expose an accessibility root."};
    // A tab and its browser window have different search scopes. Scanning the
    // whole browser for a chosen tab returns toolbar and extension buttons
    // before page content, making the 80-item limit especially misleading.
    // Choose the largest visible UIA Document under the selected tab. If the
    // provider does not expose a reliable document, report it rather than
    // accidentally binding a browser-toolbar control as a page element.
    if (!query.browserTab.empty()) {
        ComPtr<IUIAutomationCondition> documents;
        VARIANT docType{};
        docType.vt = VT_I4;
        docType.lVal = UIA_DocumentControlTypeId;
        if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId,
                docType, documents.GetAddressOf())) || !documents)
            return {{}, "Could not construct the browser document query."};
        ComPtr<IUIAutomationElementArray> candidates;
        if (FAILED(root->FindAll(TreeScope_Descendants, documents.Get(),
                candidates.GetAddressOf())) || !candidates)
            return {{}, "The browser does not expose a UIA document for this tab."};
        int count = 0;
        if (FAILED(candidates->get_Length(&count)) || count < 1 || count > 256)
            return {{}, "No unambiguous UIA document for the selected tab."};
        RECT windowRect{};
        if (!GetClientRect(hwnd, &windowRect))
            return {{}, "Could not inspect the browser client rectangle."};
        const double maximumArea = static_cast<double>(windowRect.right) * windowRect.bottom;
        ComPtr<IUIAutomationElement> bestDocument;
        double bestArea = 0.0;
        double secondArea = 0.0;
        for (int i = 0; i < count; ++i) {
            ComPtr<IUIAutomationElement> document;
            if (FAILED(candidates->GetElement(i, document.GetAddressOf())) || !document) continue;
            BOOL offscreen = TRUE;
            RECT bounds{};
            if (FAILED(document->get_CurrentIsOffscreen(&offscreen)) || offscreen ||
                FAILED(document->get_CurrentBoundingRectangle(&bounds))) continue;
            const double width = static_cast<double>(bounds.right) - bounds.left;
            const double height = static_cast<double>(bounds.bottom) - bounds.top;
            const double area = width * height;
            if (width < 120 || height < 80 || area <= 0 ||
                (maximumArea > 0 && area > maximumArea * 2.0)) continue;
            if (area > bestArea) {
                secondArea = bestArea;
                bestArea = area;
                bestDocument = std::move(document);
            } else if (area > secondArea) secondArea = area;
        }
        if (!bestDocument || (secondArea > 0 && secondArea >= bestArea * 0.97))
            return {{}, "The browser exposes no unique visible webpage document through UIA."};
        root = std::move(bestDocument);
    }
    ComPtr<IUIAutomationCondition> condition;
    if (query.role != Role::Any) {
        VARIANT value{};
        value.vt = VT_I4;
        value.lVal = controlType(query.role);
        if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value,
                condition.GetAddressOf())))
            return {{}, "Could not build the UI Automation filter."};
    } else if (FAILED(automation->CreateTrueCondition(condition.GetAddressOf())))
        return {{}, "Could not create a UI Automation query."};
    ComPtr<IUIAutomationElementArray> candidates;
    if (FAILED(root->FindAll(TreeScope_Subtree, condition.Get(), candidates.GetAddressOf())) ||
        !candidates)
        return {{}, "The application did not return an accessibility tree."};
    int length = 0;
    if (FAILED(candidates->get_Length(&length)))
        return {{}, "Could not read UI Automation results."};
    Result result;
    // The OS/provider creates the candidate array; stop reading after a bound
    // number of results. Never preserve COM element pointers in a saved plan.
    for (int i = 0; i < length; ++i) {
        ComPtr<IUIAutomationElement> candidate;
        if (FAILED(candidates->GetElement(i, candidate.GetAddressOf())) || !candidate) continue;
        Element item;
        item.name = getString(candidate.Get(), &IUIAutomationElement::get_CurrentName);
        item.automationId = getString(candidate.Get(), &IUIAutomationElement::get_CurrentAutomationId);
        if (!includes(item.name, query.text) && !includes(item.automationId, query.text)) continue;
        item.className = getString(candidate.Get(), &IUIAutomationElement::get_CurrentClassName);
        item.localizedRole = getString(candidate.Get(), &IUIAutomationElement::get_CurrentLocalizedControlType);
        CONTROLTYPEID typeId = 0;
        if (SUCCEEDED(candidate->get_CurrentControlType(&typeId))) item.role = classify(typeId);
        // UIA exposes plenty of nameless panes/containers without IDs. They
        // are valid provider objects, but almost never suitable selectors.
        // Keep actual controls (including unnamed buttons), and allow users
        // to explicitly include all anonymous containers for diagnostics.
        if (!query.includeAnonymous && query.text.empty() &&
            item.name.empty() && item.automationId.empty() &&
            item.role == Role::Any) continue;
        BOOL enabled = FALSE;
        if (SUCCEEDED(candidate->get_CurrentIsEnabled(&enabled))) item.enabled = enabled != FALSE;
        result.elements.push_back(std::move(item));
        if (result.elements.size() == query.limit) {
            result.truncated = i + 1 < length;
            break;
        }
    }
    return result;
#else
    (void)window;
    return {{}, "Windows UI Automation is supported only on Windows."};
#endif
}

Result search(const clicknova::platform::WindowTarget& window, const Query& query) {
    if (!validQuery(query)) return {{}, "Invalid accessibility search parameters."};
    // Browser-only accessibility selectors stay UIA-specific. An explicitly
    // requested visual scan may still inspect the active browser client area.
    if (query.browserOnly && query.visualTemplate.empty() && !query.visualOcr)
        return searchUia(window, query);

    Result merged;
    auto append = [&](Result current) {
        if (!current.elements.empty()) {
            for (auto& element : current.elements) {
                if (merged.elements.size() == query.limit) break;
                merged.elements.push_back(std::move(element));
            }
            merged.truncated |= current.truncated;
        }
        if (merged.error.empty() && !current.error.empty()) merged.error = std::move(current.error);
    };
    // Reserve explicit visual matches before the accessibility providers fill
    // their bounded pages. Screenshots are processed locally only.
    if (!query.visualTemplate.empty() || query.visualOcr) {
        Query visualQuery = query;
        visualQuery.limit = std::min<std::size_t>(query.limit, 30);
        auto visualResult = visual::search(window, visualQuery);
        if (!visualResult.error.empty()) return visualResult;
        append(std::move(visualResult));
    }
    if (query.browserOnly) {
        Query browser = query; browser.limit = query.limit - merged.elements.size();
        if (browser.limit) append(searchUia(window, browser));
        if (!merged.elements.empty()) merged.error.clear();
        return merged;
    }
    const bool java = native::isJavaWindow(window);
    // Reserve room for *all* native backends. A large UIA tree should not
    // hide MSAA controls, nor should UIA placeholders hide Java controls.
    if (java) {
        Query jab = query;
        jab.limit = std::max<std::size_t>(1, query.limit / 2);
        append(native::searchJava(window, jab));
    }
    Query uia = query;
    uia.limit = java ? std::max<std::size_t>(1, query.limit / 3) :
                       std::max<std::size_t>(1, query.limit * 3 / 4);
    uia.limit = std::min(uia.limit, query.limit - merged.elements.size());
    if (uia.limit > 0) append(searchUia(window, uia));
    if (merged.elements.size() < query.limit) {
        Query msaa = query;
        msaa.limit = query.limit - merged.elements.size();
        append(native::searchMsaa(window, msaa));
    }
    if (!merged.elements.empty()) merged.error.clear();
    return merged;
}

bool perform(const clicknova::platform::WindowTarget& window,
             const std::wstring& name, const std::wstring& automationId,
             const std::wstring& className, Role role, bool browserOnly,
             Operation operation, const std::wstring& valueText, Provider provider, bool allowBackground) {
    if ((name.empty() && automationId.empty()) || name.size() > 512 ||
        automationId.size() > 512 || className.size() > 512 ||
        !supportsProvider(operation, role, provider) ||
        (browserOnly && !browserExecutable(window.process) && listBrowserTabs(window).empty()) ||
        valueText.size() > 4096 || valueText.find(L'\0') != std::wstring::npos) return false;
    // Reject unused parameters rather than ignoring an accidentally retained value.
    const bool takesValue = operation == Operation::SetValue ||
        operation == Operation::SetRangeValue || operation == Operation::Scroll;
    if (!takesValue && !valueText.empty()) return false;
    std::string numericValue;
    if (operation == Operation::SetRangeValue || operation == Operation::Scroll) {
        if (valueText.empty() || valueText.size() > 128) return false;
        for (wchar_t ch : valueText) {
            if (ch > 127) return false;
            numericValue.push_back(static_cast<char>(ch));
        }
        if (!validValue(operation, numericValue)) return false;
    }
    // Only native, focus-independent operations are background-capable.
    if (allowBackground && (provider == Provider::Visual || provider == Provider::Ocr ||
        operation == Operation::Focus || browserOnly)) return false;
    // No silent fallback across providers: bindings keep their explicit origin.
    if (operation == Operation::Scroll && provider != Provider::Uia) return false;
    if (provider == Provider::Msaa)
        return native::performMsaa(window, name, automationId, role, operation, valueText, allowBackground);
    if (provider == Provider::Java)
        return native::performJava(window, name, automationId, role, operation, valueText, allowBackground);
    if (provider == Provider::Visual || provider == Provider::Ocr)
        return !browserOnly && visual::click(window, provider, name, automationId, className);
    if (provider != Provider::Uia) return false;
#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(window.handle);
    DWORD processId = 0;
    // A stale handle or another process is NEVER a valid click target.
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) ||
        !GetWindowThreadProcessId(hwnd, &processId) ||
        !processId || processId != window.processId ||
        processId == GetCurrentProcessId() ||
        (allowBackground ? clicknova::platform::checkAccessibleTarget(window) :
             clicknova::platform::checkTarget(window)) != clicknova::platform::TargetCheck::Ready)
        return false;
    ComApartment apartment;
    if (!apartment.usable()) return false;
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(automation.GetAddressOf()))) || !automation) return false;
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(hwnd, root.GetAddressOf())) || !root) return false;
    // Query exact ID (preferred) or Name; do NOT use substring matching for
    // automation. Other stored properties are checked below and ambiguities fail.
    const auto& exact = automationId.empty() ? name : automationId;
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocStringLen(exact.data(), static_cast<UINT>(exact.size()));
    if (!value.bstrVal) return false;
    ComPtr<IUIAutomationCondition> condition;
    const HRESULT conditionResult = automation->CreatePropertyCondition(
        automationId.empty() ? UIA_NamePropertyId : UIA_AutomationIdPropertyId,
        value, condition.GetAddressOf());
    VariantClear(&value);
    if (FAILED(conditionResult) || !condition) return false;
    ComPtr<IUIAutomationElementArray> matches;
    if (FAILED(root->FindAll(TreeScope_Subtree, condition.Get(), matches.GetAddressOf())) || !matches)
        return false;
    int count = 0;
    if (FAILED(matches->get_Length(&count)) || count < 1 || count > 4000) return false;
    ComPtr<IUIAutomationElement> unique;
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> item;
        if (FAILED(matches->GetElement(i, item.GetAddressOf())) || !item) continue;
        if ((!name.empty() && getString(item.Get(), &IUIAutomationElement::get_CurrentName) != name) ||
            (!automationId.empty() &&
             getString(item.Get(), &IUIAutomationElement::get_CurrentAutomationId) != automationId) ||
            (!className.empty() &&
             getString(item.Get(), &IUIAutomationElement::get_CurrentClassName) != className)) continue;
        CONTROLTYPEID actual = 0;
        if (FAILED(item->get_CurrentControlType(&actual)) ||
            (role != Role::Any && actual != controlType(role))) continue;
        if (unique) return false; // Ambiguous: never click a random duplicate.
        unique = item;
    }
    if (!unique) return false;
    BOOL enabled = FALSE, offscreen = TRUE;
    if (FAILED(unique->get_CurrentIsEnabled(&enabled)) || !enabled ||
        FAILED(unique->get_CurrentIsOffscreen(&offscreen)) ||
        (offscreen && operation != Operation::ScrollIntoView) ||
        (allowBackground ? clicknova::platform::checkAccessibleTarget(window) :
             clicknova::platform::checkTarget(window)) != clicknova::platform::TargetCheck::Ready)
        return false;
    BOOL isPassword = FALSE;
    if (operation == Operation::SetValue &&
        (FAILED(unique->get_CurrentIsPassword(&isPassword)) || isPassword)) return false;
    if (operation == Operation::Focus) return SUCCEEDED(unique->SetFocus());
    PATTERNID patternId = 0;
    switch (operation) {
    case Operation::Invoke: patternId = UIA_InvokePatternId; break;
    case Operation::SetValue: patternId = UIA_ValuePatternId; break;
    case Operation::Check:
    case Operation::Uncheck: patternId = UIA_TogglePatternId; break;
    case Operation::Select: patternId = UIA_SelectionItemPatternId; break;
    case Operation::Expand:
    case Operation::Collapse: patternId = UIA_ExpandCollapsePatternId; break;
    case Operation::SetRangeValue: patternId = UIA_RangeValuePatternId; break;
    case Operation::ScrollIntoView: patternId = UIA_ScrollItemPatternId; break;
    case Operation::Scroll: patternId = UIA_ScrollPatternId; break;
    case Operation::Focus: return false;
    }
    ComPtr<IUnknown> rawPattern;
    if (FAILED(unique->GetCurrentPattern(patternId, rawPattern.GetAddressOf())) || !rawPattern) return false;
    switch (operation) {
    case Operation::Invoke: {
        ComPtr<IUIAutomationInvokePattern> p;
        return SUCCEEDED(rawPattern.As(&p)) && p && SUCCEEDED(p->Invoke());
    }
    case Operation::SetValue: {
        ComPtr<IUIAutomationValuePattern> p;
        BOOL readOnly = TRUE;
        if (FAILED(rawPattern.As(&p)) || !p ||
            FAILED(p->get_CurrentIsReadOnly(&readOnly)) || readOnly) return false;
        // UIA expects a COM BSTR, not std::wstring::c_str() (const wchar_t*).
        // SysAllocStringLen also permits an empty value to clear the field.
        BSTR text = SysAllocStringLen(valueText.data(), static_cast<UINT>(valueText.size()));
        if (!text) return false;
        const HRESULT result = p->SetValue(text);
        SysFreeString(text);
        return SUCCEEDED(result);
    }
    case Operation::Check:
    case Operation::Uncheck: {
        ComPtr<IUIAutomationTogglePattern> p;
        if (FAILED(rawPattern.As(&p)) || !p) return false;
        const ToggleState wanted = operation == Operation::Check ? ToggleState_On : ToggleState_Off;
        for (int attempt = 0; attempt < 3; ++attempt) {
            ToggleState actual = ToggleState_Indeterminate;
            if (FAILED(p->get_CurrentToggleState(&actual))) return false;
            if (actual == wanted) return true;
            if (FAILED(p->Toggle())) return false;
        }
        ToggleState actual = ToggleState_Indeterminate;
        return SUCCEEDED(p->get_CurrentToggleState(&actual)) && actual == wanted;
    }
    case Operation::Select: {
        ComPtr<IUIAutomationSelectionItemPattern> p;
        return SUCCEEDED(rawPattern.As(&p)) && p && SUCCEEDED(p->Select());
    }
    case Operation::Expand:
    case Operation::Collapse: {
        ComPtr<IUIAutomationExpandCollapsePattern> p;
        if (FAILED(rawPattern.As(&p)) || !p) return false;
        ExpandCollapseState state = ExpandCollapseState_LeafNode;
        if (FAILED(p->get_CurrentExpandCollapseState(&state)) || state == ExpandCollapseState_LeafNode) return false;
        if (operation == Operation::Expand) {
            return state == ExpandCollapseState_Expanded || SUCCEEDED(p->Expand());
        }
        return state == ExpandCollapseState_Collapsed || SUCCEEDED(p->Collapse());
    }
    case Operation::SetRangeValue: {
        ComPtr<IUIAutomationRangeValuePattern> p;
        BOOL readOnly = TRUE;
        double min = 0, max = 0, number = 0;
        const auto converted = std::from_chars(numericValue.data(), numericValue.data() + numericValue.size(),
            number, std::chars_format::general);
        return SUCCEEDED(rawPattern.As(&p)) && p &&
            converted.ec == std::errc{} && converted.ptr == numericValue.data() + numericValue.size() &&
            SUCCEEDED(p->get_CurrentIsReadOnly(&readOnly)) && !readOnly &&
            SUCCEEDED(p->get_CurrentMinimum(&min)) && SUCCEEDED(p->get_CurrentMaximum(&max)) &&
            number >= min && number <= max && SUCCEEDED(p->SetValue(number));
    }
    case Operation::ScrollIntoView: {
        ComPtr<IUIAutomationScrollItemPattern> p;
        return SUCCEEDED(rawPattern.As(&p)) && p && SUCCEEDED(p->ScrollIntoView());
    }
    case Operation::Scroll: {
        ComPtr<IUIAutomationScrollPattern> p;
        if (FAILED(rawPattern.As(&p)) || !p) return false;
        const bool vertical = numericValue[0] == 'v';
        int delta = 0;
        const auto result = std::from_chars(numericValue.data() + 2,
            numericValue.data() + numericValue.size(), delta);
        if (result.ec != std::errc{} || delta == 0) return false;
        BOOL scrollable = FALSE;
        if (FAILED(vertical ? p->get_CurrentVerticallyScrollable(&scrollable) :
                              p->get_CurrentHorizontallyScrollable(&scrollable)) || !scrollable)
            return false;
        // Wheel sign convention: positive = up/left, negative = down/right.
        const ScrollAmount movement = delta > 0 ? ScrollAmount_SmallDecrement : ScrollAmount_SmallIncrement;
        const auto steps = std::max(1, (std::abs(delta) + 119) / 120);
        for (int i = 0; i < steps; ++i) {
            const HRESULT code = vertical ?
                p->Scroll(ScrollAmount_NoAmount, movement) :
                p->Scroll(movement, ScrollAmount_NoAmount);
            if (FAILED(code)) return false;
        }
        return true;
    }
    case Operation::Focus: return false;
    }
    return false;
#else
    (void)window;
    (void)className;
    (void)valueText;
    (void)numericValue;
    return false;
#endif
}

bool present(const clicknova::platform::WindowTarget& window,
             const std::wstring& name, const std::wstring& automationId,
             const std::wstring& className, Role role, bool browserOnly,
             Provider provider, bool allowBackground) {
    if ((name.empty() && automationId.empty()) || name.size() > 512 ||
        automationId.size() > 512 || className.size() > 512 ||
        provider < Provider::Uia || provider > Provider::Ocr ||
        (browserOnly && provider != Provider::Uia) ||
        (allowBackground && (provider == Provider::Visual || provider == Provider::Ocr || browserOnly)))
        return false;
    if ((allowBackground ? clicknova::platform::checkAccessibleTarget(window) :
          clicknova::platform::checkTarget(window)) != clicknova::platform::TargetCheck::Ready)
        return false;
    if (provider == Provider::Visual || provider == Provider::Ocr)
        return visual::present(window, provider, name, automationId, className);
    if (browserOnly && !browserExecutable(window.process) && listBrowserTabs(window).empty())
        return false;
    // Presence checks must not be limited to the first N elements of a large
    // webpage. Query the native provider by exact identity, just as perform()
    // does. No actions or input are sent while checking presence.
    if (provider == Provider::Uia) {
#ifdef _WIN32
        HWND hwnd = reinterpret_cast<HWND>(window.handle);
        DWORD pid = 0;
        if (!hwnd || !IsWindow(hwnd) ||
            !GetWindowThreadProcessId(hwnd, &pid) || pid != window.processId ||
            pid == GetCurrentProcessId()) return false;
        ComApartment apartment;
        if (!apartment.usable()) return false;
        ComPtr<IUIAutomation> automation;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(automation.GetAddressOf()))) || !automation) return false;
        ComPtr<IUIAutomationElement> root;
        if (FAILED(automation->ElementFromHandle(hwnd, root.GetAddressOf())) || !root)
            return false;
        const std::wstring& exact = automationId.empty() ? name : automationId;
        VARIANT value{};
        value.vt = VT_BSTR;
        value.bstrVal = SysAllocStringLen(exact.data(), static_cast<UINT>(exact.size()));
        if (!value.bstrVal) return false;
        ComPtr<IUIAutomationCondition> condition;
        const HRESULT status = automation->CreatePropertyCondition(
            automationId.empty() ? UIA_NamePropertyId : UIA_AutomationIdPropertyId,
            value, condition.GetAddressOf());
        VariantClear(&value);
        if (FAILED(status) || !condition) return false;
        ComPtr<IUIAutomationElementArray> matches;
        if (FAILED(root->FindAll(TreeScope_Subtree, condition.Get(), matches.GetAddressOf())) ||
            !matches) return false;
        int count = 0;
        if (FAILED(matches->get_Length(&count)) || count < 1 || count > 4000) return false;
        bool unique = false;
        for (int i = 0; i < count; ++i) {
            ComPtr<IUIAutomationElement> item;
            if (FAILED(matches->GetElement(i, item.GetAddressOf())) || !item) continue;
            if ((!name.empty() && getString(item.Get(), &IUIAutomationElement::get_CurrentName) != name) ||
                (!automationId.empty() &&
                 getString(item.Get(), &IUIAutomationElement::get_CurrentAutomationId) != automationId) ||
                (!className.empty() &&
                 getString(item.Get(), &IUIAutomationElement::get_CurrentClassName) != className)) continue;
            CONTROLTYPEID actual = 0;
            if (FAILED(item->get_CurrentControlType(&actual)) ||
                (role != Role::Any && actual != controlType(role))) continue;
            if (unique) return false;
            unique = true;
        }
        return unique;
#else
        return false;
#endif
    }
    Query query;
    query.text = automationId.empty() ? name : automationId;
    query.role = role;
    query.limit = 200;
    query.includeAnonymous = true;
    Result candidates;
    switch (provider) {
    case Provider::Uia: candidates = searchUia(window, query); break;
    case Provider::Msaa: candidates = native::searchMsaa(window, query); break;
    case Provider::Java: candidates = native::searchJava(window, query); break;
    default: return false;
    }
    if (!candidates.error.empty() || candidates.truncated) return false;
    unsigned matches = 0;
    for (const auto& item : candidates.elements) {
        if (item.provider != provider ||
            (!name.empty() && name != item.name) ||
            (!automationId.empty() && automationId != item.automationId) ||
            (!className.empty() && className != item.className) ||
            (role != Role::Any && item.role != role)) continue;
        if (++matches > 1) return false;
    }
    return matches == 1;
}


// Exact foreground geometry resolution for pointer-only gestures. No stored
// pixel coordinates, no fallback to the window center.
static std::optional<ScreenPoint> uiaPoint(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& id,
    const std::wstring& className, Role role) {
#ifdef _WIN32
    if ((name.empty() && id.empty()) ||
        clicknova::platform::checkTarget(window) != clicknova::platform::TargetCheck::Ready)
        return std::nullopt;
    ComApartment apartment;
    if (!apartment.usable()) return std::nullopt;
    ComPtr<IUIAutomation> automation;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
           IID_PPV_ARGS(automation.GetAddressOf()))) || !automation) return std::nullopt;
    ComPtr<IUIAutomationElement> root;
    if (FAILED(automation->ElementFromHandle(reinterpret_cast<HWND>(window.handle),
         root.GetAddressOf())) || !root) return std::nullopt;
    const std::wstring& key = id.empty() ? name : id;
    VARIANT filter{}; filter.vt = VT_BSTR;
    filter.bstrVal = SysAllocStringLen(key.data(), static_cast<UINT>(key.size()));
    if (!filter.bstrVal) return std::nullopt;
    ComPtr<IUIAutomationCondition> condition;
    const HRESULT created = automation->CreatePropertyCondition(
        id.empty() ? UIA_NamePropertyId : UIA_AutomationIdPropertyId,
        filter, condition.GetAddressOf());
    VariantClear(&filter);
    if (FAILED(created) || !condition) return std::nullopt;
    ComPtr<IUIAutomationElementArray> matches;
    if (FAILED(root->FindAll(TreeScope_Subtree, condition.Get(), matches.GetAddressOf())) || !matches)
        return std::nullopt;
    int count = 0;
    if (FAILED(matches->get_Length(&count)) || count < 1 || count > 4000) return std::nullopt;
    std::optional<ScreenPoint> only;
    for (int i = 0; i < count; ++i) {
        ComPtr<IUIAutomationElement> candidate;
        if (FAILED(matches->GetElement(i, candidate.GetAddressOf())) || !candidate) continue;
        if ((!name.empty() && getString(candidate.Get(), &IUIAutomationElement::get_CurrentName) != name) ||
            (!id.empty() && getString(candidate.Get(), &IUIAutomationElement::get_CurrentAutomationId) != id) ||
            (!className.empty() && getString(candidate.Get(), &IUIAutomationElement::get_CurrentClassName) != className)) continue;
        CONTROLTYPEID type = 0;
        if (FAILED(candidate->get_CurrentControlType(&type)) ||
            (role != Role::Any && controlType(role) != type)) continue;
        if (only) return std::nullopt; // More than one exact match.
        BOOL enabled = FALSE, offscreen = TRUE;
        RECT rect{};
        if (FAILED(candidate->get_CurrentIsEnabled(&enabled)) || !enabled ||
            FAILED(candidate->get_CurrentIsOffscreen(&offscreen)) || offscreen ||
            FAILED(candidate->get_CurrentBoundingRectangle(&rect)) ||
            rect.right <= rect.left || rect.bottom <= rect.top) return std::nullopt;
        only = ScreenPoint{rect.left + (rect.right - rect.left)/2,
                           rect.top + (rect.bottom - rect.top)/2};
    }
    return only;
#else
    (void)window; (void)name; (void)id; (void)className; (void)role;
    return std::nullopt;
#endif
}

bool pointerGesture(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& automationId,
    const std::wstring& className, Role role, Provider provider,
    PointerGesture gesture, bool horizontal, int delta, int intervalMs,
    std::stop_token stop) {
#ifdef _WIN32
    if (stop.stop_requested() ||
        clicknova::platform::checkTarget(window) != clicknova::platform::TargetCheck::Ready ||
        (gesture == PointerGesture::Wheel && (delta == 0 || delta < -1200 || delta > 1200)) ||
        (gesture == PointerGesture::DoubleClick && (intervalMs < 10 ||
             intervalMs > static_cast<int>(GetDoubleClickTime())))) return false;
    const auto locate = [&]() -> std::optional<ScreenPoint> {
        switch (provider) {
        case Provider::Uia: return uiaPoint(window, name, automationId, className, role);
        case Provider::Msaa: return native::pointMsaa(window, name, automationId, role);
        case Provider::Java: return native::pointJava(window, name, automationId, role);
        case Provider::Visual:
        case Provider::Ocr: return visual::locate(window, provider, name, automationId, className);
        }
        return std::nullopt;
    };
    const auto target = locate();
    if (!target || clicknova::platform::checkScreenPoint(window, target->x, target->y) !=
         clicknova::platform::TargetCheck::Ready) return false;
    const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width < 2 || height < 2) return false;
    INPUT move{}; move.type = INPUT_MOUSE;
    move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    move.mi.dx = MulDiv(target->x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535, width - 1);
    move.mi.dy = MulDiv(target->y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535, height - 1);
    if (SendInput(1, &move, sizeof(INPUT)) != 1) return false;
    if (clicknova::platform::checkScreenPoint(window, target->x, target->y) !=
         clicknova::platform::TargetCheck::Ready) return false;
    if (gesture == PointerGesture::Wheel) {
        INPUT wheel{}; wheel.type = INPUT_MOUSE;
        wheel.mi.dwFlags = horizontal ? MOUSEEVENTF_HWHEEL : MOUSEEVENTF_WHEEL;
        // Windows HWHEEL: positive goes right; ClickNova's convention is
        // positive left, consistent with UIA SmallDecrement.
        wheel.mi.mouseData = static_cast<DWORD>(horizontal ? -delta : delta);
        return !stop.stop_requested() && SendInput(1, &wheel, sizeof(INPUT)) == 1;
    }
    const auto clickOnce = [&]() {
        INPUT input[2]{};
        input[0].type = INPUT_MOUSE; input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        input[1].type = INPUT_MOUSE; input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        if (SendInput(2, input, sizeof(INPUT)) == 2) return true;
        SendInput(1, &input[1], sizeof(INPUT)); // Always try to release.
        return false;
    };
    if (stop.stop_requested() || !clickOnce()) return false;
    const auto firstClickAt = std::chrono::steady_clock::now();
    // A real OS double-click requires two input pairs inside the system limit.
    // Cancellation and focus checks prevent a click in a different window.
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);
    while (!stop.stop_requested() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (stop.stop_requested()) return false;
    // Re-resolve after the first click; a UI change or occlusion must fail closed.
    const auto second = locate();
    if (!second || std::abs(second->x-target->x) > 4 || std::abs(second->y-target->y) > 4 ||
        std::chrono::steady_clock::now() - firstClickAt >
            std::chrono::milliseconds(GetDoubleClickTime()) ||
        clicknova::platform::checkScreenPoint(window, second->x, second->y) !=
        clicknova::platform::TargetCheck::Ready) return false;
    return clickOnce();
#else
    (void)window; (void)name; (void)automationId; (void)className;
    (void)role; (void)provider; (void)gesture; (void)horizontal;
    (void)delta; (void)intervalMs; (void)stop;
    return false;
#endif
}

bool invoke(const clicknova::platform::WindowTarget& window,
            const std::wstring& name, const std::wstring& automationId,
            const std::wstring& className, Role role, bool browserOnly) {
    return perform(window, name, automationId, className, role, browserOnly, Operation::Invoke, L"");
}

} // namespace clicknova::accessibility
