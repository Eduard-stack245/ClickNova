// SPDX-License-Identifier: Apache-2.0
#pragma once

// Qt-independent parser for an editable relative wait duration. The result is
// milliseconds, matching the existing Wait action, so saved plans and playback
// need no new format and no conversion via floating-point numbers.
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace clicknova::duration {
namespace detail {
inline bool digit(char16_t c) noexcept { return c >= u'0' && c <= u'9'; }
inline bool space(char16_t c) noexcept {
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u'\u00a0';
}
inline bool separator(char16_t c) noexcept {
    return space(c) || c == u',' || c == u';' || c == u'+';
}
inline bool letter(char16_t c) noexcept {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') ||
        (c >= u'\u0430' && c <= u'\u044f') || (c >= u'\u0410' && c <= u'\u042f') ||
        c == u'\u0451' || c == u'\u0401';
}
inline char16_t lower(char16_t c) noexcept {
    if (c >= u'A' && c <= u'Z') return static_cast<char16_t>(c + (u'a' - u'A'));
    if (c >= u'\u0410' && c <= u'\u042f') return static_cast<char16_t>(c + (u'\u0430' - u'\u0410'));
    if (c == u'\u0401') return u'\u0451';
    return c;
}
inline std::optional<std::int64_t> unitFactor(std::u16string_view value) {
    std::u16string u;
    u.reserve(value.size());
    for (char16_t c : value) u.push_back(lower(c));
    if (u == u"ms" || u == u"msec" || u == u"millisecond" || u == u"milliseconds" ||
        u == u"\u043c\u0441" || u == u"\u043c\u0438\u043b\u043b\u0438\u0441\u0435\u043a\u0443\u043d\u0434" || u == u"\u043c\u0438\u043b\u043b\u0438\u0441\u0435\u043a\u0443\u043d\u0434\u0430" ||
        u == u"\u043c\u0438\u043b\u043b\u0438\u0441\u0435\u043a\u0443\u043d\u0434\u044b" ) return 1;
    if (u == u"s" || u == u"sec" || u == u"secs" || u == u"second" || u == u"seconds" ||
        u == u"\u0441" || u == u"\u0441\u0435\u043a" || u == u"\u0441\u0435\u043a\u0443\u043d\u0434" || u == u"\u0441\u0435\u043a\u0443\u043d\u0434\u0430" ||
        u == u"\u0441\u0435\u043a\u0443\u043d\u0434\u044b") return 1000;
    if (u == u"m" || u == u"min" || u == u"mins" || u == u"minute" || u == u"minutes" ||
        u == u"\u043c\u0438\u043d" || u == u"\u043c\u0438\u043d\u0443\u0442" || u == u"\u043c\u0438\u043d\u0443\u0442\u0430" ||
        u == u"\u043c\u0438\u043d\u0443\u0442\u044b") return 60000;
    if (u == u"h" || u == u"hr" || u == u"hrs" || u == u"hour" || u == u"hours" ||
        u == u"\u0447" || u == u"\u0447\u0430\u0441" || u == u"\u0447\u0430\u0441\u0430" || u == u"\u0447\u0430\u0441\u043e\u0432") return 3600000;
    if (u == u"d" || u == u"day" || u == u"days" || u == u"\u0434" || u == u"\u0434\u043d" ||
        u == u"\u0434\u0435\u043d\u044c" || u == u"\u0434\u043d\u044f" || u == u"\u0434\u043d\u0435\u0439" ||
        u == u"\u0441\u0443\u0442\u043a\u0438" || u == u"\u0441\u0443\u0442\u043e\u043a") return 86400000;
    if (u == u"w" || u == u"wk" || u == u"week" || u == u"weeks" ||
        u == u"\u043d\u0435\u0434" || u == u"\u043d\u0435\u0434\u0435\u043b\u044f" ||
        u == u"\u043d\u0435\u0434\u0435\u043b\u0438" || u == u"\u043d\u0435\u0434\u0435\u043b\u044c") return 604800000;
    return std::nullopt;
}
inline bool add(std::int64_t& total, std::int64_t amount, std::int64_t factor) noexcept {
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    if (amount < 0 || factor <= 0 || amount > (max - total) / factor) return false;
    total += amount * factor;
    return true;
}
inline std::optional<std::int64_t> number(std::u16string_view text, std::size_t& pos) {
    if (pos >= text.size() || !digit(text[pos])) return std::nullopt;
    std::int64_t result = 0;
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    while (pos < text.size() && digit(text[pos])) {
        const int d = static_cast<int>(text[pos++] - u'0');
        if (result > (max - d) / 10) return std::nullopt;
        result = result * 10 + d;
    }
    return result;
}
inline std::optional<std::int64_t> clock(std::u16string_view s) {
    // HH:MM:SS[.mmm] or MM:SS[.mmm], for relative durations.
    std::size_t i = 0;
    auto first = number(s, i);
    if (!first || i >= s.size() || s[i++] != u':') return std::nullopt;
    auto second = number(s, i);
    if (!second) return std::nullopt;
    std::int64_t h = 0, m = 0, sec = 0;
    bool includesHours = false;
    if (i < s.size() && s[i] == u':') {
        includesHours = true;
        ++i;
        auto third = number(s, i);
        if (!third) return std::nullopt;
        h = *first; m = *second; sec = *third;
    } else { m = *first; sec = *second; }
    if (m >= 60 && includesHours) return std::nullopt;
    if (sec >= 60) return std::nullopt;
    std::int64_t ms = 0;
    if (i < s.size() && (s[i] == u'.' || s[i] == u',')) {
        ++i;
        int fractionDigits = 0;
        while (i < s.size() && digit(s[i]) && fractionDigits < 3) {
            ms = ms * 10 + static_cast<int>(s[i++] - u'0');
            ++fractionDigits;
        }
        if (!fractionDigits || (i < s.size() && digit(s[i]))) return std::nullopt;
        while (fractionDigits++ < 3) ms *= 10;
    }
    while (i < s.size() && space(s[i])) ++i;
    if (i != s.size()) return std::nullopt;
    std::int64_t total = 0;
    if (!add(total, h, 3600000) || !add(total, m, 60000) ||
        !add(total, sec, 1000) || !add(total, ms, 1)) return std::nullopt;
    return total;
}
} // namespace detail

inline std::optional<std::int64_t> parse(std::u16string_view text) {
    while (!text.empty() && detail::space(text.front())) text.remove_prefix(1);
    while (!text.empty() && detail::space(text.back())) text.remove_suffix(1);
    if (text.empty()) return std::nullopt;
    if (text.find(u':') != std::u16string_view::npos) return detail::clock(text);
    std::int64_t total = 0;
    std::size_t pos = 0;
    bool found = false;
    while (pos < text.size()) {
        while (pos < text.size() && detail::separator(text[pos])) ++pos;
        if (pos == text.size()) break;
        auto quantity = detail::number(text, pos);
        if (!quantity) return std::nullopt;
        const std::size_t numberEnd = pos;
        while (pos < text.size() && detail::space(text[pos])) ++pos;
        const std::size_t unitStart = pos;
        while (pos < text.size() && detail::letter(text[pos])) ++pos;
        std::int64_t factor = 1; // A single plain number is milliseconds.
        if (pos > unitStart) {
            const auto multiplier = detail::unitFactor(text.substr(unitStart, pos - unitStart));
            if (!multiplier) return std::nullopt;
            factor = *multiplier;
        } else if (found || pos < text.size() || numberEnd != text.size()) {
            // A bare quantity mixed with named units is ambiguous.
            return std::nullopt;
        }
        if (!detail::add(total, *quantity, factor)) return std::nullopt;
        found = true;
        if (pos < text.size() && !detail::separator(text[pos]) && !detail::digit(text[pos]))
            return std::nullopt;
    }
    return found ? std::optional<std::int64_t>(total) : std::nullopt;
}

inline std::u16string format(std::int64_t milliseconds, bool russian) {
    if (milliseconds < 0) return {};
    if (milliseconds == 0) return russian ? u"0 мс" : u"0 ms";
    const std::int64_t factors[] = {604800000, 86400000, 3600000, 60000, 1000, 1};
    const char16_t* namesRu[] = {u"нед", u"д", u"ч", u"мин", u"с", u"мс"};
    const char16_t* namesEn[] = {u"w", u"d", u"h", u"min", u"s", u"ms"};
    std::u16string result;
    for (int i = 0; i < 6; ++i) {
        const auto n = milliseconds / factors[i];
        milliseconds %= factors[i];
        if (!n) continue;
        if (!result.empty()) result += u' ';
        const auto digits = std::to_string(n);
        for (const char c : digits) result.push_back(static_cast<char16_t>(c));
        result += u' ';
        result += russian ? namesRu[i] : namesEn[i];
    }
    return result;
}
} // namespace clicknova::duration
