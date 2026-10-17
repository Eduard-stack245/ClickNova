// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QIcon>

class QAbstractButton;

namespace clicknova::gui {
// One icon vocabulary is shared by all windows; no icon font or assets required.
enum class Icon {
    Click, Keyboard, Text, Wait, More, Record, Play, Stop, Save, Folder,
    Delete, Copy, Up, Down, Cursor, Target, Window, Settings, List,
    Edit, Pause, Close, Check, Refresh, Drag, Mouse, Undo, Redo,
    MoveTop, MoveBottom, SelectAll, Paste, Brand, Profile, ClockUntil, Code
};

QIcon icon(Icon kind, const QColor& foreground = QColor(QStringLiteral("#dae5f5")));
void setButtonIcon(QAbstractButton* button, Icon kind,
                   const QColor& foreground = QColor(QStringLiteral("#dae5f5")));
} // namespace clicknova::gui
