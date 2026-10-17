// SPDX-License-Identifier: Apache-2.0
#include "ui_icons.hpp"

#include <QAbstractButton>
#include <initializer_list>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSize>

namespace clicknova::gui {
QIcon icon(Icon kind, const QColor& color) {
    QPixmap pixmap(48, 48);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2.0);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const auto line = [&painter](qreal x1, qreal y1, qreal x2, qreal y2) {
        painter.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    const auto rect = [&painter](qreal x, qreal y, qreal w, qreal h, qreal r = 2.0) {
        painter.drawRoundedRect(QRectF(x, y, w, h), r, r);
    };
    const auto circle = [&painter](qreal x, qreal y, qreal r) {
        painter.drawEllipse(QPointF(x, y), r, r);
    };
    const auto path = [&painter](std::initializer_list<QPointF> points, bool close = false) {
        QPainterPath p;
        auto it = points.begin();
        if (it == points.end()) return;
        p.moveTo(*it++);
        for (; it != points.end(); ++it) p.lineTo(*it);
        if (close) p.closeSubpath();
        painter.drawPath(p);
    };
    switch (kind) {
    case Icon::Click:
        path({{5,2},{5,18},{9,14},{12.5,22},{16,20.5},{12.5,13},{19,13}}, true);
        line(15,2,15,6); line(20,7,16.5,9); line(9.5,1,9.5,4);
        break;
    case Icon::Cursor:
        path({{5,2},{5,20},{10,15},{13,22},{16,20.5},{13,14.5},{20,14}}, true);
        break;
    case Icon::Keyboard:
        rect(2,5,20,14,2.5);
        for (int row=0; row<2; ++row) for (int col=0; col<5; ++col)
            line(5+3.5*col,9+4*row,5.5+3.5*col,9+4*row);
        line(8,16,16,16);
        break;
    case Icon::Text:
        line(4,5,20,5); line(12,5,12,20); line(9,20,15,20);
        line(4,5,4,8); line(20,5,20,8);
        break;
    case Icon::Wait:
        circle(12,12,9); line(12,6,12,12); line(12,12,16,14);
        break;
    case Icon::ClockUntil:
        // Absolute clock time: small target mark distinguishes it from duration Wait.
        circle(11,12,8); line(11,7,11,12); line(11,12,15,12);
        painter.setBrush(color); circle(20,19,2.5);
        break;
    case Icon::Code:
        // Source code glyph, never the same letter-T icon used for typing text.
        path({{9,7},{4,12},{9,17}});
        path({{15,7},{20,12},{15,17}});
        line(13,5,11,19);
        break;
    case Icon::More:
        for (int i=0; i<3; ++i) { painter.setBrush(color); circle(5+7*i,12,1.2); }
        break;
    case Icon::Record:
        painter.setBrush(color); circle(12,12,7); break;
    case Icon::Play:
        painter.setBrush(color); path({{7,4},{20,12},{7,20}}, true); break;
    case Icon::Stop:
        painter.setBrush(color); rect(5,5,14,14,2); break;
    case Icon::Save:
        path({{4,3},{17,3},{21,7},{21,21},{3,21},{3,3}}, true);
        rect(7,3,10,7,0.8); rect(7,14,10,7,0.8);
        break;
    case Icon::Folder:
        path({{2,7},{2,20},{22,20},{22,9},{11,9},{9,6},{2,6}}, true); break;
    case Icon::Delete:
        line(5,6,19,6); line(9,3,15,3); rect(7,7,10,14,1.5);
        line(10,10,10,18); line(14,10,14,18);
        break;
    case Icon::Copy:
        rect(7,6,13,15,1); path({{4,17},{3,17},{3,3},{16,3},{16,5}}); break;
    case Icon::Up:
        line(12,20,12,4); path({{5,11},{12,4},{19,11}}); break;
    case Icon::Down:
        line(12,4,12,20); path({{5,13},{12,20},{19,13}}); break;
    case Icon::Target:
        circle(12,12,8); circle(12,12,3); line(12,1,12,6);
        line(12,18,12,23); line(1,12,6,12); line(18,12,23,12);
        break;
    case Icon::Window:
        rect(2,4,20,16,2); line(2,9,22,9);
        for(int i=0;i<3;++i) circle(5+3*i,6.5,0.35);
        break;
    case Icon::Settings:
        circle(12,12,4); circle(12,12,9);
        line(12,1,12,5); line(12,19,12,23);
        line(1,12,5,12); line(19,12,23,12);
        line(4.2,4.2,7,7); line(17,17,19.8,19.8);
        line(19.8,4.2,17,7); line(7,17,4.2,19.8);
        break;
    case Icon::List:
        for(int i=0;i<3;++i) {
            painter.setBrush(color); circle(4,6+6*i,1); painter.setBrush(Qt::NoBrush);
            line(8,6+6*i,21,6+6*i);
        }
        break;
    case Icon::Edit:
        path({{5,19},{4,22},{8,21},{20,9},{16,5}}, true);
        line(14,7,18,11);
        break;
    case Icon::Pause:
        painter.setBrush(color); rect(6,4,4,16,1); rect(14,4,4,16,1); break;
    case Icon::Close:
        line(5,5,19,19); line(19,5,5,19); break;
    case Icon::Check:
        path({{3,12},{9,18},{21,5}}); break;
    case Icon::Refresh:
        painter.drawArc(QRectF(4,4,16,16),30*16,300*16);
        path({{17,3},{21,6},{16,9}});
        break;
    case Icon::Drag:
        path({{4,5},{9,5},{9,2},{13,7},{9,12},{9,9},{4,9}}, true);
        path({{20,19},{15,19},{15,22},{11,17},{15,12},{15,15},{20,15}}, true);
        break;
    case Icon::Mouse:
        rect(6,2,12,20,6); line(12,2,12,11); line(6,11,18,11); break;
    case Icon::Undo:
        // Left-curved history arrow: never reuse the Move Up pictogram.
        path({{10,4},{4,9},{10,14}});
        line(4,9,14,9);
        painter.drawArc(QRectF(8,7,13,13), 225*16, 225*16);
        break;
    case Icon::Redo:
        // Right-curved history arrow, deliberately distinct from Move Down.
        path({{14,4},{20,9},{14,14}});
        line(20,9,10,9);
        painter.drawArc(QRectF(3,7,13,13), 90*16, 225*16);
        break;
    case Icon::MoveTop:
        line(4,4,20,4);
        line(12,20,12,8);
        path({{6,14},{12,8},{18,14}});
        break;
    case Icon::MoveBottom:
        line(4,20,20,20);
        line(12,4,12,16);
        path({{6,10},{12,16},{18,10}});
        break;
    case Icon::SelectAll:
        // Dashed selection border and check mark.
        painter.setPen(QPen(color, 1.7, Qt::DashLine, Qt::RoundCap));
        rect(3,3,18,18,2);
        painter.setPen(QPen(color, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        path({{7,12},{10.5,15.5},{17,8}});
        break;
    case Icon::Paste:
        rect(5,5,14,17,2);
        rect(9,2,6,6,1);
        line(8,12,16,12); line(8,16,14,16);
        break;
    case Icon::Profile:
        // Account/profile glyph: clearly identifies the scenario drawer.
        circle(12,8,4.0);
        painter.drawArc(QRectF(3,13,18,11), 0, 180*16);
        break;
    case Icon::Brand:
        // Scalable application mark; no external fonts or image files.
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#2774d9")));
        rect(1,1,22,22,5);
        painter.setPen(QPen(QColor(QStringLiteral("#ffffff")), 2.0,
                             Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(5,5,14,14), 60*16, 240*16);
        painter.setBrush(QColor(QStringLiteral("#ffffff")));
        path({{13.5,8},{19.5,12},{13.5,16}}, true);
        break;
    }
    painter.end();
    return QIcon(pixmap);
}

void setButtonIcon(QAbstractButton* button, Icon kind, const QColor& foreground) {
    if (!button) return;
    button->setIcon(icon(kind, foreground));
    button->setIconSize(QSize(19,19));
}
} // namespace clicknova::gui
