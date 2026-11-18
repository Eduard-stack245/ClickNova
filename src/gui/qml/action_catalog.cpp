// SPDX-License-Identifier: MIT
#include "action_catalog.hpp"

#include <QStringList>
#include <type_traits>
#include <variant>

namespace clicknova::gui::qml {
namespace {
QString tr(bool uk, const char* english, const char* ukrainian) {
    return QString::fromUtf8(uk ? ukrainian : english);
}
QString mouseButton(input::Button button, bool uk) {
    switch (button) {
    case input::Button::Left: return tr(uk, "Left", "Ліва");
    case input::Button::Right: return tr(uk, "Right", "Права");
    case input::Button::Middle: return tr(uk, "Middle", "Середня");
    case input::Button::X1: return QStringLiteral("X1");
    case input::Button::X2: return QStringLiteral("X2");
    }
    return tr(uk, "Unknown", "Невідомо");
}
QString ms(std::int64_t value) { return QString::number(value) + QStringLiteral(" ms"); }
QString moveMode(input::MoveMode mode, bool uk) {
    switch (mode) {
    case input::MoveMode::Relative: return tr(uk, "relative", "відносно");
    case input::MoveMode::Absolute: return tr(uk, "screen", "екран");
    case input::MoveMode::WindowClient: return tr(uk, "window client", "область вікна");
    }
    return tr(uk, "unknown", "невідомо");
}
QString commandLabel(input::ElementCommand command, bool uk) {
    switch (command) {
    case input::ElementCommand::Activate: return tr(uk, "Activate", "Активувати");
    case input::ElementCommand::DoubleActivate: return tr(uk, "Double activate", "Подвійна активація");
    case input::ElementCommand::SetText: return tr(uk, "Set text", "Встановити текст");
    case input::ElementCommand::Scroll: return tr(uk, "Scroll element", "Прокрутити елемент");
    case input::ElementCommand::WaitVisible: return tr(uk, "Wait for element", "Чекати на елемент");
    case input::ElementCommand::AssertVisible: return tr(uk, "Check visibility", "Перевірити видимість");
    }
    return tr(uk, "Element operation", "Операція з елементом");
}
} // namespace

ActionSummary describeAction(const input::Action& action,
                             const input::StepWindow* binding,
                             bool ukrainian) {
    ActionSummary view;
    const auto name = [&](const char* en, const char* uk) { return tr(ukrainian, en, uk); };
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, input::Click>) {
            view.kind = QStringLiteral("click");
            view.title = name("Mouse click", "Клік мишею");
            view.details = QStringLiteral("%1 × %2 · %3").arg(QString::number(value.count),
                mouseButton(value.button, ukrainian), ms(value.intervalMs));
        } else if constexpr (std::is_same_v<T, input::ButtonDown>) {
            view.kind = QStringLiteral("mouseDown");
            view.title = name("Mouse button down", "Натиснути кнопку миші");
            view.details = mouseButton(value.button, ukrainian);
        } else if constexpr (std::is_same_v<T, input::ButtonUp>) {
            view.kind = QStringLiteral("mouseUp");
            view.title = name("Mouse button up", "Відпустити кнопку миші");
            view.details = mouseButton(value.button, ukrainian);
        } else if constexpr (std::is_same_v<T, input::MouseHold>) {
            view.kind = QStringLiteral("mouseHold");
            view.title = name("Hold mouse button", "Утримувати кнопку миші");
            view.details = mouseButton(value.button, ukrainian) + QStringLiteral(" · ") + ms(value.durationMs);
        } else if constexpr (std::is_same_v<T, input::Wheel>) {
            view.kind = QStringLiteral("scroll");
            view.title = name("Scroll wheel", "Прокрутка коліщатком");
            view.details = name(value.axis == input::Axis::Vertical ? "Vertical" : "Horizontal",
                                value.axis == input::Axis::Vertical ? "Вертикально" : "Горизонтально") +
                QStringLiteral(" · %1").arg(value.delta);
        } else if constexpr (std::is_same_v<T, input::KeyTap>) {
            view.kind = QStringLiteral("keyTap");
            view.title = name("Press key", "Натиснути клавішу");
            view.details = QStringLiteral("VK %1").arg(value.virtualKey);
        } else if constexpr (std::is_same_v<T, input::KeyDown>) {
            view.kind = QStringLiteral("keyDown");
            view.title = name("Key down", "Натиснути й утримувати клавішу");
            view.details = QStringLiteral("VK %1").arg(value.virtualKey);
        } else if constexpr (std::is_same_v<T, input::KeyUp>) {
            view.kind = QStringLiteral("keyUp");
            view.title = name("Key up", "Відпустити клавішу");
            view.details = QStringLiteral("VK %1").arg(value.virtualKey);
        } else if constexpr (std::is_same_v<T, input::KeyHold>) {
            view.kind = QStringLiteral("keyHold");
            view.title = name("Hold key", "Утримувати клавішу");
            view.details = QStringLiteral("VK %1 · %2").arg(QString::number(value.virtualKey), ms(value.durationMs));
        } else if constexpr (std::is_same_v<T, input::TypeText>) {
            view.kind = QStringLiteral("typeText");
            view.title = name("Type text", "Ввести текст");
            view.details = name("Text content hidden", "Вміст тексту приховано") +
                QStringLiteral(" · %1").arg(static_cast<qulonglong>(value.text.size()));
        } else if constexpr (std::is_same_v<T, input::Wait>) {
            view.kind = QStringLiteral("wait");
            view.title = name("Wait", "Пауза");
            view.details = ms(value.durationMs);
        } else if constexpr (std::is_same_v<T, input::MovePointer>) {
            view.kind = QStringLiteral("move");
            view.title = name("Move pointer", "Перемістити курсор");
            view.details = QStringLiteral("%1, %2 · %3 · %4").arg(
                QString::number(value.x), QString::number(value.y),
                moveMode(value.mode, ukrainian), ms(value.durationMs));
        } else if constexpr (std::is_same_v<T, input::RunScript>) {
            view.kind = QStringLiteral("script");
            view.title = QStringLiteral("PowerShell");
            view.details = name("Script content hidden", "Вміст скрипту приховано") +
                QStringLiteral(" · %1").arg(static_cast<qulonglong>(value.code.size()));
        } else if constexpr (std::is_same_v<T, input::WaitUntil>) {
            view.kind = QStringLiteral("waitUntil");
            view.title = name("Wait until time", "Чекати до часу");
            view.details = QStringLiteral("%1:%2:%3")
                .arg(value.hour, 2, 10, QLatin1Char('0'))
                .arg(value.minute, 2, 10, QLatin1Char('0'))
                .arg(value.second, 2, 10, QLatin1Char('0'));
        } else if constexpr (std::is_same_v<T, input::ElementTask>) {
            view.kind = QStringLiteral("element");
            view.title = name("UI element", "Елемент інтерфейсу");
            view.details = commandLabel(value.command, ukrainian);
            view.editable = false; // Native element inspector retains provider-specific configuration.
        }
    }, action);
    if (binding && !binding->title.empty()) {
        view.hasRequiredWindow = binding->enforce;
        view.details += QStringLiteral(" · ") + (binding->enforce
            ? name("Target window", "Цільове вікно")
            : name("Recorded window", "Записане вікно"));
    }
    return view;
}
} // namespace clicknova::gui::qml
