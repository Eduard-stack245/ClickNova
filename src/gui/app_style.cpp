// SPDX-License-Identifier: MIT
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
        QLabel#inputLabQuickHeading, QLabel#inputLabStepsHeading, QLabel#inputLabEditHeading {
            color: #d4e4f8; font-size: 13px; font-weight: 650;
            padding: 7px 0 2px 0;
        }
        QLabel#inputLabStepCount, QLabel#inputLabAutosaveStatus {
            color: #9cacc2; font-size: 11px; }
        QLabel#inputLabNextStepTime { color: #c0d0e7; }
        QLabel#inputLabStepWindowStatus, QLabel#inputLabWindowStatus {
            color: #aebcd1; font-size: 12px; }
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
        QListWidget#navigation::item:selected { background: #304b74; color: #ffffff; }
        QWidget#navigationPanel { background: #1b2029; border: 1px solid #354255;
                                  border-radius: 12px; }
        QLabel#navigationHeading { color: #8798b1; font-size: 10px;
                                   font-weight: 700; letter-spacing: 1px; }
        QListWidget#navigation { background: transparent; border: none;
                                 border-radius: 0px; padding: 0px; outline: none; }
        QListWidget#navigation::item { border-radius: 9px; padding: 10px 9px;
                                       margin: 2px 0px; }
        QListWidget#navigation[compact="true"]::item { padding: 8px 2px;
                                                     margin: 1px 1px; }
        QListWidget#navigation::item:selected { background: #304b74; color: #ffffff; }
        QSplitter#mainWorkspaceSplitter::handle:horizontal {
            background: #354255; border-radius: 3px; margin: 12px 2px;
        }
        QSplitter#mainWorkspaceSplitter::handle:horizontal:hover { background: #7393bd; }
        QToolButton#navigationCollapse { background: transparent; border: 1px solid #35485e;
                                          padding: 3px; border-radius: 8px; }
        QToolButton#navigationCollapse:hover { background: #30415a; }
        QGroupBox { border: 1px solid #333d4b; border-radius: 12px;
                    margin-top: 13px; padding-top: 6px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px;
                           color: #dce6f6; }
        QGroupBox#inputLabEditor { border-color: #405879; }
        QGroupBox#quickMoveGroup { border-color: #33475e; }
        QGroupBox#inputLabSavedGroup { border-color: #33475e; }
        QLabel#sequenceEditorHelp { color: #9eafc4; }
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
        QTableWidget::item:hover:!selected, QListWidget::item:hover:!selected { background: #29394d; }
        QHeaderView::section { background: #232d3b; border: none;
            border-bottom: 1px solid #3c4a5c; padding: 9px; color: #c8d7ec; }
        QPushButton, QToolButton { background: #283242; color: #e0e8f4;
            border: 1px solid #405069; border-radius: 8px; padding: 7px 11px;
            font-weight: 500; }
        QPushButton#inputLabEnlargeSteps, QPushButton#inputLabShrinkSteps {
            padding: 4px 10px; min-width: 28px; }
        QPushButton#inputLabMoveStepUp, QPushButton#inputLabMoveStepDown,
        QPushButton#inputLabUndoStep, QPushButton#inputLabRedoStep {
            background: #222d3a; border-color: #35475d; }
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

    // CLICK_NOVA_MODERN_STYLE_BEGIN
    app.setStyleSheet(app.styleSheet() + QStringLiteral(R"CLICKNOVA(
/* ClickNova Modern UI 2026: additive skin; existing handlers and controls stay intact. */
QMainWindow, QWidget { background-color: #0e1522; color: #edf3ff; font-family: "Segoe UI"; font-size: 13px; }
QWidget#modernNavigationPanel { background-color: #131e30; border: 1px solid #2a3a52; border-radius: 14px; }
QLabel#modernNavigationHeading { color: #889bb8; font-size: 11px; font-weight: 700; letter-spacing: 1px; padding-left: 8px; }
QToolButton#modernNavigationCollapse { border-radius: 9px; border: 1px solid #344863; background: #202f44; padding: 6px; }
QToolButton#modernNavigationCollapse:hover { background: #2d4564; border-color: #679adf; }
QSplitter#modernWorkspaceSplitter { background: transparent; }
QSplitter#modernWorkspaceSplitter::handle:horizontal { background: #253954; border-radius: 3px; margin: 14px 1px; }
QSplitter#modernWorkspaceSplitter::handle:horizontal:hover { background: #69a2f0; }
QListWidget#navigation { background: transparent; border: none; outline: none; padding: 4px 1px; }
QListWidget#navigation::item { border: 1px solid transparent; border-radius: 10px; padding: 12px 12px; margin: 2px 1px; color: #becde0; }
QListWidget#navigation::item:hover:!selected { background: #24354d; color: #f0f6ff; }
QListWidget#navigation::item:selected { background: #274775; color: #ffffff; border: 1px solid #4975ad; }
QListWidget#navigation[modernCollapsed="true"]::item { padding: 12px 7px; margin: 2px 0; }
QWidget#inputLab, QWidget#inputLabStepPanel { background: transparent; }
QLabel#pageHeading { font-size: 22px; font-weight: 700; color: #f4f7ff; padding-bottom: 3px; }
QLabel#subheading, QLabel#sectionHint { color: #9dadc6; font-size: 12px; }
QLabel#inputLabQuickHeading, QLabel#inputLabStepsHeading { color: #9cc4f8; font-size: 12px; font-weight: 700; padding: 9px 0 3px 0; }
QGroupBox { border: 1px solid #30425b; background-color: #162235; border-radius: 13px; margin-top: 16px; padding-top: 9px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 15px; padding: 0 9px; color: #dbe9fd; }
QGroupBox#inputLabEditor { border-color: #416a9d; }
QTabWidget::pane { background: #162335; border: 1px solid #33445e; border-radius: 10px; }
QTabBar::tab { padding: 10px 17px; background: #1b2b40; border: 1px solid #334963; color: #b6c8e0; }
QTabBar::tab:hover { background: #28405c; }
QTabBar::tab:selected { background: #2f507b; color: white; border-bottom: 2px solid #88b5f5; }
QPushButton, QToolButton { background: #24354b; border: 1px solid #39516b; border-radius: 9px; color: #e4eefc; padding: 8px 13px; font-weight: 500; }
QPushButton:hover:enabled, QToolButton:hover:enabled { background: #2c4461; border-color: #81a5d4; }
QPushButton:pressed, QToolButton:pressed { background: #274773; }
QPushButton:disabled, QToolButton:disabled { color: #76879f; background: #1b2839; border-color: #2b3a4f; }
QPushButton#primaryButton, QPushButton#sequenceAddPoint, QPushButton[actionRole="primary"], QToolButton[actionRole="primary"] { background: #346fba; color: white; border-color: #5b91d8; font-weight: 650; }
QPushButton#primaryButton:hover:enabled, QPushButton#sequenceAddPoint:hover:enabled, QPushButton[actionRole="primary"]:hover:enabled { background: #4384d3; }
QPushButton[actionRole="danger"] { background: #402b3a; border-color: #81505f; color: #ffe3e9; }
QPushButton[actionRole="danger"]:hover:enabled { background: #623746; }
QPushButton[actionRole="success"] { background: #205e56; border-color: #418f80; color: #e9fff6; }
QPushButton[actionRole="success"]:hover:enabled { background: #2c766b; }
QPushButton:focus, QToolButton:focus, QComboBox:focus, QSpinBox:focus, QLineEdit:focus, QPlainTextEdit:focus { border: 2px solid #9cc9ff; }
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTimeEdit, QKeySequenceEdit, QPlainTextEdit { color: #eff6ff; background: #172538; border: 1px solid #405570; border-radius: 9px; padding: 8px 10px; selection-background-color: #3e77be; }
QComboBox::drop-down { width: 24px; border: none; }
QComboBox QAbstractItemView { background: #1a2a3f; border: 1px solid #47627e; selection-background-color: #376296; }
QTableWidget, QListWidget:not(#navigation), QTreeWidget { background: #142034; alternate-background-color: #192a40; gridline-color: #2c405c; border: 1px solid #324761; border-radius: 11px; outline: none; }
QTableWidget::item, QTreeWidget::item { padding: 7px 9px; }
QTableWidget::item:selected, QListWidget::item:selected, QTreeWidget::item:selected { background: #345c8d; color: white; }
QHeaderView::section { background: #1c2d44; border: none; border-bottom: 1px solid #3c5676; padding: 9px 10px; color: #bacde6; font-weight: 600; }
QScrollBar:vertical { background: #111b2b; width: 10px; margin: 1px; }
QScrollBar::handle:vertical { background: #4b647f; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #7ba8d7; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QDockWidget::title { text-align: left; background: #18283d; color: #cfdef1; padding: 8px; }
QToolTip { background: #233650; color: white; border: 1px solid #759bce; border-radius: 8px; padding: 8px; }
QLabel { background-color: transparent; }
QScrollArea { border: none; background: transparent; }
QWidget#navigationPanel, QFrame#navigationPanel { background: #131e30; border: 1px solid #2a3a52; border-radius: 14px; }
QLabel#navigationHeading { color: #889bb8; font-size: 11px; font-weight: 700; padding-left: 6px; }
QToolButton#navigationCollapse { background: #202f44; border: 1px solid #344863; border-radius: 9px; padding: 6px; }
QSplitter#mainWorkspaceSplitter::handle:horizontal { background: #253954; border-radius: 3px; margin: 14px 1px; }

)CLICKNOVA"));
    // CLICK_NOVA_MODERN_STYLE_END
}
