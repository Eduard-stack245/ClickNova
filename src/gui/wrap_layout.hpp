// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QLayout>
#include <QList>

// A small QWidget layout for toolbars that must stay usable in narrow windows.
// Buttons wrap to a new line instead of extending beyond a QScrollArea viewport.
class WrapLayout final : public QLayout {
public:
    explicit WrapLayout(QWidget* parent = nullptr, int spacing = 8);
    ~WrapLayout() override;

    void addItem(QLayoutItem* item) override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QSize minimumSize() const override;

private:
    int arrange(const QRect& rect, bool measureOnly) const;
    QList<QLayoutItem*> items_;
    int spacing_;
};
