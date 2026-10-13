// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QAbstractItemView>
#include <QDropEvent>
#include <QDrag>
#include <QAbstractItemModel>
#include <QTableWidget>
#include <algorithm>
#include <functional>
#include <vector>

// Move the underlying Action list rather than reordering QTableWidgetItems.
// QTableWidget's default InternalMove would otherwise desynchronize playback.
class ActionTableWidget final : public QTableWidget {
public:
    using QTableWidget::QTableWidget;
    std::function<void(int, int)> requestMove;
    std::function<void(const std::vector<int>&, int)> requestMoveRows;

protected:
    // QAbstractItemView::startDrag may delete model rows after a Move drop.
    // We must bypass that cleanup: the callback owns the action model and
    // refreshes the table after each successful reorder.
    void startDrag(Qt::DropActions) override {
        const auto indexes = selectedIndexes();
        if (indexes.isEmpty()) return;
        auto* dragMimeData = model()->mimeData(indexes);
        if (!dragMimeData) return;
        draggedRow_ = currentRow();
        draggedRows_.clear();
        for (const auto& index : selectionModel()->selectedRows()) draggedRows_.push_back(index.row());
        std::sort(draggedRows_.begin(), draggedRows_.end());
        QDrag drag(this);
        drag.setMimeData(dragMimeData);
        drag.exec(Qt::MoveAction);
        draggedRow_ = -1;
        draggedRows_.clear();
    }

    void dropEvent(QDropEvent* event) override {
        if (event->source() != this || (!requestMove && !requestMoveRows) || draggedRow_ < 0) {
            event->ignore();
            return;
        }
        const int from = draggedRow_;
        int insertion = rowAt(event->position().toPoint().y());
        if (insertion < 0 || dropIndicatorPosition() == QAbstractItemView::OnViewport) {
            insertion = rowCount();
        } else if (dropIndicatorPosition() == QAbstractItemView::BelowItem) {
            ++insertion;
        } else if (dropIndicatorPosition() == QAbstractItemView::OnItem) {
            // Dropping on the upper half inserts before, lower half after.
            const QRect rowRect = visualRect(model()->index(insertion, 0));
            if (event->position().y() >= rowRect.center().y()) ++insertion;
        }
        if (requestMoveRows && !draggedRows_.empty()) {
            requestMoveRows(draggedRows_, insertion);
        } else {
            const int to = std::clamp(insertion > from ? insertion - 1 : insertion, 0, rowCount() - 1);
            if (to != from && requestMove) requestMove(from, to);
        }
        event->acceptProposedAction();
    }

private:
    int draggedRow_{-1};
    std::vector<int> draggedRows_;
};
