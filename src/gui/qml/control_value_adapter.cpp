// SPDX-License-Identifier: MIT
#include "control_value_adapter.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTimeEdit>
#include <QValidator>
#include <QMetaType>
#include <QtGlobal>
#include <cmath>

namespace clicknova::gui::qml {
QVariant ControlValueAdapter::read(const QObject* source) {
    if (const auto* choice = qobject_cast<const QComboBox*>(source)) return choice->currentIndex();
    if (const auto* input = qobject_cast<const QSpinBox*>(source)) return input->value();
    if (const auto* doubleInput = qobject_cast<const QDoubleSpinBox*>(source)) return doubleInput->value();
    if (const auto* box = qobject_cast<const QCheckBox*>(source)) return box->isChecked();
    if (const auto* lineInput = qobject_cast<const QLineEdit*>(source)) return lineInput->text();
    if (const auto* plainInput = qobject_cast<const QPlainTextEdit*>(source)) return plainInput->toPlainText();
    if (const auto* timeInput = qobject_cast<const QTimeEdit*>(source))
        return timeInput->time().toString(QStringLiteral("HH:mm:ss"));
    return {};
}

bool ControlValueAdapter::write(QObject* target, const QVariant& requested) {
    if (!target || !requested.isValid()) return false;
    if (auto* choice = qobject_cast<QComboBox*>(target)) {
        bool valid = false;
        const int index = requested.toInt(&valid);
        if (!valid || index < 0 || index >= choice->count()) return false;
        choice->setCurrentIndex(index);
    } else if (auto* integerInput = qobject_cast<QSpinBox*>(target)) {
        bool numeric = false;
        const double source = requested.toDouble(&numeric);
        if (!numeric || !qIsFinite(source) || std::trunc(source) != source) return false;
        bool valid = false;
        const int value = requested.toInt(&valid);
        if (!valid || value < integerInput->minimum() || value > integerInput->maximum()) return false;
        integerInput->setValue(value);
    } else if (auto* doubleInput = qobject_cast<QDoubleSpinBox*>(target)) {
        bool valid = false;
        const double value = requested.toDouble(&valid);
        if (!valid || !qIsFinite(value) || value < doubleInput->minimum() || value > doubleInput->maximum()) return false;
        doubleInput->setValue(value);
    } else if (auto* box = qobject_cast<QCheckBox*>(target)) {
        // Reject strings such as "false" that QVariant::toBool interprets as true.
        if (requested.metaType().id() != QMetaType::Bool) return false;
        box->setChecked(requested.toBool());
    } else if (auto* lineInput = qobject_cast<QLineEdit*>(target)) {
        if (lineInput->isReadOnly()) return false;
        const QString original = requested.toString();
        if (original.size() > lineInput->maxLength()) return false;
        if (const auto* validator = lineInput->validator()) {
            QString candidate = original;
            int position = candidate.size();
            if (validator->validate(candidate, position) != QValidator::Acceptable || candidate != original)
                return false;
        }
        lineInput->setText(original);
    } else if (auto* plainInput = qobject_cast<QPlainTextEdit*>(target)) {
        if (plainInput->isReadOnly()) return false;
        plainInput->setPlainText(requested.toString());
    } else if (auto* timeInput = qobject_cast<QTimeEdit*>(target)) {
        const QTime time = QTime::fromString(requested.toString(), QStringLiteral("HH:mm:ss"));
        if (!time.isValid() || time.toString(QStringLiteral("HH:mm:ss")) != requested.toString()) return false;
        timeInput->setTime(time);
    } else return false;
    return true;
}
} // namespace clicknova::gui::qml
