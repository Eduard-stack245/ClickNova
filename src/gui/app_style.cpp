// SPDX-License-Identifier: Apache-2.0
#include "app_style.hpp"
#include <QApplication>
#include <QFont>

void applyClickNovaStyle(QApplication& app) {
    app.setStyle(QStringLiteral("Fusion"));
    QFont font(QStringLiteral("Segoe UI"), 10);
    app.setFont(font);
    // One restrained palette throughout every workspace and floating dialog.
    // Keep action meanings redundant with labels/icons: color is never the only cue.
    app.setStyleSheet(QStringLiteral(R"(
        QWidget { background-color: #151920; color: #e6ecf5; font-size: 13px; }
        QLabel { background: transparent; }
        QLabel#heading { font-size: 24px; font-weight: 700; color: #a8c9ff; }
        QLabel#pageHeading { font-size: 20px; font-weight: 700; color: #f2f5fb; }
        QLabel#subheading, QLabel#sectionHint { color: #a1aec1; font-size: 12px; }
        QLabel#caution { color: #edc88e; }
        QLabel#recorderCounter { color: #b7e6cd; font-weight: 600; }
        QLabel#workflowFeedback { background: #1c3047; border: 1px solid #365a86;
                                  color: #e0edff; border-radius: 9px; padding: 9px 12px; }
        QLabel#inputLabQuickHeading, QLabel#inputLabStepsHeading {
            color: #d4e4f8; font-size: 13px; font-weight: 650;
            padding: 4px 0 1px 0;
        }
        QWidget#activityLogHeader { background: #202a36; border-top: 1px solid #46566c; }
        QLabel#activityLogTitle { font-weight: 650; color: #e4edf9; }
        QLabel#activityLogCount { background: #30445a; color: #dbeafc;
                                  border-radius: 9px; padding: 2px 6px; font-weight: 600; }
        QWidget#activityLogHeader QPushButton { padding: 6px 9px; border-radius: 7px; }
        QPushButton#toggleActivityLog { padding: 5px; }
        QLabel#profileHeaderTitle { color: #aebfd5; font-weight: 600; }
        QListWidget#navigation { background: #1b2029; border: 1px solid #313a48;
                                 border-radius: 13px; padding: 8px 5px; outline: none; }
        QListWidget#navigation::item { border-radius: 9px; padding: 12px 10px; margin: 2px 3px; }
        QListWidget#navigation::item:hover:!selected { background: #293343; }
        QListWidget#navigation::item:selected { background: #28446e; color: #ffffff; }
        QGroupBox { border: 1px solid #333d4b; border-radius: 12px;
                    margin-top: 13px; padding-top: 6px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px;
                           color: #dce6f6; }
        QGroupBox#inputLabEditor { border-color: #405879; }
        QTabWidget::pane { background: #1b2029; border: 1px solid #333d4b;
                           border-radius: 9px; }
        QTabBar::tab { background: #232b37; border: 1px solid #354152;
                       padding: 9px 15px; color: #c4cfdf; }
        QTabBar::tab:hover { background: #2a3749; }
        QTabBar::tab:selected { background: #304b74; color: white; }
        QTableWidget, QListWidget:not(#navigation) { background: #1a2029;
            alternate-background-color: #222b36; gridline-color: #303b4b;
            border: 1px solid #354152; border-radius: 10px; outline: none; }
        QTableWidget::item { padding: 6px 9px; }
        QTableWidget::item:selected, QListWidget::item:selected { background: #304b74; color: white; }
        QHeaderView::section { background: #232d3b; border: none;
            border-bottom: 1px solid #3c4a5c; padding: 9px; color: #c8d7ec; }
        QPushButton, QToolButton { background: #283242; color: #e0e8f4;
            border: 1px solid #405069; border-radius: 9px; padding: 9px 13px;
            font-weight: 500; }
        QPushButton:hover:enabled, QToolButton:hover:enabled {
            background: #344761; border-color: #648ac1; }
        QPushButton:pressed, QToolButton:pressed { background: #1d3c67; }
        QPushButton:disabled, QToolButton:disabled { color: #778596;
            background: #202731; border-color: #343e4d; }
        QPushButton#primaryButton, QPushButton#sequenceAddPoint,
        QPushButton[actionRole="primary"], QToolButton[actionRole="primary"] {
            background: #3265ba; color: #ffffff; border-color: #477cda; font-weight: 650; }
        QPushButton#primaryButton:hover:enabled, QPushButton#sequenceAddPoint:hover:enabled,
        QPushButton[actionRole="primary"]:hover:enabled, QToolButton[actionRole="primary"]:hover:enabled {
            background: #4078d0; border-color: #80a9f2; }
        QPushButton[actionRole="record"] { background: #883c53; color: white;
            border-color: #b85a77; font-weight: 650; }
        QPushButton[actionRole="record"]:hover:enabled { background: #a64b65; }
        QPushButton[actionRole="success"] { background: #236453; color: #e4fff5;
            border-color: #438d76; font-weight: 600; }
        QPushButton[actionRole="success"]:hover:enabled { background: #2e7b66; }
        QPushButton[actionRole="danger"] { background: #3b2a33;
            border-color: #795061; color: #ffd5dd; }
        QPushButton[actionRole="danger"]:hover:enabled { background: #673745; }
        QPushButton[actionRole="secondary"], QToolButton[actionRole="secondary"] {
            background: #252f3d; border-color: #405069; color: #d5e1f1; }
        QPushButton[actionRole="secondary"]:checked, QToolButton[actionRole="secondary"]:checked,
        QToolButton#sequenceEditToggle:checked { background: #304b74; border-color: #6997df; }
        QToolButton#sequenceEditToggle, QToolButton#sequenceWindowSettingsToggle {
            padding: 9px 12px; }
        QPushButton:focus, QToolButton:focus, QComboBox:focus, QSpinBox:focus,
        QLineEdit:focus, QPlainTextEdit:focus { border: 2px solid #88b5ff; }
        QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QKeySequenceEdit, QPlainTextEdit {
            background: #202936; color: #e9f0fa; border: 1px solid #435168;
            padding: 7px 9px; border-radius: 8px; selection-background-color: #3566ae; }
        QComboBox::drop-down { border: none; width: 24px; }
        QComboBox QAbstractItemView { background: #232d3a; color: #e9f0fa;
            border: 1px solid #4a5b72; selection-background-color: #355783; }
        QCheckBox { background: transparent; padding: 4px 2px; spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; }
        QScrollArea { border: none; }
        QScrollBar:vertical { background: #1a2029; width: 10px; margin: 1px; }
        QScrollBar::handle:vertical { background: #48596f; min-height: 28px; border-radius: 5px; }
        QScrollBar::handle:vertical:hover { background: #6381a4; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
        QToolTip { background: #2a3545; color: #f6f9ff;
            border: 1px solid #6986af; padding: 8px; border-radius: 7px; }
    )"));
}
