// SPDX-License-Identifier: MIT
#include "../src/gui/qml/action_catalog.hpp"
#include <QCoreApplication>
#include <vector>

using namespace clicknova::input;
using clicknova::gui::qml::describeAction;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::vector<Action> examples = {
        Click{Button::Left, 5, 120}, ButtonDown{}, ButtonUp{}, MouseHold{}, Wheel{},
        KeyTap{}, KeyDown{}, KeyUp{}, KeyHold{}, TypeText{u"PRIVATE-EXAMPLE"}, Wait{},
        MovePointer{}, RunScript{u"SECRET-SCRIPT-CONTENT"}, WaitUntil{}, ElementTask{}
    };
    if (examples.size() != 15) return 1;
    for (const auto& action : examples) {
        const auto en = describeAction(action, nullptr, false);
        const auto uk = describeAction(action, nullptr, true);
        if (en.kind.isEmpty() || en.title.isEmpty() || en.details.isEmpty() ||
            uk.kind != en.kind || uk.title.isEmpty() || uk.details.isEmpty()) return 2;
        if (en.details.contains(QStringLiteral("PRIVATE-EXAMPLE")) ||
            en.details.contains(QStringLiteral("SECRET-SCRIPT-CONTENT")) ||
            uk.details.contains(QStringLiteral("PRIVATE-EXAMPLE")) ||
            uk.details.contains(QStringLiteral("SECRET-SCRIPT-CONTENT"))) return 3;
    }
    if (describeAction(examples.front(), nullptr, false).details != QStringLiteral("5 × Left · 120 ms")) return 4;
    if (describeAction(examples.back(), nullptr, true).editable) return 5;
    StepWindow window;
    window.title = "Private document title";
    window.enforce = true;
    const auto bound = describeAction(examples.front(), &window, true);
    if (!bound.hasRequiredWindow || !bound.details.contains(QStringLiteral("Цільове вікно")) ||
        bound.details.contains(QStringLiteral("Private document title"))) return 6;
    window.enforce = false;
    const auto observed = describeAction(examples.front(), &window, false);
    if (observed.hasRequiredWindow || !observed.details.contains(QStringLiteral("Recorded window"))) return 7;
    return 0;
}
