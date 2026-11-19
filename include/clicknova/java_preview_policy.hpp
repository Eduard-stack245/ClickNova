// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/element_candidate_search.hpp"
#include <string_view>

namespace clicknova::accessibility::java_preview {
// Java Access Bridge reports role_en_US/states_en_US as localized metadata.
// Map only a fixed vocabulary to generic UIA-style presentation roles. Never
// transfer Java AccessibleName, AccessibleDescription, text, JNI handles or IDs.
constexpr int genericRole(std::wstring_view role) noexcept {
    if (role == L"password text") return -1; // never disclose password controls
    if (role == L"push button" || role == L"toggle button") return 50000;
    if (role == L"hyperlink") return 50005;
    if (role == L"menu item") return 50011;
    if (role == L"check box") return 50002;
    if (role == L"combo box") return 50003;
    if (role == L"text") return 50004;
    if (role == L"label") return 50020;
    if (role == L"list item") return 50007;
    if (role == L"list") return 50008;
    if (role == L"menu" || role == L"popup menu") return 50009;
    if (role == L"menu bar") return 50010;
    if (role == L"radio button") return 50013;
    if (role == L"scroll bar") return 50014;
    if (role == L"slider") return 50015;
    if (role == L"spin box") return 50016;
    if (role == L"status bar") return 50017;
    if (role == L"page tab list") return 50018;
    if (role == L"page tab") return 50019;
    if (role == L"tool bar") return 50021;
    if (role == L"tool tip") return 50022;
    if (role == L"tree") return 50023;
    if (role == L"table") return 50036;
    if (role == L"progress bar") return 50012;
    if (role == L"window" || role == L"frame" || role == L"dialog") return 50032;
    if (role == L"panel" || role == L"root pane" || role == L"viewport") return 50033;
    return 50025; // unknown, not bindable
}

// Token-aware check: 'not enabled' or 'enabled-extra' must never be treated
// as enabled. Missing state information is treated as disabled.
constexpr bool hasState(std::wstring_view states, std::wstring_view state) noexcept {
    while (!states.empty()) {
        const auto comma = states.find(L',');
        auto token = states.substr(0, comma);
        const auto start = token.find_first_not_of(L" \t");
        if (start != std::wstring_view::npos) {
            token.remove_prefix(start);
            const auto end = token.find_last_not_of(L" \t");
            token = token.substr(0, end + 1);
            if (token == state) return true;
        }
        if (comma == std::wstring_view::npos) break;
        states.remove_prefix(comma + 1);
    }
    return false;
}
constexpr bool enabled(std::wstring_view states) noexcept {
    return hasState(states, L"enabled") && !hasState(states, L"invisible") &&
           !hasState(states, L"defunct");
}
inline candidates::Candidate preview(std::wstring_view role,
                                     std::wstring_view states) {
    return {genericRole(role), enabled(states), {}, {}};
}
constexpr bool sendable(const candidates::Candidate& candidate) noexcept {
    return candidate.controlType >= 0 && candidate.controlType <= 100000 &&
           candidate.automationId.empty() && candidate.className.empty() &&
           !candidates::eligible(candidate);
}
} // namespace clicknova::accessibility::java_preview
