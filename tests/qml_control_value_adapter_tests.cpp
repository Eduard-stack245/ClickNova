// SPDX-License-Identifier: MIT
#include "../src/gui/qml/control_value_adapter.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTimeEdit>
#include <QtGlobal>

using clicknova::gui::qml::ControlValueAdapter;
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QSpinBox integer;
    integer.setRange(10, 50);
    if (ControlValueAdapter::write(&integer, 9) || integer.value() != 10) return 1;
    if (!ControlValueAdapter::write(&integer, 42) || integer.value() != 42) return 2;
    if (ControlValueAdapter::write(&integer, 51) || integer.value() != 42) return 3;
    if (ControlValueAdapter::write(&integer, 23.7) || integer.value() != 42) return 20;
    if (ControlValueAdapter::read(&integer).toInt() != 42) return 4;
    QComboBox choice;
    choice.addItems({QStringLiteral("A"), QStringLiteral("B")});
    if (ControlValueAdapter::write(&choice, 4) || choice.currentIndex() != 0) return 5;
    if (!ControlValueAdapter::write(&choice, 1) || choice.currentIndex() != 1) return 6;
    QLineEdit input;
    input.setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]+")), &input));
    input.setMaxLength(5);
    if (ControlValueAdapter::write(&input, QStringLiteral("12x")) || !input.text().isEmpty()) return 7;
    if (ControlValueAdapter::write(&input, QStringLiteral("123456"))) return 8;
    if (!ControlValueAdapter::write(&input, QStringLiteral("123")) || input.text() != QStringLiteral("123")) return 9;
    QCheckBox toggle;
    if (ControlValueAdapter::write(&toggle, QStringLiteral("false"))) return 10;
    if (!ControlValueAdapter::write(&toggle, true) || !toggle.isChecked()) return 11;
    QTimeEdit time;
    time.setDisplayFormat(QStringLiteral("HH:mm:ss"));
    if (ControlValueAdapter::write(&time, QStringLiteral("25:00:00"))) return 12;
    if (!ControlValueAdapter::write(&time, QStringLiteral("12:30:00"))) return 13;
    if (ControlValueAdapter::read(&time).toString() != QStringLiteral("12:30:00")) return 14;
    QPlainTextEdit text;
    text.setReadOnly(true);
    if (ControlValueAdapter::write(&text, QStringLiteral("secret"))) return 15;
    text.setReadOnly(false);
    if (!ControlValueAdapter::write(&text, QStringLiteral("hello"))) return 16;
    if (ControlValueAdapter::read(&text).toString() != QStringLiteral("hello")) return 17;
    QDoubleSpinBox decimal;
    decimal.setRange(0.0, 1.0);
    if (ControlValueAdapter::write(&decimal, 1.5)) return 18;
    if (!ControlValueAdapter::write(&decimal, 0.5)) return 19;
    return 0;
}
