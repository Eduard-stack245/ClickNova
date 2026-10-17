// SPDX-License-Identifier: Apache-2.0
#include "wheel_scroll_filter.hpp"

#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTableWidget>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {
bool isInside(const QWidget* child, const QWidget* parent) {
    // Qt top-level dialogs and combo popups can retain a QObject/QWidget
    // parent in the main window. Their wheel events must NEVER be treated as
    // wheel events in that parent's scroll area: doing so makes a search
    // results dialog appear unscrollable even after the user clicks it.
    for (auto* widget = child; widget; widget = widget->parentWidget()) {
        if (widget == parent) return true;
        if (widget->isWindow()) break;
    }
    return false;
}

// Send wheel movement to the scroll BAR belonging to the explicitly clicked
// area, rather than synthesizing a new wheel event for a different viewport.
// Qt may discard or propagate synthetic nested viewport events differently
// across styles/platforms; setting the scrollbar is deterministic and prevents
// the non-active area from scrolling even at the active area's boundaries.
void scrollArea(QAbstractScrollArea* area, const QWheelEvent& wheel) {
    if (!area) return;
    const QPoint pixels = wheel.pixelDelta();
    const QPoint angles = wheel.angleDelta();
    const bool horizontal = wheel.modifiers().testFlag(Qt::ShiftModifier) ||
        (angles.y() == 0 && pixels.y() == 0 && (angles.x() != 0 || pixels.x() != 0));
    QScrollBar* bar = horizontal ? area->horizontalScrollBar() : area->verticalScrollBar();
    if (!bar) return;

    // Mouse wheels use 120 units per notch; a table can scroll by items while
    // a page scrolls by pixels, so respect each scrollbar's singleStep().
    // Touchpads provide pixelDelta() and must also work when angleDelta() is 0.
    const int pixelDelta = horizontal ? (pixels.x() ? pixels.x() : pixels.y()) : pixels.y();
    const int angleDelta = horizontal ? (angles.x() ? angles.x() : angles.y()) : angles.y();
    int distance = 0;
    if (pixelDelta != 0) {
        distance = pixelDelta;
    } else if (angleDelta != 0) {
        const double lines = static_cast<double>(std::max(1, QApplication::wheelScrollLines()));
        distance = static_cast<int>(std::lround(static_cast<double>(angleDelta) / 120.0 *
            lines * std::max(1, bar->singleStep())));
        // Small high-resolution deltas must not disappear after rounding.
        if (distance == 0) distance = angleDelta > 0 ? 1 : -1;
    }
    if (distance != 0) bar->setValue(bar->value() - distance);
}

class WheelScrollFilter final : public QObject {
public:
    using QObject::QObject;

    void setStepScrollTargets(QTableWidget* steps, QScrollArea* page) {
        steps_ = steps;
        page_ = page;
        stepsActive_ = false;
    }
    void resetStepSelection() { stepsActive_ = false; }
    void setPageStack(QStackedWidget* pages) { pages_ = pages; }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (!event) return false;
        auto* widget = qobject_cast<QWidget*>(watched);
        if (event->type() == QEvent::MouseButtonPress && widget &&
            page_ && page_->isVisible() && steps_) {
            // A click anywhere in ClickNova's main window (including the
            // surrounding toolbar) selects the page; only a click inside the
            // action table selects Steps. Hover NEVER changes this choice.
            // Dialogs and popup menus belong to another top-level window.
            if (pages_ && isInside(widget, pages_->window()))
                stepsActive_ = isInside(widget, steps_);
            return false;
        }
        if (event->type() != QEvent::Wheel || !widget) return false;
        const auto* wheel = static_cast<QWheelEvent*>(event);

        // Always scroll the active workspace, including when the pointer is
        // over its labels, cards, buttons, editors or empty background. Native
        // Qt wheel propagation depends on the control and Windows style; a
        // focused combo or spinbox otherwise consumes the event. Only the
        // Steps workspace has two independently scrollable destinations.
        if (pages_) {
            for (int i = 0; i < pages_->count(); ++i) {
                auto* page = qobject_cast<QScrollArea*>(pages_->widget(i));
                if (!page || !page->isVisible() || !isInside(widget, page)) continue;
                QAbstractScrollArea* target = page;
                if (page == page_ && steps_ && stepsActive_)
                    target = steps_;
                scrollArea(target, *wheel);
                event->accept();
                return true; // Do not leak scroll when the target hits its end.
            }
        }

        // Keep compatibility with separately embedded scroll pages. Unlike
        // ordinary child widgets, popup selector views are separate windows
        // and their native scrolling must remain untouched.
        if (!qobject_cast<QAbstractSpinBox*>(widget) &&
            !qobject_cast<QComboBox*>(widget)) return false;
        if (widget->isWindow()) return false;
        for (QWidget* parent = widget->parentWidget(); parent; parent = parent->parentWidget()) {
            // A modal dialog can be QObject-parented to a scrollable page.
            // Do not climb across that dialog's top-level window boundary.
            if (parent->isWindow()) break;
            auto* scroll = qobject_cast<QScrollArea*>(parent);
            if (!scroll) continue;
            scrollArea(scroll, *wheel);
            event->accept();
            return true;
        }
        return false;
    }

private:
    QPointer<QTableWidget> steps_;
    QPointer<QScrollArea> page_;
    QPointer<QStackedWidget> pages_;
    bool stepsActive_{false};
};
QPointer<WheelScrollFilter> globalFilter;
} // namespace

void installWheelScrollFilter(QApplication& app) {
    globalFilter = new WheelScrollFilter(&app);
    app.installEventFilter(globalFilter);
}

void configureStepWheelRouting(QTableWidget* table, QScrollArea* page) {
    if (globalFilter) globalFilter->setStepScrollTargets(table, page);
}

void resetStepWheelRouting() {
    if (globalFilter) globalFilter->resetStepSelection();
}

void configurePageWheelRouting(QStackedWidget* pages) {
    if (globalFilter) globalFilter->setPageStack(pages);
}
