// SPDX-License-Identifier: Apache-2.0
#include "wrap_layout.hpp"

#include <QLayoutItem>
#include <QWidget>
#include <algorithm>

WrapLayout::WrapLayout(QWidget* parent, int spacing)
    : QLayout(parent), spacing_(spacing) {
    setContentsMargins(0, 0, 0, 0);
}

WrapLayout::~WrapLayout() {
    while (auto* item = takeAt(0)) delete item;
}

void WrapLayout::addItem(QLayoutItem* item) { items_.append(item); }
int WrapLayout::count() const { return items_.size(); }
QLayoutItem* WrapLayout::itemAt(int index) const { return items_.value(index, nullptr); }
QLayoutItem* WrapLayout::takeAt(int index) {
    if (index < 0 || index >= items_.size()) return nullptr;
    return items_.takeAt(index);
}
Qt::Orientations WrapLayout::expandingDirections() const { return {}; }
bool WrapLayout::hasHeightForWidth() const { return true; }
int WrapLayout::heightForWidth(int width) const {
    return arrange(QRect(0, 0, width, 0), true);
}
void WrapLayout::setGeometry(const QRect& rect) {
    QLayout::setGeometry(rect);
    arrange(rect, false);
}
QSize WrapLayout::sizeHint() const { return minimumSize(); }
QSize WrapLayout::minimumSize() const {
    QSize result;
    for (const auto* item : items_) result = result.expandedTo(item->minimumSize());
    const QMargins margins = contentsMargins();
    return result + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

int WrapLayout::arrange(const QRect& rect, bool measureOnly) const {
    const QMargins margins = contentsMargins();
    const QRect space = rect.adjusted(margins.left(), margins.top(),
                                      -margins.right(), -margins.bottom());
    int x = space.x();
    int y = space.y();
    int lineHeight = 0;
    for (auto* item : items_) {
        if (item->isEmpty()) continue;
        const QSize size = item->sizeHint();
        if (lineHeight > 0 && x + size.width() > space.right() + 1) {
            x = space.x();
            y += lineHeight + spacing_;
            lineHeight = 0;
        }
        if (!measureOnly) item->setGeometry(QRect(QPoint(x, y), size));
        x += size.width() + spacing_;
        lineHeight = std::max(lineHeight, size.height());
    }
    return y + lineHeight - rect.y() + margins.bottom();
}
