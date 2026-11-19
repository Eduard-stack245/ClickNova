// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/input_actions.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace clicknova::browser::scope {
// Per-action browser-tab scope is an existing StepWindow feature. It remains
// subject to the native executor's live window/tab checks at playback time.
// QML may edit only this scope, never a UIA selector or native handle.
inline bool hasSelector(const input::StepWindow& window) noexcept {
    // A weak name-only or class-only legacy selector is insufficient for
    // adding a new exact browser-tab restriction from QML.
    return !window.elementAutomationId.empty() &&
           !window.elementClass.empty();
}
inline bool eligible(const input::Action& action,
                     const input::StepWindow& window) noexcept {
    const auto* click = std::get_if<input::Click>(&action);
    return click && click->button == input::Button::Left && click->count == 1 &&
           window.enforce && !window.title.empty() && !window.process.empty() &&
           window.elementProvider == 0 && window.elementOperation == 0 &&
           hasSelector(window);
}
// Strict, UTF-8 scalar validation: no control characters, invalid encodings,
// bidi-override/isolate characters or hidden whitespace. Never trim a name,
// silently coerce it or interpret it as a URL/regex. A tab title is private.
inline bool validTabTitle(std::string_view text) noexcept {
    if (text.empty() || text.size() > 384) return false;
    std::size_t count = 0;
    std::uint32_t first = 0, last = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::uint32_t scalar = 0;
        std::size_t following = 0;
        if (lead <= 0x7f) scalar = lead;
        else if (lead >= 0xc2 && lead <= 0xdf) { scalar = lead & 0x1f; following = 1; }
        else if (lead >= 0xe0 && lead <= 0xef) { scalar = lead & 0x0f; following = 2; }
        else if (lead >= 0xf0 && lead <= 0xf4) { scalar = lead & 0x07; following = 3; }
        else return false;
        if (i + following >= text.size()) return false;
        for (std::size_t n = 1; n <= following; ++n) {
            const auto c = static_cast<unsigned char>(text[i+n]);
            if ((c & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (c & 0x3f);
        }
        if ((following == 1 && scalar < 0x80) ||
            (following == 2 && scalar < 0x800) ||
            (following == 3 && scalar < 0x10000) ||
            scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff) ||
            scalar < 0x20 || (scalar >= 0x7f && scalar <= 0x9f) ||
            scalar == 0x00ad || scalar == 0x200b || scalar == 0x200e ||
            scalar == 0x200f || scalar == 0xfeff ||
            (scalar >= 0x202a && scalar <= 0x202e) ||
            (scalar >= 0x2066 && scalar <= 0x2069)) return false;
        if (++count > 128) return false;
        if (count == 1) first = scalar;
        last = scalar;
        i += following + 1;
    }
    // Reject accidental leading/trailing whitespace instead of silently
    // turning a different tab title into a target.
    const auto edgeSpace = [](std::uint32_t c) noexcept {
        return c == 0x20 || c == 0x00a0 || c == 0x1680 || c == 0x202f ||
               c == 0x205f || c == 0x3000 || (c >= 0x2000 && c <= 0x200a);
    };
    return !edgeSpace(first) && !edgeSpace(last);
}
// Clearing always remains possible, even if an old selector has become
// invalid. Enabling requires an explicit, validated title and safe action.
inline std::optional<input::StepWindow> propose(const input::Action& action,
        const input::StepWindow& current, bool enable, std::string_view title) {
    input::StepWindow replacement = current;
    if (!enable) {
        replacement.elementBrowser = false;
        replacement.elementTab.clear();
        return replacement;
    }
    if (!eligible(action, current) || !validTabTitle(title)) return std::nullopt;
    replacement.elementBrowser = true;
    replacement.elementTab.assign(title.begin(), title.end());
    return replacement;
}
} // namespace clicknova::browser::scope
