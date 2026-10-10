// SPDX-License-Identifier: Apache-2.0
#include "wheel_scroll_filter.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidget>

namespace {
class WheelScrollFilter final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::Wheel) return false;
        auto* control = qobject_cast<QWidget*>(watched);
        if (!control || (!qobject_cast<QAbstractSpinBox*>(control) &&
                         !qobject_cast<QComboBox*>(control))) return false;

        // A combo popup is a separate view: its list can still be scrolled.
        // Only the collapsed combo and numeric editors are protected.
        auto* wheel = static_cast<QWheelEvent*>(event);
        for (QWidget* parent = control->parentWidget(); parent; parent = parent->parentWidget()) {
            auto* scroll = qobject_cast<QScrollArea*>(parent);
            if (!scroll || !scroll->verticalScrollBar() ||
                scroll->verticalScrollBar()->maximum() == scroll->verticalScrollBar()->minimum()) continue;

            auto* viewport = scroll->viewport();
            QWheelEvent redirected(
                QPointF(viewport->mapFromGlobal(wheel->globalPosition().toPoint())),
                wheel->globalPosition(), wheel->pixelDelta(), wheel->angleDelta(),
                wheel->buttons(), wheel->modifiers(), wheel->phase(),
                wheel->inverted(), wheel->source());
            QCoreApplication::sendEvent(viewport, &redirected);
            break;
        }
        // Even without a scrollable parent, never modify the value on hover.
        return true;
    }
};
} // namespace

void installWheelScrollFilter(QApplication& app) {
    app.installEventFilter(new WheelScrollFilter(&app));
}
