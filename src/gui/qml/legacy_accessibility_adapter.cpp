// SPDX-License-Identifier: MIT
#include "legacy_accessibility_adapter.hpp"
#include "../input_lab.hpp"
#include <QAbstractButton>
#include <QMainWindow>

namespace {
using Operation = clicknova::accessibility::discovery::Operation;
QAbstractButton* control(QMainWindow* window, Operation operation) {
    return window ? window->findChild<QAbstractButton*>(operation == Operation::Find
        ? QStringLiteral("inputLabSearchUiElements")
        : QStringLiteral("inputLabClearUiElement")) : nullptr;
}
}
LegacyAccessibilityAdapter::LegacyAccessibilityAdapter(QMainWindow* legacy)
    : legacy_(legacy) {}

bool LegacyAccessibilityAdapter::available(Operation operation) const {
    auto* button = control(legacy_.data(), operation);
    return button && button->isEnabled();
}

bool LegacyAccessibilityAdapter::invoke(Operation operation, int expectedRow) {
    auto* window = legacy_.data();
    auto* editor = window ? dynamic_cast<InputLabWidget*>(window->findChild<QWidget*>(QStringLiteral("inputLab"))) : nullptr;
    auto* button = control(window, operation);
    // Avoid applying a picker result to the wrong row after a QML signal or
    // another native callback changed selection inside the event loop.
    if (!editor || editor->selectedActionRowForPresentation() != expectedRow ||
        !editor->canEditActionFromQml(expectedRow) || !button || !button->isEnabled()) return false;
    button->click();
    // A click only confirms dispatch. Cancellation in the original modal
    // picker is a valid outcome and must not be advertised as "found".
    return true;
}
