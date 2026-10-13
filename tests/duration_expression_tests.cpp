// SPDX-License-Identifier: Apache-2.0
#include "clicknova/duration_expression.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <array>
#include <cstdint>
#include <limits>
#include <string>

// Regression: a Windows ANSI codepage must never corrupt UTF-8 Cyrillic
// in a char16_t literal. Fails at compilation without MSVC /utf-8 instead
// of giving a misleading parser failure at runtime.
static_assert(u'м' == u'\u043c', "Compile all source files as UTF-8 (/utf-8 for MSVC)");
static_assert(u'с' == u'\u0441', "Compile all source files as UTF-8 (/utf-8 for MSVC)");

int main() {
    using clicknova::duration::parse;
    using clicknova::duration::format;
    assert(parse(u"0") == 0);
    assert(parse(u"500") == 500);
    assert(parse(u"250 \u043c\u0441") == 250); // Source-charset-independent reference.
    assert(parse(u"250 мс") == 250);
    assert(parse(u"1ч 2мин 3с 250мс") == 3723250);
    assert(parse(u"2 часа 15 минут 8 секунд 250 миллисекунд") == 8108250);
    assert(parse(u"1h30min50ms") == 5400050);
    assert(parse(u"2h 15m 8s 250ms") == 8108250);
    assert(parse(u"1w 2d 3h 4min 5s 6ms") == 788645006);
    assert(parse(u"1 д + 2 ч, 3 мин; 4 сек") == 93784000);
    assert(parse(u"0:01:30.005") == 90005);
    assert(parse(u"01:30,250") == 90250);
    assert(parse(u"48:00:00") == 172800000);
    assert(!parse(u""));
    assert(!parse(u"-3s"));
    assert(!parse(u"2 hours pizza"));
    assert(!parse(u"1s 5"));
    assert(!parse(u"1.5h"));
    assert(!parse(u"1:99:00"));
    assert(!parse(u"1:00:61"));
    assert(!parse(u"2:01.1234"));
    assert(!parse(u"999999999999999999999999999999999999999h"));
    assert(!parse(u"9223372036854775807d"));
    assert(parse(u"9223372036854775807ms") == std::numeric_limits<std::int64_t>::max());
    assert(!parse(u"9223372036854775807ms 1ms"));
    for (const auto amount : std::array<std::int64_t, 8>{0, 1, 500, 1000, 3723250,
                              86400000, 788645006, std::numeric_limits<std::int64_t>::max()}) {
        const auto formatted = format(amount, true);
        assert(parse(formatted) == amount);
        assert(parse(format(amount, false)) == amount);
    }
}
