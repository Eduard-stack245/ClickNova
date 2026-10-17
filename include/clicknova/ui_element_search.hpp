// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/window_target.hpp"

#include <cstddef>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace clicknova::accessibility {

// Windows UI Automation exposes native controls and the accessible subset of
// browser content. It is NOT a browser DOM selector or a persistent binding.
enum class Role { Any, Button, Edit, CheckBox, ComboBox, Link, Text, MenuItem,
                  RadioButton, ListItem, TreeItem, Slider, Spinner };
// Explicit, persistent action. No implicit fallback to desktop input.
enum class Provider { Uia = 0, Msaa = 1, Java = 2, Visual = 3, Ocr = 4 };
const char* providerName(Provider provider);
enum class Operation { Invoke, SetValue, Check, Uncheck, Select, Expand, Collapse,
                       SetRangeValue, Focus, ScrollIntoView, Scroll };
bool supports(Operation operation, Role role);
bool supportsProvider(Operation operation, Role role, Provider provider);
bool validValue(Operation operation, const std::string& utf8Value);
struct Query {
    std::wstring text; // Case-insensitive substring of Name or AutomationId.
    Role role{Role::Any};
    bool browserOnly{false};
    std::size_t limit{400};
    // By default, skip anonymous, unclassified provider containers that bury
    // useful controls in the first page of results.
    bool includeAnonymous{false};
    // When a specific tab is selected, scan its visible webpage document,
    // rather than the browser toolbar and unrelated chrome controls.
    std::wstring browserTab;
    std::wstring visualTemplate; // Absolute user-chosen PNG/JPEG/BMP filename.
    bool visualOcr{false};      // OCR searches the nonempty text query.
    int imageConfidence{90};   // OpenCV correlation as percent (50..99).
    int ocrConfidence{60};     // Tesseract line confidence (30..99).
    int visualRadius{8};       // Max displacement in normalized window coordinates, percent (1..30).
};
struct Element {
    std::wstring name;
    std::wstring automationId;
    std::wstring className;
    std::wstring localizedRole; // The actual UIA control type (e.g. pane/window).
    Role role{Role::Any};
    bool enabled{false};
    Provider provider{Provider::Uia};
};
struct Result {
    std::vector<Element> elements;
    std::string error;
    bool truncated{false};
};

struct ScreenPoint { int x{0}; int y{0}; };
// Coordinates are resolved at execution time, from an exact accessible element
// or a uniquely recognized visible image. Never persisted into the scenario.
// Only foreground actions. No background input or silent provider fallback.
enum class PointerGesture { DoubleClick, Wheel };
bool pointerGesture(const clicknova::platform::WindowTarget& window,
    const std::wstring& name, const std::wstring& automationId,
    const std::wstring& className, Role role, Provider provider,
    PointerGesture gesture, bool horizontal, int delta, int intervalMs,
    std::stop_token stop);

// Browser tabs are best-effort accessibility objects, not DOM URLs.
// A site is listed only if the browser exposes a named UIA TabItem.
struct BrowserTab { std::wstring title; bool active{false}; std::wstring activeUrl; };
std::vector<BrowserTab> listBrowserTabs(const clicknova::platform::WindowTarget& window);
// Select the exact, unique tab explicitly chosen by the user. Never guess.
bool selectBrowserTab(const clicknova::platform::WindowTarget& window, const std::wstring& title);
bool browserExecutable(const std::wstring& executable);
bool validQuery(const Query& query);
// An element is resolved afresh in the target window for EVERY operation.
// SetValue and SetRangeValue require a value; Check/Uncheck are idempotent.
// A failed pattern never falls back to coordinate clicks or SendInput.
bool perform(const clicknova::platform::WindowTarget& window,
             const std::wstring& name, const std::wstring& automationId,
             const std::wstring& className, Role role, bool browserOnly,
             Operation operation, const std::wstring& value,
             Provider provider = Provider::Uia, bool allowBackground = false);
// Read-only resolution for wait/assert. A result is true ONLY when one
// uniquely matching element exists, enabled or disabled (no input injection).
bool present(const clicknova::platform::WindowTarget& window,
             const std::wstring& name, const std::wstring& automationId,
             const std::wstring& className, Role role, bool browserOnly,
             Provider provider, bool allowBackground);
// Backward-compatible Invoke interface.
// An Invoke target is resolved fresh in its persisted window on EVERY click.
// Exact, unique match by automation ID or Name, optional Role and class.
// Returns false (fail closed) for missing/ambiguous/disabled/non-invokable controls.
bool invoke(const clicknova::platform::WindowTarget& window,
            const std::wstring& name, const std::wstring& automationId,
            const std::wstring& className, Role role, bool browserOnly);
// Read-only discovery. Results describe the current accessibility tree only:
// no click, text injection, UIA Invoke or persistent element reference.
Result search(const clicknova::platform::WindowTarget& window, const Query& query);

} // namespace clicknova::accessibility
