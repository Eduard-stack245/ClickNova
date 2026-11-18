// SPDX-License-Identifier: MIT
#include "clicknova/java_preview_policy.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#define CHECK(x) do { if (!(x)) {std::cerr << "Java policy FAIL line " << __LINE__ << ": " << #x << '\n'; std::abort();} } while(false)
namespace jp = clicknova::accessibility::java_preview;
namespace ca = clicknova::accessibility::candidates;
int main() {
    CHECK(jp::genericRole(L"push button") == 50000);
    CHECK(jp::genericRole(L"menu item") == 50011);
    CHECK(jp::genericRole(L"check box") == 50002);
    CHECK(jp::genericRole(L"password text") == -1);
    CHECK(jp::genericRole(L"unknown-secret-value") == 50025);
    CHECK(jp::enabled(L"visible, enabled, focusable"));
    CHECK(jp::enabled(L"  enabled  "));
    CHECK(!jp::enabled(L"enabled-extra, focusable"));
    CHECK(!jp::enabled(L"disabled, visible"));
    CHECK(!jp::enabled(L"enabled, invisible"));
    CHECK(!jp::enabled(L"enabled, defunct"));
    CHECK(!jp::enabled(L"not enabled"));
    CHECK(!jp::enabled(L""));
    auto c = jp::preview(L"push button", L"enabled, focusable");
    CHECK(c.enabled && c.controlType == 50000 && jp::sendable(c));
    CHECK(!ca::eligible(c));
    c.automationId = "private";
    CHECK(!jp::sendable(c));
    c.automationId.clear();c.className = "private";
    CHECK(!jp::sendable(c));
    c.className.clear();c.controlType = -1;
    CHECK(!jp::sendable(c));
    // Deterministic property tests: unknown or adversarial metadata never
    // turns into a stable selector and never changes the absence of identifiers.
    std::uint32_t seed = 0xabcdeff1u;
    for (int i = 0; i < 28000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        std::wstring role(static_cast<std::size_t>(seed % 80), L'a');
        if (seed % 7 == 0) role = L"password text";
        const auto info = jp::preview(role, (seed & 1) ? L"enabled, visible" : L"disabled");
        CHECK(info.automationId.empty() && info.className.empty());
        CHECK(jp::sendable(info) == (jp::genericRole(role) >= 0));
        CHECK(!ca::eligible(info));
        if (role == L"password text") CHECK(info.controlType == -1);
    }
    std::cout << "PASS Java role/privacy/read-only policy, 28000 adversarial cases\n";
}
