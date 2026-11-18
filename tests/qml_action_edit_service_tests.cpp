// SPDX-License-Identifier: MIT
#include "../src/gui/qml/action_edit_service.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <variant>
#include <vector>

using namespace clicknova::input;
using namespace clicknova::gui::qml::action_edit;

int main() {
    const std::vector<Action> types = {
        Click{}, KeyTap{}, KeyDown{}, KeyUp{}, ButtonDown{}, ButtonUp{}, Wheel{},
        Wait{}, MouseHold{}, KeyHold{}, TypeText{}, MovePointer{}, RunScript{}, WaitUntil{}
    };
    for (int i = 0; i < 14; ++i) {
        if (editorType(types[static_cast<std::size_t>(i)]) != i) return 1;
        if (read(types[static_cast<std::size_t>(i)], "inputLabType") !=
            std::u16string(1, static_cast<char16_t>(u'0' + (i < 10 ? i : 0))) && i < 10) return 2;
        if (apply(types[static_cast<std::size_t>(i)], "inputLabType", u"-1")) return 3;
        if (apply(types[static_cast<std::size_t>(i)], "inputLabType", u"14")) return 4;
    }
    const auto assertValue = [&](const Action& a, std::string_view field, std::u16string_view value,
                                 std::u16string_view expected) {
        const auto changed = apply(a, field, value);
        return changed && changed.changed && read(changed.action, field) == expected &&
            read(a, field) != read(changed.action, field);
    };
    if (!assertValue(Click{}, "inputLabClickCount", u"9223372036854775807", u"9223372036854775807")) return 5;
    if (apply(Click{}, "inputLabClickCount", u"9223372036854775808")) return 6;
    if (apply(Click{}, "inputLabClickCount", u"-2")) return 7;
    if (apply(Click{}, "inputLabClickCount", u"1.5")) return 8;
    if (apply(Click{}, "inputLabClickCount", u" 3")) return 9;
    if (!assertValue(Click{}, "inputLabClickIntervalMs", u"60000", u"60000")) return 10;
    if (apply(Click{}, "inputLabClickIntervalMs", u"60001")) return 11;
    if (!assertValue(Click{}, "inputLabMouseButton", u"4", u"4")) return 12;
    if (apply(Click{}, "inputLabMouseButton", u"5")) return 13;
    if (!assertValue(KeyTap{}, "inputLabVirtualKey", u"254", u"254")) return 14;
    if (apply(KeyTap{}, "inputLabVirtualKey", u"7")) return 15;
    if (!assertValue(MouseHold{}, "inputLabHoldMs", u"60000", u"60000")) return 16;
    if (apply(MouseHold{}, "inputLabHoldMs", u"0")) return 17;
    if (!assertValue(Wheel{}, "inputLabWheelDelta", u"-1200", u"-1200")) return 18;
    if (!assertValue(Wheel{}, "inputLabWheelAxis", u"1", u"1")) return 19;
    if (!assertValue(MovePointer{}, "inputLabMoveX", u"-32768", u"-32768")) return 20;
    if (!assertValue(MovePointer{}, "inputLabMoveY", u"32767", u"32767")) return 21;
    if (apply(MovePointer{}, "inputLabMoveX", u"32768")) return 22;
    if (!assertValue(MovePointer{}, "inputLabMoveMode", u"2", u"2")) return 23;
    if (!assertValue(Wait{}, "inputLabWaitExpression", u"2s", u"2000 ms")) return 24;
    if (apply(Wait{}, "inputLabWaitExpression", u"garbage")) return 25;
    if (!assertValue(WaitUntil{}, "inputLabWaitUntilClock", u"23:59:59", u"23:59:59")) return 26;
    if (apply(WaitUntil{}, "inputLabWaitUntilClock", u"24:00:00")) return 27;
    if (apply(WaitUntil{}, "inputLabWaitUntilClock", u"12:60:00")) return 28;
    if (!assertValue(TypeText{}, "inputLabText", u"Private input", u"Private input")) return 29;
    if (!assertValue(RunScript{}, "inputLabScript", u"Write-Output ok", u"Write-Output ok")) return 30;
    if (apply(TypeText{}, "inputLabText", std::u16string(65537, u'x'))) return 31;
    if (apply(Click{}, "inputLabText", u"wrong kind")) return 32;
    if (apply(Click{}, "inputLabUnknown", u"1")) return 33;
    if (read(ElementTask{}, "inputLabType")) return 34;
    if (apply(ElementTask{}, "inputLabType", u"0")) return 35;
    if (apply(Click{}, "inputLabClickCount", u"1").changed) return 36;
    const auto typeChange = apply(Click{}, "inputLabType", u"7");
    if (!typeChange || !typeChange.changed || !std::holds_alternative<Wait>(typeChange.action)) return 37;
    const auto unchanged = apply(Click{Button::Left, 1, 200}, "inputLabClickCount", u"1");
    if (!unchanged || unchanged.changed) return 38;
    // Invalid field input must never change the caller-owned action, even when
    // rapidly entering malformed values or repeatedly switching action types.
    Action stable = Click{Button::Left, 14, 260};
    for (int iteration = 0; iteration < 1000; ++iteration) {
        const std::u16string malformed = u"12x" + std::u16string(iteration % 7, u'-');
        const auto rejected = apply(stable, "inputLabClickCount", malformed);
        if (rejected || rejected.changed ||
            std::get<Click>(rejected.action).count != 14 || std::get<Click>(stable).count != 14) return 39;
    }
    const auto negativeLimit = apply(Click{}, "inputLabClickCount", u"-9223372036854775808");
    if (negativeLimit || negativeLimit.changed) return 40;
    std::cout << "Action edit service: 40 checks + 1000 malformed-input cases passed\n";
    return 0;
}
