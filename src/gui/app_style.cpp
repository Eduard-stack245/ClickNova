// SPDX-License-Identifier: Apache-2.0
#include "app_style.hpp"
#include <QApplication>

void applyClickNovaStyle(QApplication& app) {
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(QStringLiteral(R"(
        QWidget { background: #171a20; color: #e9eef6; font-size: 13px; }
        QLabel#heading { font-size: 25px; font-weight: 700; color: #81aaff; }
        QLabel#pageHeading { font-size: 21px; font-weight: 700; color: #e9eef6; }
        QListWidget#navigation { border: 1px solid #303947; border-radius: 10px; background: #1d222b; padding: 8px; }
        QListWidget#navigation::item { border-radius: 7px; padding: 13px 9px; }
        QListWidget#navigation::item:selected { background: #294e92; color: #ffffff; }
        QListWidget#navigation::item:hover:!selected { background: #293341; }
        QTabWidget::pane { border: 1px solid #394250; border-radius: 8px; }
        QTabBar::tab { padding: 11px 18px; border: 1px solid #394250;
                       background: #252d3a; color: #d5dfec; }
        QTabBar::tab:selected { background: #315aa3; color: white; }
        QTableWidget { background: #1d222b; alternate-background-color: #242b36;
                       gridline-color: #384455; border: 1px solid #394250; border-radius: 7px; }
        QTableWidget::item { padding: 6px; }
        QTableWidget::item:selected { background: #294e92; color: white; }
        QHeaderView::section { background: #293341; border: 0; border-bottom: 1px solid #485467;
                               padding: 9px; color: #d6e1f0; }
        QPushButton#sequenceAddPoint { background: #285bd6; color: white; font-weight: 700; }
        QToolButton#sequenceEditToggle { border: 1px solid #4b5973; padding: 10px;
                                         border-radius: 7px; background: #293341; }
        QToolButton#sequenceEditToggle:checked { background: #315aa3; }
        QToolButton#sequenceWindowSettingsToggle { border: 1px solid #394250; border-radius: 6px;
                                                   background: #252d3a; padding: 8px 12px; }
        QLabel#subheading { color: #aab5c5; }
        QLabel#caution { color: #ebc581; }
        QLabel#workflowFeedback { background: #25334a; color: #dae9ff;
                                  border: 1px solid #5477a9; border-radius: 7px;
                                  padding: 8px 12px; }
        QGroupBox { border: 1px solid #394250; border-radius: 10px; margin-top: 12px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; }
        QPushButton { border: 1px solid #4b5973; padding: 12px; border-radius: 7px; background: #293341; }
        QPushButton#primaryButton { background: #285bd6; color: white; font-weight: bold; }
        QPushButton:hover:enabled { background: #3763ab; }
        QPushButton:disabled { color: #7d8998; }
        QScrollArea { border: 0; }
        QComboBox, QSpinBox, QKeySequenceEdit { border: 1px solid #4b5973; padding: 6px; border-radius: 6px; background: #242a35; }
        QCheckBox { padding: 4px; }
        QToolTip { background: #293341; color: #f0f5ff; border: 1px solid #6386b5;
                   padding: 7px; border-radius: 5px; }
    )"));
}
