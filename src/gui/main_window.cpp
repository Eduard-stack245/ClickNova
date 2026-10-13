// SPDX-License-Identifier: Apache-2.0
// Windows desktop input prototype; the Linux UI does not inject input yet.
#include <QApplication>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QColor>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QToolBar>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QAction>
#include <QPoint>
#include <QListWidget>
#include <QStackedWidget>
#include <QTabWidget>
#include <QKeySequenceEdit>
#include <QKeyCombination>
#include <QStringList>
#include <QTranslator>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QElapsedTimer>
#include <QDateTime>
#include <QTimeEdit>
#include <QHash>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QInputDialog>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QMessageBox>
#include <QPushButton>
#include <QDebug>
#include <QScrollArea>
#include <QScreen>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <vector>
#include <memory>
#include <utility>
#include "ui_translation.hpp"
#include "ui_icons.hpp"
#include "main_window.hpp"
#include "../storage/scenario_store.hpp"
#include "../storage/scenario_codec.hpp"
#include "../platform/windows/input_support.hpp"
#include "sequence_editor.hpp"
#include "input_lab.hpp"
#include "input_capture_dialog.hpp"
#include "wrap_layout.hpp"
#include "window_chooser.hpp"
#include "clicknova/sequence.hpp"
#include "clicknova/sequence_preview.hpp"
#include "clicknova/sequence_conversion.hpp"
#include "clicknova/sequence_to_input.hpp"
#include "clicknova/quick_motion.hpp"
#include "clicknova/version.hpp"
#include "clicknova/window_target.hpp"
#include "clicknova/windows_awake.hpp"
#include "clicknova/start_delay.hpp"
#include "clicknova/input_actions.hpp"
#ifdef _WIN32
#include "clicknova/windows_input_backend.hpp"
#endif

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <QAbstractNativeEventFilter>
#endif

namespace {
using clicknova::windows::ShortcutOptions;
#ifdef _WIN32
using clicknova::windows::sendClick;
using clicknova::windows::MousePointPicker;
using clicknova::windows::Hotkeys;
using clicknova::windows::validateShortcuts;
#endif

bool isSmokeMode() {
    return qApp->arguments().contains(QStringLiteral("--smoke-test-scenarios")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-ui")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-language")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-sequence")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-workflows")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-adaptive")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-icons")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-window-picker")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-input-lab")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-unified")) ||
           qApp->arguments().contains(QStringLiteral("--smoke-test-quick-motion"));
}

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("ClickNova — Auto Clicker v") +
                       QString::fromLatin1(CLICKNOVA_VERSION));
        QSettings initialPreferences(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        language_ = initialPreferences.value(QStringLiteral("ui/language"), QStringLiteral("en")).toString();
        if (language_ != QStringLiteral("ru") || isSmokeMode()) language_ = QStringLiteral("en");
        applyQtTranslator();
        resize(1040, 760);
        setMinimumSize(760, 540);
        auto* root = new QWidget(this);
        auto* layout = new QVBoxLayout(root);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(12);
        auto* brand = new QLabel(QStringLiteral("ClickNova"));
        brand->setObjectName(QStringLiteral("heading"));
        layout->addWidget(brand);
        // Keep user-visible feedback above the scrollable content. A message
        // only in the bottom status bar is easy to miss on large screens.
        workflowFeedback_ = new QLabel(root);
        workflowFeedback_->setObjectName(QStringLiteral("workflowFeedback"));
        workflowFeedback_->setWordWrap(true);
        workflowFeedback_->hide();
        layout->addWidget(workflowFeedback_);

        auto* body = new QHBoxLayout;
        body->setSpacing(16);
        navigation_ = new QListWidget(root);
        navigation_->setObjectName(QStringLiteral("navigation"));
        navigation_->setFixedWidth(208);
        navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        navigation_->setTextElideMode(Qt::ElideRight);
        navigation_->setSpacing(6);
        navigation_->addItems({QStringLiteral("Auto Clicker"), QStringLiteral("Sequence"),
                               QStringLiteral("Settings")});
        // Consistent task icons help identify sections before reading their labels.
        const clicknova::gui::Icon sections[] = {
            clicknova::gui::Icon::Mouse, clicknova::gui::Icon::List,
            clicknova::gui::Icon::Settings};
        for (int index = 0; index < navigation_->count(); ++index)
            navigation_->item(index)->setIcon(clicknova::gui::icon(sections[index]));
        navigation_->setIconSize(QSize(21, 21));
        body->addWidget(navigation_);
        pages_ = new QStackedWidget(root);
        body->addWidget(pages_, 1);
        layout->addLayout(body, 1);

        auto createPage = [this](const QString& title, const QString& description) {
            auto* scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            auto* page = new QWidget;
            auto* pageLayout = new QVBoxLayout(page);
            pageLayout->setContentsMargins(5, 6, 8, 12);
            pageLayout->setSpacing(14);
            auto* heading = new QLabel(title);
            heading->setObjectName(QStringLiteral("pageHeading"));
            pageLayout->addWidget(heading);
            auto* helper = new QLabel(description);
            helper->setObjectName(QStringLiteral("subheading"));
            helper->setWordWrap(true);
            pageLayout->addWidget(helper);
            scroll->setWidget(page);
            pages_->addWidget(scroll);
            return pageLayout;
        };
        auto* clickPage = createPage(QStringLiteral("Auto Clicker"),
                                     QStringLiteral("Set up simple clicks here. Use optional position or window targeting only when needed."));
        auto* positionPage = createPage(QStringLiteral("Position"),
                                        QStringLiteral("Optional: set a fixed position when enabled on the Auto Clicker page. Otherwise the current cursor is used."));
        auto* windowsPage = createPage(QStringLiteral("Windows"),
                                       QStringLiteral("Optional: select a window to restrict clicking. This is NOT background input; the target must stay foreground."));
        auto* sequencePage = createPage(QStringLiteral("Sequence"),
                                         QStringLiteral("One editor for mouse clicks, keyboard, text, waits and recorded actions."));
        auto* profilePage = createPage(QStringLiteral("Profiles"),
                                       QStringLiteral("Save and restore your frequently used configurations."));
        auto* shortcutsPage = createPage(QStringLiteral("Shortcuts"),
                                         QStringLiteral("Customize keyboard controls to avoid conflicts with other apps."));
        auto* settingsPage = createPage(QStringLiteral("Settings"),
                                        QStringLiteral("Manage local profiles, keyboard shortcuts and interface language."));
        inputLab_ = new InputLabWidget;
        inputLab_->setCanBegin([this] { return !active_ && !capturePending_; });
        // The only visible sequence editor accepts recorded, manual and imported actions.
        sequencePage->insertWidget(2, inputLab_);
        for (auto* page : {clickPage, positionPage, windowsPage, sequencePage, profilePage, shortcutsPage, settingsPage})
            page->addStretch();
        connect(navigation_, &QListWidget::currentRowChanged, this, [this](int index) {
            if (index < 0) return;
            if (index == 1 && inputLab_) inputLab_->refreshLibrary();
            const int pageIndex = index == 0 ? 0 : index == 1 ? 3 : 6;
            pages_->setCurrentIndex(pageIndex);
            // Both workflows share one always-visible toolbar at the top.
            // Settings keeps the last selected execution target.
            if (index < 2) sequenceToolbarActive_ = (index == 1);
            refreshRunToolbar();
            if (caution_) caution_->setVisible(index != 1);
            // The selected workspace determines which workflow Start runs.
            // Changing Settings must never change the currently selected mode.
            if (quickSequence_ && !active_ && !capturePending_ && index < 2) {
                quickSequence_->setChecked(index == 1);
                updateQuickOptions();
            }
            if (workflowFeedback_) workflowFeedback_->hide();
            if (auto* scroll = qobject_cast<QScrollArea*>(pages_->widget(pageIndex)))
                scroll->verticalScrollBar()->setValue(0);
        });
        navigation_->setCurrentRow(0);

        auto* group = new QGroupBox(QStringLiteral("Quick Auto Clicker"));
        auto* form = new QFormLayout(group);
        quickForm_ = form;
        form->setContentsMargins(16, 20, 16, 16);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(12);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        group->setMinimumHeight(240);
        group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        quickClickGroup_ = group;
        button_ = new QComboBox;
        button_->setObjectName(QStringLiteral("quickMouseButton"));
        button_->setMinimumHeight(44);
        button_->addItems({QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Middle"),
                           QStringLiteral("X1 (side)"), QStringLiteral("X2 (side)")});
        quickAction_ = new QComboBox;
        quickAction_->setObjectName(QStringLiteral("quickActionMode"));
        quickAction_->setMinimumHeight(44);
        quickAction_->addItems({QStringLiteral("Click"), QStringLiteral("Move cursor (no button)"),
                                QStringLiteral("Drag with selected button"),
                                QStringLiteral("Press and hold until Stop")});
        form->addRow(QStringLiteral("Action"), quickAction_);
        form->addRow(QStringLiteral("Mouse button"), button_);
        auto* chooseButton = new QPushButton(QStringLiteral("Choose button by pressing it..."));
        chooseButton->setObjectName(QStringLiteral("quickChooseMouseButton"));
        quickChooseButton_ = chooseButton;
        chooseButton->setToolTip(QStringLiteral(
            "Capture works only for mouse buttons Windows reports: Left, Right, Middle, X1 and X2. "
            "Firmware-only DPI buttons and additional vendor buttons cannot be detected here."));
        form->addRow(chooseButton);
        connect(chooseButton, &QPushButton::clicked, this, [this] {
            if (active_ || (quickAction_ && quickAction_->currentIndex() == 1)) return;
            if (auto result = chooseMouseButtonByPress(this, language_ == QStringLiteral("ru")))
                button_->setCurrentIndex(static_cast<int>(*result));
        });
        interval_ = new QSpinBox;
        interval_->setObjectName(QStringLiteral("quickClickInterval"));
        interval_->setMinimumHeight(44);
        interval_->setRange(10, 60000);
        interval_->setValue(200);
        interval_->setSuffix(QStringLiteral(" ms"));
        form->addRow(QStringLiteral("Interval between clicks"), interval_);
        quickHoldDuration_ = new QSpinBox;
        quickHoldDuration_->setObjectName(QStringLiteral("quickHoldDuration"));
        quickHoldDuration_->setMinimumHeight(44);
        quickHoldDuration_->setRange(0, 60000);
        quickHoldDuration_->setValue(0);
        quickHoldDuration_->setSuffix(QStringLiteral(" ms"));
        quickHoldDuration_->setToolTip(QStringLiteral("How long each mouse button stays pressed. Zero is a normal click. Supports Left, Right, Middle, X1 and X2."));
        form->addRow(QStringLiteral("Hold each click"), quickHoldDuration_);
        infinite_ = new QCheckBox(QStringLiteral("Unlimited clicks"));
        infinite_->setMinimumHeight(44);
        infinite_->setChecked(false);
        form->addRow(QStringLiteral("Mode"), infinite_);
        count_ = new QSpinBox;
        count_->setObjectName(QStringLiteral("quickClickCount"));
        count_->setMinimumHeight(44);
        count_->setRange(1, std::numeric_limits<int>::max());
        count_->setValue(10);
        count_->setEnabled(true);
        countLabel_ = new QLabel(QStringLiteral("Click count"));
        form->addRow(countLabel_, count_);
        // The first click can be scheduled by elapsed delay or Windows wall clock.
        startMode_ = new QComboBox(group);
        startMode_->setObjectName(QStringLiteral("quickStartMode"));
        // All settings controls must remain reachable at compact window sizes.
        // Unlike the older fields, this new combo had no minimum height on
        // Windows and caused gui_layout_smoke to fail (Qt default: ~28 px).
        startMode_->setMinimumHeight(44);
        startMode_->addItems({QStringLiteral("After a delay"),
                              QStringLiteral("At a specific time")});
        form->addRow(QStringLiteral("Start method"), startMode_);
        // Configurable start countdown is independent from the click interval.
        // Users may start immediately (0) or wait minutes, hours or days.
        auto* delayRow = new QWidget(group);
        auto* delayLayout = new QHBoxLayout(delayRow);
        delayLayout->setContentsMargins(0, 0, 0, 0);
        delayLayout->setSpacing(8);
        startDelayValue_ = new QSpinBox(delayRow);
        startDelayValue_->setObjectName(QStringLiteral("quickStartDelayValue"));
        startDelayValue_->setRange(0, std::numeric_limits<int>::max());
        startDelayValue_->setValue(3); // Preserve the old three-second default.
        startDelayValue_->setMinimumHeight(44);
        startDelayUnit_ = new QComboBox(delayRow);
        startDelayUnit_->setObjectName(QStringLiteral("quickStartDelayUnit"));
        startDelayUnit_->addItems({QStringLiteral("Seconds"), QStringLiteral("Minutes"),
                                   QStringLiteral("Hours"), QStringLiteral("Days")});
        startDelayUnit_->setMinimumHeight(44);
        startDelayUnit_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        delayLayout->addWidget(startDelayValue_, 2);
        delayLayout->addWidget(startDelayUnit_, 1);
        startDelayLabel_ = new QLabel(QStringLiteral("Start after"), group);
        form->addRow(startDelayLabel_, delayRow);
        startAtTime_ = new QTimeEdit(QTime(18, 30, 0), group);
        startAtTime_->setObjectName(QStringLiteral("quickStartAtTime"));
        startAtTime_->setDisplayFormat(QStringLiteral("HH:mm:ss"));
        startAtTime_->setMinimumHeight(44);
        startAtTime_->setToolTip(QStringLiteral("Uses Windows local time. If the chosen time has passed today, start tomorrow. Stop cancels the wait."));
        form->addRow(QStringLiteral("Start at time"), startAtTime_);
        keepAwakeQuick_ = new QCheckBox(QStringLiteral("Keep Windows and display awake while running"));
        keepAwakeQuick_->setObjectName(QStringLiteral("quickKeepAwake"));
        keepAwakeQuick_->setToolTip(QStringLiteral("Prevents automatic system sleep and display timeout until Stop. Cannot override lid closure or manual sleep."));
        QSettings awakeSettings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        keepAwakeQuick_->setChecked(awakeSettings.value(QStringLiteral("keepAwakeQuick"), true).toBool());
        form->addRow(keepAwakeQuick_);
        connect(keepAwakeQuick_, &QCheckBox::toggled, this, [](bool checked) {
            QSettings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"))
                .setValue(QStringLiteral("keepAwakeQuick"), checked);
        });
        customizeClicks_ = new QPushButton(QStringLiteral("Customize each click in Sequence..."));
        customizeClicks_->setObjectName(QStringLiteral("customizeEachClick"));
        customizeClicks_->setProperty("actionRole", QStringLiteral("secondary"));
        customizeClicks_->setMinimumHeight(42);
        customizeClicks_->setToolTip(QStringLiteral("Turn a fixed-position, finite click run into editable actions with independent intervals."));
        form->addRow(customizeClicks_);
        auto* quickTip = new QLabel(QStringLiteral(
            "Choose an action. For moving or dragging, capture the destination first. Stop releases any held button."));
        quickTip->setObjectName(QStringLiteral("subheading"));
        quickTip->setWordWrap(true);
        form->addRow(quickTip);
        connect(customizeClicks_, &QPushButton::clicked, this, [this] { convertQuickClicksToSequence(); });
        connect(infinite_, &QCheckBox::toggled, count_, [this](bool infinite) { count_->setEnabled(!infinite); });

        auto* positionGroup = new QGroupBox(QStringLiteral("Cursor Position"));
        auto* positionForm = new QFormLayout(positionGroup);
        positionForm->setContentsMargins(16, 20, 16, 16);
        positionForm->setVerticalSpacing(12);
        positionForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        positionMode_ = new QComboBox;
        positionMode_->setObjectName(QStringLiteral("positionMode"));
        positionMode_->setMinimumHeight(44);
        positionMode_->addItems({QStringLiteral("Current cursor"), QStringLiteral("Fixed screen coordinates")});
        positionForm->addRow(QStringLiteral("Position behavior"), positionMode_);

        auto* coordinates = new QWidget;
        auto* coordinatesLayout = new QHBoxLayout(coordinates);
        coordinatesLayout->setContentsMargins(0, 0, 0, 0);
        coordinatesLayout->setSpacing(8);
        x_ = new QSpinBox;
        x_->setObjectName(QStringLiteral("coordinateX"));
        y_ = new QSpinBox;
        y_->setObjectName(QStringLiteral("coordinateY"));
        x_->setPrefix(QStringLiteral("X: "));
        y_->setPrefix(QStringLiteral("Y: "));
        x_->setMinimumHeight(44);
        y_->setMinimumHeight(44);
#ifdef _WIN32
        const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        x_->setRange(left, left + qMax(1, width) - 1);
        y_->setRange(top, top + qMax(1, height) - 1);
#else
        x_->setRange(-32768, 32767);
        y_->setRange(-32768, 32767);
#endif
        coordinatesLayout->addWidget(x_);
        coordinatesLayout->addWidget(y_);
        positionForm->addRow(QStringLiteral("Coordinates"), coordinates);

        capture_ = new QPushButton(QStringLiteral("Capture cursor after 3 seconds"));
        capture_->setMinimumHeight(44);
#ifndef _WIN32
        capture_->setEnabled(false);
#endif
        positionForm->addRow(QStringLiteral("Pick coordinates"), capture_);
        positionCaptureSeconds_ = new QSpinBox;
        positionCaptureSeconds_->setObjectName(QStringLiteral("positionCaptureSeconds"));
        positionCaptureSeconds_->setRange(1, 60);
        positionCaptureSeconds_->setValue(5);
        positionCaptureSeconds_->setSuffix(QStringLiteral(" s"));
        positionCaptureSeconds_->setMinimumHeight(44);
        positionForm->addRow(QStringLiteral("Capture delay"), positionCaptureSeconds_);
        connect(positionMode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { updatePositionInputs(); });
        connect(capture_, &QPushButton::clicked, this, [this] { captureCursor(); });
        updatePositionInputs();
        positionPage->insertWidget(positionPage->count() - 1, positionGroup);
        clickPage->insertWidget(clickPage->count() - 1, group);

        // Destination is separate from the click/start position. It is not
        // silently inferred from (0, 0), which could move the cursor to an
        // unintended part of the desktop.
        quickMoveGroup_ = new QGroupBox(QStringLiteral("Pointer destination"));
        quickMoveGroup_->setObjectName(QStringLiteral("quickMoveGroup"));
        auto* moveForm = new QFormLayout(quickMoveGroup_);
        moveForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto* endpoint = new QWidget;
        auto* endpointRow = new QHBoxLayout(endpoint);
        endpointRow->setContentsMargins(0, 0, 0, 0);
        quickMoveX_ = new QSpinBox;
        quickMoveY_ = new QSpinBox;
        quickMoveX_->setObjectName(QStringLiteral("quickDestinationX"));
        quickMoveY_->setObjectName(QStringLiteral("quickDestinationY"));
        quickMoveX_->setRange(-131072, 131072);
        quickMoveY_->setRange(-131072, 131072);
        quickMoveX_->setMinimumHeight(44);
        quickMoveY_->setMinimumHeight(44);
        quickMoveX_->setPrefix(QStringLiteral("X: "));
        quickMoveY_->setPrefix(QStringLiteral("Y: "));
        endpointRow->addWidget(quickMoveX_);
        endpointRow->addWidget(quickMoveY_);
        moveForm->addRow(QStringLiteral("Destination coordinates"), endpoint);
        quickCaptureDestination_ = new QPushButton(QStringLiteral("Capture destination by timer"));
        quickCaptureDestination_->setObjectName(QStringLiteral("quickCaptureDestination"));
        quickCaptureDestination_->setProperty("actionRole", QStringLiteral("primary"));
        quickCaptureDestination_->setMinimumHeight(44);
        moveForm->addRow(quickCaptureDestination_);
        quickMoveDuration_ = new QSpinBox;
        quickMoveDuration_->setObjectName(QStringLiteral("quickMoveDuration"));
        quickMoveDuration_->setRange(0, 60000);
        quickMoveDuration_->setValue(600);
        quickMoveDuration_->setSuffix(QStringLiteral(" ms"));
        quickMoveDuration_->setMinimumHeight(44);
        moveForm->addRow(QStringLiteral("Movement duration"), quickMoveDuration_);
        auto* moveNote = new QLabel(QStringLiteral(
            "Start: current cursor or a fixed point in Click Position. "
            "Destination: screen coordinates, or client coordinates when a window is selected. "
            "Dragging holds the selected button through the movement and releases it at the end."));
        moveNote->setWordWrap(true);
        moveNote->setObjectName(QStringLiteral("subheading"));
        moveForm->addRow(moveNote);
        quickMoveReadiness_ = new QLabel;
        quickMoveReadiness_->setObjectName(QStringLiteral("quickMoveReadiness"));
        quickMoveReadiness_->setWordWrap(true);
        moveForm->addRow(quickMoveReadiness_);
        clickPage->insertWidget(clickPage->count() - 1, quickMoveGroup_);
        connect(quickAction_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int) { updateQuickOptions(); });
        connect(quickCaptureDestination_, &QPushButton::clicked, this, [this] {
            if (active_ || capturePending_) return;
            endpointCapture_ = true;
            captureCursor();
            if (!capturePending_) endpointCapture_ = false;
        });
        connect(quickMoveX_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            if (!restoringSettings_) destinationConfigured_ = true;
            updateQuickOptions();
        });
        connect(quickMoveY_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            if (!restoringSettings_) destinationConfigured_ = true;
            updateQuickOptions();
        });

        auto* targetGroup = new QGroupBox(QStringLiteral("Window Targeting"));
        auto* targetForm = new QFormLayout(targetGroup);
        targetForm->setContentsMargins(16, 20, 16, 16);
        targetForm->setHorizontalSpacing(16);
        targetForm->setVerticalSpacing(12);
        targetForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        targetMode_ = new QComboBox;
        targetMode_->setObjectName(QStringLiteral("targetMode"));
        targetMode_->setMinimumHeight(44);
        targetMode_->addItems({QStringLiteral("Global mouse (all windows)"),
                               QStringLiteral("Bound window (foreground only)")});
        targetForm->addRow(QStringLiteral("Target behavior"), targetMode_);
        targetWindows_ = new QComboBox;
        targetWindows_->setObjectName(QStringLiteral("targetWindows"));
        targetWindows_->setMinimumHeight(44);
        targetWindows_->setPlaceholderText(QStringLiteral("Select a running window"));
        targetForm->addRow(QStringLiteral("Window"), targetWindows_);
        auto* targetActions = new QWidget;
        auto* targetActionsLayout = new WrapLayout(targetActions, 8);
        targetActionsLayout->setContentsMargins(0, 0, 0, 0);
        chooseWindow_ = new QPushButton(QStringLiteral("Choose from list..."));
        chooseWindow_->setObjectName(QStringLiteral("chooseTargetWindow"));
        chooseWindow_->setProperty("actionRole", QStringLiteral("secondary"));
        chooseWindow_->setMinimumHeight(44);
        refreshWindows_ = new QPushButton(QStringLiteral("Refresh list"));
        captureWindow_ = new QPushButton(QStringLiteral("Pick window by hovering"));
        refreshWindows_->setMinimumHeight(44);
        captureWindow_->setMinimumHeight(44);
        targetActionsLayout->addWidget(chooseWindow_);
        targetActionsLayout->addWidget(refreshWindows_);
        targetActionsLayout->addWidget(captureWindow_);
        targetForm->addRow(QStringLiteral("Select"), targetActions);
        windowCaptureSeconds_ = new QSpinBox;
        windowCaptureSeconds_->setObjectName(QStringLiteral("windowCaptureSeconds"));
        windowCaptureSeconds_->setRange(1, 60);
        windowCaptureSeconds_->setSuffix(QStringLiteral(" s"));
        windowCaptureSeconds_->setValue(positionCaptureSeconds_->value());
        windowCaptureSeconds_->setMinimumHeight(44);
        targetForm->addRow(QStringLiteral("Capture delay"), windowCaptureSeconds_);
        connect(windowCaptureSeconds_, QOverload<int>::of(&QSpinBox::valueChanged),
                positionCaptureSeconds_, &QSpinBox::setValue);
        connect(positionCaptureSeconds_, QOverload<int>::of(&QSpinBox::valueChanged),
                windowCaptureSeconds_, &QSpinBox::setValue);
        connect(positionCaptureSeconds_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { refreshShortcutLabels(); });
        targetHelp_ = new QLabel;
        targetHelp_->setObjectName(QStringLiteral("targetHelp"));
        targetHelp_->setWordWrap(true);
        targetForm->addRow(QString(), targetHelp_);
        windowsPage->insertWidget(windowsPage->count() - 1, targetGroup);
        connect(targetMode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    // Store screen and client positions separately to avoid mixing coordinate spaces.
                    if (lastTargetMode_ == 1) {
                        clientCoordinates_ = QPoint(x_->value(), y_->value());
                    } else {
                        screenCoordinates_ = QPoint(x_->value(), y_->value());
                    }
                    lastTargetMode_ = targetMode_->currentIndex();
                    if (!restoringSettings_) {
                        // A point in client coordinates cannot become a screen
                        // point merely by selecting another targeting mode.
                        destinationConfigured_ = false;
                    }
                    const QPoint point = lastTargetMode_ == 1 ? clientCoordinates_ : screenCoordinates_;
                    x_->setValue(point.x());
                    y_->setValue(point.y());
                    updateTargetInputs();
                    updatePositionInputs();
                    if (!isSmokeMode())
                        saveLastSettings();
                });
        connect(targetWindows_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int selected) {
                    const auto previous = selectedTarget_ ? selectedTarget_->handle : 0;
                    if (selected >= 0 && selected < static_cast<int>(availableWindows_.size())) {
                        selectedTarget_ = availableWindows_[static_cast<std::size_t>(selected)];
                        savedWindowTitle_.clear();
                    } else {
                        selectedTarget_.reset();
                    }
                    if (targetMode_->currentIndex() == 1 && !restoringSettings_ &&
                        previous != (selectedTarget_ ? selectedTarget_->handle : 0))
                        destinationConfigured_ = false;
                    updateTargetInputs();
                });
        connect(chooseWindow_, &QPushButton::clicked, this, [this] { chooseWindowFromList(false); });
        connect(refreshWindows_, &QPushButton::clicked, this, [this] { refreshWindowList(); });
        connect(captureWindow_, &QPushButton::clicked, this, [this] { captureActiveWindow(); });

        // These are independent options, not separate automation modes.
        auto* optionsGroup = new QGroupBox(QStringLiteral("Optional features"));
        auto* optionsLayout = new QVBoxLayout(optionsGroup);
        optionsLayout->setContentsMargins(16, 22, 16, 16);
        optionsLayout->setSpacing(12);
        auto* optionsIntro = new QLabel(QStringLiteral(
            "Optional: pick a fixed point or select a target window. "
            "If neither is enabled, clicks use the current cursor position."));
        optionsIntro->setWordWrap(true);
        optionsIntro->setObjectName(QStringLiteral("subheading"));
        optionsLayout->addWidget(optionsIntro);

        auto* positionRow = new QHBoxLayout;
        quickPosition_ = new QCheckBox(QStringLiteral("Use fixed click position"));
        quickPosition_->setObjectName(QStringLiteral("optionUseFixedPosition"));
        editPosition_ = new QPushButton(QStringLiteral("Pick position..."));
        editPosition_->setObjectName(QStringLiteral("quickPickPosition"));
        editPosition_->setProperty("actionRole", QStringLiteral("primary"));
        editPosition_->setToolTip(QStringLiteral(
            "Click to hide ClickNova and capture the cursor point after the configured delay."));
        positionRow->addWidget(quickPosition_, 1);
        positionRow->addWidget(editPosition_);
        optionsLayout->addLayout(positionRow);

        auto* windowRow = new QHBoxLayout;
        quickWindow_ = new QCheckBox(QStringLiteral("Limit clicks to a selected window"));
        quickWindow_->setObjectName(QStringLiteral("optionLimitToWindow"));
        editWindow_ = new QPushButton(QStringLiteral("Choose window..."));
        editWindow_->setProperty("actionRole", QStringLiteral("secondary"));
        windowRow->addWidget(quickWindow_, 1);
        windowRow->addWidget(editWindow_);
        optionsLayout->addLayout(windowRow);

        auto* sequenceRow = new QHBoxLayout;
        quickSequence_ = new QCheckBox(QStringLiteral("Use click sequence instead of a single point"));
        quickSequence_->setObjectName(QStringLiteral("optionUseSequence"));
        editSequence_ = new QPushButton(QStringLiteral("Edit sequence..."));
        sequenceRow->addWidget(quickSequence_, 1);
        sequenceRow->addWidget(editSequence_);
        optionsLayout->addLayout(sequenceRow);
        // Sequences are now a full workflow, not a third independent click option.
        // Keep the underlying control for compatibility with saved profiles.
        quickSequence_->hide();
        editSequence_->hide();

        auto* summaryTitle = new QLabel(QStringLiteral("ACTIVE SETUP"));
        summaryTitle->setObjectName(QStringLiteral("subheading"));
        optionsLayout->addWidget(summaryTitle);
        activeSetup_ = new QLabel;
        activeSetup_->setObjectName(QStringLiteral("activeSetup"));
        activeSetup_->setWordWrap(true);
        optionsLayout->addWidget(activeSetup_);
        setupHelp_ = new QLabel;
        setupHelp_->setObjectName(QStringLiteral("setupHelp"));
        setupHelp_->setWordWrap(true);
        optionsLayout->addWidget(setupHelp_);
        clickPage->insertWidget(clickPage->count() - 1, optionsGroup);

        connect(quickPosition_, &QCheckBox::toggled, this, [this](bool enabled) {
            if (active_ || capturePending_) return;
            positionMode_->setCurrentIndex(enabled ? 1 : 0);
            updatePositionInputs();
            if (!isSmokeMode()) saveLastSettings();
        });
        connect(quickWindow_, &QCheckBox::toggled, this, [this](bool enabled) {
            if (active_ || capturePending_) return;
            targetMode_->setCurrentIndex(enabled ? 1 : 0);
            updateTargetInputs();
        });
        connect(quickSequence_, &QCheckBox::toggled, this, [this](bool) {
            updateQuickOptions();
            if (!isSmokeMode() && !restoringSettings_) saveLastSettings();
        });
        connect(editSequence_, &QPushButton::clicked, this, [this] {
            sequenceEditor_->setInitialPoint(x_->value(), y_->value(), button_->currentIndex(), interval_->value());
            navigation_->setCurrentRow(1);
        });
        connect(editPosition_, &QPushButton::clicked, this, [this] {
            if (active_ || capturePending_) return;
            // Picking a point is the common path; do not force the user to
            // enable a checkbox and find a different tab first.
            positionMode_->setCurrentIndex(1);
            captureCursor();
        });
        connect(editWindow_, &QPushButton::clicked, this, [this] { chooseWindowFromList(false); });
        connect(button_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { updateQuickOptions(); });
        connect(interval_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { updateQuickOptions(); });
        connect(infinite_, &QCheckBox::toggled, this, [this](bool) { updateQuickOptions(); });
        connect(count_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { updateQuickOptions(); });
        connect(x_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { updateQuickOptions(); });
        connect(y_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int) { updateQuickOptions(); });
        updateQuickOptions();

        auto* languageGroup = new QGroupBox(QStringLiteral("Interface language"));
        auto* languageForm = new QFormLayout(languageGroup);
        languageForm->setContentsMargins(16, 20, 16, 16);
        languageSelector_ = new QComboBox;
        languageSelector_->setObjectName(QStringLiteral("uiLanguage"));
        languageSelector_->setMinimumHeight(44);
        languageSelector_->addItem(QStringLiteral("English"), QStringLiteral("en"));
        languageSelector_->addItem(QString::fromUtf8("Русский"), QStringLiteral("ru"));
        languageForm->addRow(QStringLiteral("Language"), languageSelector_);
        settingsPage->insertWidget(settingsPage->count() - 1, languageGroup);
        languageSelector_->setCurrentIndex(language_ == QStringLiteral("ru") ? 1 : 0);
        connect(languageSelector_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int index) {
                    if (index < 0) return;
                    language_ = languageSelector_->itemData(index).toString();
                    if (!isSmokeMode()) {
                        QSettings userSettings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
                        userSettings.setValue(QStringLiteral("ui/language"), language_);
                    }
                    applyQtTranslator();
                    localizeUi();
                });

        auto* profileGroup = new QGroupBox(QStringLiteral("Profiles"));
        auto* profileLayout = new QVBoxLayout(profileGroup);
        profiles_ = new QComboBox;
        profiles_->setMinimumHeight(44);
        profiles_->setPlaceholderText(QStringLiteral("Choose a saved profile"));
        profileLayout->addWidget(profiles_);
        auto* profileActions = new WrapLayout(nullptr, 8);
        saveProfile_ = new QPushButton(QStringLiteral("Save as..."));
        saveProfile_->setProperty("actionRole", QStringLiteral("success"));
        loadProfile_ = new QPushButton(QStringLiteral("Load"));
        deleteProfile_ = new QPushButton(QStringLiteral("Delete"));
        deleteProfile_->setProperty("actionRole", QStringLiteral("danger"));
        for (auto* action : {saveProfile_, loadProfile_, deleteProfile_}) {
            action->setMinimumHeight(44);
            profileActions->addWidget(action);
        }
        profileLayout->addLayout(profileActions);
        profilePage->insertWidget(profilePage->count() - 1, profileGroup);
        auto* profileHelp = new QLabel(QStringLiteral(
            "Profiles save click settings, coordinates, window preference, shortcuts and language. "
            "They are stored for your local user, never in Git. A window must be selected again after restarting."));
        profileHelp->setWordWrap(true);
        profileHelp->setObjectName(QStringLiteral("subheading"));
        profilePage->insertWidget(profilePage->count() - 1, profileHelp);
        connect(saveProfile_, &QPushButton::clicked, this, [this] { saveNamedProfile(); });
        connect(loadProfile_, &QPushButton::clicked, this, [this] { loadNamedProfile(); });
        connect(deleteProfile_, &QPushButton::clicked, this, [this] { deleteNamedProfile(); });
        connect(profiles_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { updateProfileActions(); });

        auto* sequenceGroup = new QGroupBox(QStringLiteral("Click Sequence Editor"));
        auto* sequenceGroupLayout = new QVBoxLayout(sequenceGroup);
        sequenceEditor_ = new SequenceEditor(sequenceGroup);
        sequenceEditor_->setObjectName(QStringLiteral("sequenceEditor"));
        sequenceEditor_->setSpaceProvider([this] {
            return targetMode_->currentIndex() == 1
                ? clicknova::sequence::Space::Client : clicknova::sequence::Space::Screen;
        });
        sequenceEditor_->setWindowsProvider([] { return clicknova::platform::listWindows(); });
        sequenceEditor_->onCapture = [this] { captureSequencePoint(); };
        sequenceEditor_->onPickClick = [this] { beginClickPointPicker(); };
        sequenceEditor_->onPickHover = [this] { beginSequenceHoverPicker(); };
        sequenceEditor_->onPickWindow = [this] { beginWindowHoverCapture(true); };
        sequenceEditor_->onChooseWindow = [this] { chooseWindowFromList(true); };
        connect(positionCaptureSeconds_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int secs) { sequenceEditor_->setCaptureDelaySeconds(secs); refreshShortcutLabels(); });
        sequenceEditor_->onChange = [this] {
            if (positionCaptureSeconds_->value() != sequenceEditor_->captureDelaySeconds()) {
                const QSignalBlocker b(positionCaptureSeconds_);
                positionCaptureSeconds_->setValue(sequenceEditor_->captureDelaySeconds());
            }
            refreshShortcutLabels();
            updateQuickOptions();
            if (!isSmokeMode() && !restoringSettings_) saveLastSettings();
        };
        sequenceGroupLayout->addWidget(sequenceEditor_);
        sequencePage->insertWidget(sequencePage->count() - 1, sequenceGroup);

        // Sequence execution controls must be available next to the action list.
        // These fields mirror the original click count settings; only one state
        // is persisted, so switching pages cannot change the execution plan.
        auto* sequenceExecution = new QGroupBox(QStringLiteral("Sequence execution"));
        auto* sequenceExecutionLayout = new QVBoxLayout(sequenceExecution);
        sequenceExecutionLayout->setContentsMargins(16, 22, 16, 16);
        sequenceExecutionLayout->setSpacing(10);
        sequenceEnableOnPage_ = new QCheckBox(QStringLiteral("Use this sequence when pressing Start"), sequenceExecution);
        sequenceEnableOnPage_->setObjectName(QStringLiteral("sequenceEnableOnPage"));
        // Navigation selects the workflow automatically. Keep this widget
        // for backwards-compatible UI/test references, but never ask the user
        // to enable the same mode twice.
        sequenceEnableOnPage_->hide();
        auto* repeatRow = new QHBoxLayout;
        sequenceUnlimited_ = new QCheckBox(QStringLiteral("Repeat indefinitely"));
        sequenceUnlimited_->setObjectName(QStringLiteral("sequenceRepeatUnlimited"));
        sequenceRepeatCount_ = new QSpinBox;
        sequenceRepeatCount_->setObjectName(QStringLiteral("sequenceRepeatCount"));
        sequenceRepeatCount_->setRange(1, 1000000);
        sequenceRepeatCount_->setMinimumHeight(44);
        sequenceRepeatCount_->setMinimumWidth(100);
        auto* repeatLabel = new QLabel(QStringLiteral("Full sequence passes"));
        repeatRow->addWidget(sequenceUnlimited_);
        repeatRow->addStretch(1);
        repeatRow->addWidget(repeatLabel);
        repeatRow->addWidget(sequenceRepeatCount_);
        sequenceExecutionLayout->addLayout(repeatRow);
        sequenceReady_ = new QLabel;
        sequenceReady_->setObjectName(QStringLiteral("sequenceReadiness"));
        sequenceReady_->setWordWrap(true);
        sequenceExecutionLayout->addWidget(sequenceReady_);
        sequencePreview_ = new QLabel;
        sequencePreview_->setObjectName(QStringLiteral("sequencePreview"));
        sequencePreview_->setWordWrap(true);
        sequencePreview_->setTextFormat(Qt::PlainText);
        // Detailed estimates are useful, but should not push the action
        // buttons off-screen on first use.
        auto* previewToggle = new QCheckBox(QStringLiteral("Show click and timing estimate"));
        previewToggle->setObjectName(QStringLiteral("sequenceShowPreview"));
        sequenceExecutionLayout->addWidget(previewToggle);
        sequenceExecutionLayout->addWidget(sequencePreview_);
        sequencePreview_->hide();
        connect(previewToggle, &QCheckBox::toggled, sequencePreview_, &QWidget::setVisible);
        sequencePage->insertWidget(sequencePage->count() - 2, sequenceExecution);
        connect(sequenceEnableOnPage_, &QCheckBox::toggled, quickSequence_, &QCheckBox::setChecked);
        sequenceUnlimited_->setChecked(true);
        sequenceRepeatCount_->setValue(100);
        sequenceRepeatCount_->setEnabled(false);
        connect(sequenceUnlimited_, &QCheckBox::toggled, this, [this](bool unlimited) {
            sequenceRepeatCount_->setEnabled(!unlimited && !active_);
            updateQuickOptions();
            if (!isSmokeMode() && !restoringSettings_) saveLastSettings();
        });
        connect(sequenceRepeatCount_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
            updateQuickOptions();
            if (!isSmokeMode() && !restoringSettings_) saveLastSettings();
        });
        updateQuickOptions();

        auto* shortcutsGroup = new QGroupBox(QStringLiteral("Keyboard Controls"));
        auto* shortcutsForm = new QFormLayout(shortcutsGroup);
        shortcutsForm->setContentsMargins(16, 20, 16, 16);
        shortcutsForm->setVerticalSpacing(12);
        shortcutsForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        enableStartShortcut_ = new QCheckBox(QStringLiteral("Enable global Start shortcut"));
        enableStartShortcut_->setChecked(true);
        shortcutsForm->addRow(QStringLiteral("Start enabled"), enableStartShortcut_);
        startShortcut_ = new QKeySequenceEdit;
        startShortcut_->setObjectName(QStringLiteral("shortcutStart"));
        startShortcut_->setKeySequence(QKeySequence(QStringLiteral("F6")));
        startShortcut_->setMaximumSequenceLength(1);
        startShortcut_->setMinimumHeight(44);
        shortcutsForm->addRow(QStringLiteral("Start"), startShortcut_);
        inputLab_->setStartShortcut([this] {
            const QString key = startShortcut_->keySequence().toString(QKeySequence::NativeText);
            if (key.isEmpty()) return QString();
            return enableStartShortcut_->isChecked() ? key : QString();
        });
        stopShortcut_ = new QKeySequenceEdit;
        stopShortcut_->setObjectName(QStringLiteral("shortcutStop"));
        stopShortcut_->setKeySequence(QKeySequence(QStringLiteral("F7")));
        stopShortcut_->setMaximumSequenceLength(1);
        stopShortcut_->setMinimumHeight(44);
        shortcutsForm->addRow(QStringLiteral("Stop (required)"), stopShortcut_);
        enablePickShortcut_ = new QCheckBox(QStringLiteral("Enable temporary point-picking shortcut"));
        enablePickShortcut_->setChecked(true);
        shortcutsForm->addRow(QStringLiteral("Point picker"), enablePickShortcut_);
        pickShortcut_ = new QKeySequenceEdit;
        pickShortcut_->setObjectName(QStringLiteral("shortcutPick"));
        pickShortcut_->setKeySequence(QKeySequence(QStringLiteral("F8")));
        pickShortcut_->setMaximumSequenceLength(1);
        pickShortcut_->setMinimumHeight(44);
        shortcutsForm->addRow(QStringLiteral("Pick point"), pickShortcut_);
        auto* shortcutHelp = new QLabel(QStringLiteral(
            "Click a field and press a new key combination. Supported: F1–F11, or Ctrl/Alt + letter, number or F-key. "
            "Each shortcut must be unique. The Stop shortcut is required for safety. "
            "Start can be disabled; point picking always works with the 3-second button. "
            "Start is reserved only while idle; Stop is reserved only while running; "
            "the point shortcut is reserved only while picking. Any reserved shortcut is consumed by ClickNova, "
            "so pick a Stop key your automation will not need."));
        shortcutHelp->setWordWrap(true);
        shortcutsForm->addRow(shortcutHelp);
        shortcutsPage->insertWidget(shortcutsPage->count() - 1, shortcutsGroup);
        for (auto* editor : {startShortcut_, stopShortcut_, pickShortcut_}) {
            editor->installEventFilter(this);
            connect(editor, &QKeySequenceEdit::editingFinished, this, [this] { applyShortcutEdits(); });
        }
        connect(enableStartShortcut_, &QCheckBox::toggled, this, [this](bool enabled) {
            startShortcut_->setEnabled(enabled && !active_ && !capturePending_);
            applyShortcutEdits();
        });
        connect(enablePickShortcut_, &QCheckBox::toggled, this, [this](bool enabled) {
            pickShortcut_->setEnabled(enabled && !active_ && !capturePending_);
            applyShortcutEdits();
        });
        // Keep the primary navigation task-oriented. Optional click settings live
        // under the Auto Clicker, while personal configuration lives in Settings.
        clickConfigTabs_ = new QTabWidget;
        clickConfigTabs_->setObjectName(QStringLiteral("clickConfigurationTabs"));
        clickConfigTabs_->setDocumentMode(true);
        clickConfigTabs_->setMinimumHeight(320);
        auto* positionTab = new QWidget;
        auto* positionTabLayout = new QVBoxLayout(positionTab);
        positionTabLayout->addWidget(positionGroup);
        positionTabLayout->addStretch();
        auto* targetTab = new QWidget;
        auto* targetTabLayout = new QVBoxLayout(targetTab);
        targetTabLayout->addWidget(targetGroup);
        targetTabLayout->addStretch();
        clickConfigTabs_->addTab(positionTab, QStringLiteral("Click Position"));
        clickConfigTabs_->addTab(targetTab, QStringLiteral("Target Window"));
        optionalDetails_ = new QCheckBox(QStringLiteral("Advanced: edit coordinates and target-window options"));
        optionalDetails_->setObjectName(QStringLiteral("showOptionalDetails"));
        clickPage->insertWidget(clickPage->count() - 1, optionalDetails_);
        clickPage->insertWidget(clickPage->count() - 1, clickConfigTabs_);
        clickConfigTabs_->setVisible(false);
        connect(optionalDetails_, &QCheckBox::toggled, clickConfigTabs_, &QTabWidget::setVisible);
        // The quick actions open the matching detailed tab, but the user does
        // not need to visit it for ordinary global clicking.

        settingsTabs_ = new QTabWidget;
        settingsTabs_->setObjectName(QStringLiteral("personalSettingsTabs"));
        settingsTabs_->setDocumentMode(true);
        auto addSettingsTab = [this](QGroupBox* content, const QString& name, QLabel* help = nullptr) {
            auto* tab = new QWidget;
            auto* tabLayout = new QVBoxLayout(tab);
            tabLayout->setSpacing(12);
            tabLayout->addWidget(content);
            if (help) tabLayout->addWidget(help);
            tabLayout->addStretch();
            settingsTabs_->addTab(tab, name);
        };
        addSettingsTab(profileGroup, QStringLiteral("Profiles"), profileHelp);
        addSettingsTab(shortcutsGroup, QStringLiteral("Shortcuts"));
        addSettingsTab(languageGroup, QStringLiteral("Language"));
        settingsPage->insertWidget(settingsPage->count() - 1, settingsTabs_);

        // Scenario libraries are independent of the complete user profile.
        // User data lives under QStandardPaths::AppDataLocation, never in Git.
        auto addScenarioLibrary = [this](QVBoxLayout* page, clicknova::storage::Kind kind) {
            const bool autoClicker = kind == clicknova::storage::Kind::AutoClicker;
            auto* group = new QGroupBox(autoClicker ? QStringLiteral("Saved auto-clicker scenarios")
                                                   : QStringLiteral("Saved sequence scenarios"));
            auto* groupLayout = new QVBoxLayout(group);
            auto* picker = new QComboBox;
            picker->setObjectName(autoClicker ? QStringLiteral("autoScenarioSelector")
                                             : QStringLiteral("sequenceScenarioSelector"));
            picker->setMinimumHeight(44);
            picker->setPlaceholderText(QStringLiteral("Select a saved scenario"));
            auto* buttons = new WrapLayout(nullptr, 8);
            auto* saveButton = new QPushButton(QStringLiteral("Save as..."));
            saveButton->setProperty("actionRole", QStringLiteral("success"));
            auto* loadButton = new QPushButton(QStringLiteral("Load"));
            auto* deleteButton = new QPushButton(QStringLiteral("Delete"));
            deleteButton->setProperty("actionRole", QStringLiteral("danger"));
            saveButton->setObjectName(autoClicker ? QStringLiteral("autoScenarioSave")
                                                 : QStringLiteral("sequenceScenarioSave"));
            loadButton->setObjectName(autoClicker ? QStringLiteral("autoScenarioLoad")
                                                 : QStringLiteral("sequenceScenarioLoad"));
            buttons->addWidget(saveButton);
            buttons->addWidget(loadButton);
            buttons->addWidget(deleteButton);
            groupLayout->addWidget(picker);
            groupLayout->addLayout(buttons);
            page->insertWidget(page->count() - 1, group);
            if (autoClicker) {
                clickScenarios_ = picker;
                saveClickScenario_ = saveButton;
                loadClickScenario_ = loadButton;
                deleteClickScenario_ = deleteButton;
            } else {
                sequenceScenarios_ = picker;
                saveSequenceScenario_ = saveButton;
                loadSequenceScenario_ = loadButton;
                deleteSequenceScenario_ = deleteButton;
            }
            connect(saveButton, &QPushButton::clicked, this, [this, kind] { saveScenario(kind); });
            connect(loadButton, &QPushButton::clicked, this, [this, kind] { loadScenario(kind); });
            connect(deleteButton, &QPushButton::clicked, this, [this, kind] { deleteScenario(kind); });
            connect(picker, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this](int) { updateScenarioButtons(); });
            return group;
        };
        addScenarioLibrary(clickPage, clicknova::storage::Kind::AutoClicker);
        // The old click editor stays constructed for reading existing profiles
        // and for the backwards-compatible internal tests, but is NOT another
        // user-facing workflow. All edits, recording and playback take place in
        // InputLabWidget. Legacy saved scenarios are selectable in its library.
        auto* legacyLibrary = addScenarioLibrary(sequencePage, clicknova::storage::Kind::Sequence);
        for (QWidget* old : {static_cast<QWidget*>(sequenceGroup),
                             static_cast<QWidget*>(sequenceExecution),
                             static_cast<QWidget*>(legacyLibrary)}) {
            sequencePage->removeWidget(old);
            old->hide();
        }
        inputLab_->setLegacyImporter([this] {
            if (!inputLab_ || inputLab_->isRunning() || active_ || capturePending_) return;
            const auto& previous = sequenceEditor_->steps();
            if (previous.empty()) {
                QMessageBox::information(this, QStringLiteral("ClickNova"), uiText(
                    QStringLiteral("No previous unsaved click sequence was found. Saved legacy scenarios are available in the common library.")));
                return;
            }
            int repeats = sequenceRepeatCount_->value();
            if (sequenceUnlimited_->isChecked()) {
                bool ok = false;
                repeats = QInputDialog::getInt(this, QStringLiteral("ClickNova"),
                    uiText(QStringLiteral("Choose a finite repetition count for the old sequence:")),
                    1, 1, std::numeric_limits<int>::max(), 1, &ok);
                if (!ok) return;
            }
            const clicknova::sequence::Plan old{previous, repeats, false,
                                                 sequenceEditor_->passDelayMs()};
            std::string reason;
            auto converted = clicknova::sequence::toInputPlan(
                old, &reason, targetMode_->currentIndex() == 1);
            if (!converted) {
                QMessageBox::warning(this, QStringLiteral("ClickNova"), QString::fromStdString(reason));
                return;
            }
            if (QMessageBox::question(this, QStringLiteral("ClickNova"), uiText(
                QStringLiteral("Replace the common action list with the previous click sequence?"))) != QMessageBox::Yes) return;
            inputLab_->replaceActions(*converted);
            setStatus(QStringLiteral("Previous clicks imported into the unified action list. Select the target window again if needed."));
        });
        if (!isSmokeMode()) refreshScenarios();

        caution_ = new QLabel;
        caution_->setWordWrap(true);
        caution_->setObjectName(QStringLiteral("caution"));
        layout->addWidget(caution_);

        auto* buttons = new QHBoxLayout;
        start_ = new QPushButton(QStringLiteral("Start"));
        start_->setObjectName(QStringLiteral("primaryButton"));
        start_->setProperty("actionRole", QStringLiteral("primary"));
        stop_ = new QPushButton(QStringLiteral("Stop"));
        stop_->setProperty("actionRole", QStringLiteral("danger"));
        stop_->setEnabled(false);
        start_->setMinimumHeight(52);
        stop_->setMinimumHeight(52);
        buttons->addWidget(start_);
        buttons->addWidget(stop_);
        layout->addLayout(buttons);
        status_ = new QLabel(QStringLiteral("Ready"));
        status_->setWordWrap(true);
        status_->setMinimumHeight(28);
        layout->addWidget(status_);
        // Keep the old label available for compatibility, but show messages
        // only in the collapsible diagnostic log below the workspace.
        status_->hide();
        if (workflowFeedback_) workflowFeedback_->hide();
        setCentralWidget(root);
        setupRunToolbarAndLog();
        refreshWindowList();
        if (!isSmokeMode()) {
            restoreLastSettings();
            refreshProfiles();
        }
        updateProfileActions();
        updateTargetInputs();
        localizeUi();
        // No buttons are removed: icons are supplemental to the localized captions.
        using clicknova::gui::Icon;
        using clicknova::gui::setButtonIcon;
        setButtonIcon(start_, Icon::Play, QColor(QStringLiteral("#ffffff")));
        setButtonIcon(stop_, Icon::Stop, QColor(QStringLiteral("#ffcad3")));
        setButtonIcon(capture_, Icon::Target);
        setButtonIcon(quickCaptureDestination_, Icon::Target);
        setButtonIcon(chooseWindow_, Icon::Window);
        setButtonIcon(refreshWindows_, Icon::Refresh);
        setButtonIcon(captureWindow_, Icon::Target);
        setButtonIcon(editPosition_, Icon::Cursor);
        setButtonIcon(editWindow_, Icon::Window);
        setButtonIcon(editSequence_, Icon::Edit);
        setButtonIcon(customizeClicks_, Icon::List);
        setButtonIcon(saveProfile_, Icon::Save);
        setButtonIcon(loadProfile_, Icon::Folder);
        setButtonIcon(deleteProfile_, Icon::Delete);
        setButtonIcon(saveClickScenario_, Icon::Save);
        setButtonIcon(loadClickScenario_, Icon::Folder);
        setButtonIcon(deleteClickScenario_, Icon::Delete);
        setButtonIcon(saveSequenceScenario_, Icon::Save);
        setButtonIcon(loadSequenceScenario_, Icon::Folder);
        setButtonIcon(deleteSequenceScenario_, Icon::Delete);

        connect(start_, &QPushButton::clicked, this, [this] { start(); });
        // Saving immediately also persists the timer across normal restarts.
        connect(startDelayValue_, qOverload<int>(&QSpinBox::valueChanged),
                this, [this](int) { if (!isSmokeMode() && !restoringSettings_) saveLastSettings(); });
        connect(startDelayUnit_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { if (!isSmokeMode() && !restoringSettings_) saveLastSettings(); });
        connect(startMode_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    updateQuickOptions();
                    if (!isSmokeMode() && !restoringSettings_) saveLastSettings();
                });
        connect(startAtTime_, &QTimeEdit::timeChanged,
                this, [this](const QTime&) {
                    if (!isSmokeMode() && !restoringSettings_) saveLastSettings();
                });
        // Returning to a saved sequence should open its actual workspace.
        if (quickSequence_->isChecked()) navigation_->setCurrentRow(1);
        connect(stop_, &QPushButton::clicked, this, [this] { stop(); });
        // Do not start another click while a button is held or a drag is in progress.
        clickTimer_.setSingleShot(true);
        connect(&clickTimer_, &QTimer::timeout, this, [this] { click(); });
        quickReleaseTimer_.setSingleShot(true);
        quickReleaseTimer_.setTimerType(Qt::PreciseTimer);
        connect(&quickReleaseTimer_, &QTimer::timeout, this, [this] {
#ifdef _WIN32
            if (!quickMouseDown_) return;
            const auto button = quickHeldButton_;
            clicknova::input::WindowsInputBackend backend;
            clicknova::input::Event release;
            release.kind = clicknova::input::EventKind::ButtonRelease;
            release.button = button;
            if (!backend.dispatch(release)) {
                stopWithReason(QStringLiteral("Windows could not release the mouse button."));
                return;
            }
            quickMouseDown_ = false;
            finishClickCycle();
#endif
        });
        quickMotionTimer_.setTimerType(Qt::PreciseTimer);
        quickMotionTimer_.setInterval(16);
        connect(&quickMotionTimer_, &QTimer::timeout, this, [this] { quickMotionTick(); });
        countdownTimer_.setSingleShot(true);
        countdownTimer_.setTimerType(Qt::PreciseTimer);
        connect(&countdownTimer_, &QTimer::timeout, this, [this] { countdownTick(); });
        captureCountdownTimer_.setInterval(1000);
        captureCountdownTimer_.setTimerType(Qt::PreciseTimer);
        connect(&captureCountdownTimer_, &QTimer::timeout, this, [this] {
            if (!capturePending_ || captureRemaining_ <= 0) { captureCountdownTimer_.stop(); return; }
            --captureRemaining_;
            if (captureRemaining_ > 0) { showCaptureCountdown(); return; }
            captureCountdownTimer_.stop();
#ifdef _WIN32
            if (captureWindowPending_ || captureStepWindowPending_) finishWindowHoverCapture();
            else if (sequenceTimedCapture_) finishSequenceTimedCapture();
            else finishCursorCapture();
#endif
        });
        clickTimer_.setTimerType(Qt::PreciseTimer);
        clickTimer_.setSingleShot(true);
#ifdef _WIN32
        if (!isSmokeMode()) {
            hotkeys_ = std::make_unique<Hotkeys>([this] { start(); }, [this] { stop(); },
                                                 [this] {
                                                     if (sequenceHoverPicking_) finishSequenceHoverPicking();
                                                     else if (sequenceTimedCapture_) finishSequenceTimedCapture();
                                                     else if (captureWindowPending_ || captureStepWindowPending_) finishWindowHoverCapture();
                                                     else finishCursorCapture();
                                                 });
            qApp->installNativeEventFilter(hotkeys_.get());
            if (inputLab_) inputLab_->setStopHotkeyController(
                [this](bool running) {
                    if (!hotkeys_ || (running && (active_ || capturePending_))) return false;
                    if (!hotkeys_->setRunning(running)) return false;
                    setShortcutEditorsEnabled(!running);
                    return true;
                },
                [this] { return stopHotkeyLabel(); });
            restoreShortcutSettings();
            if (!hotkeys_->ready()) {
                setStatus(QStringLiteral("Start shortcut unavailable. Choose another key in Shortcuts."));
            }
        } else {
            refreshShortcutLabels();
        }
#else
        start_->setEnabled(false);
        setStatus(QStringLiteral("Linux input injection is not available in this prototype."));
#endif
    }
    ~MainWindow() override {
        captureCountdownTimer_.stop();
        hideCaptureCountdown();
        delete captureOverlay_;
        captureOverlay_ = nullptr;
#ifdef _WIN32
        mousePicker_.cancel();
#endif
        stop();
        if (!isSmokeMode()) saveLastSettings();
#ifdef _WIN32
        if (hotkeys_) qApp->removeNativeEventFilter(hotkeys_.get());
#endif
    }
private:
    void resizeEvent(QResizeEvent* event) override {
        QMainWindow::resizeEvent(event);
        if (!navigation_) return;
        // Give the content priority at smaller desktop/window sizes.
        const int navWidth = width() < 980 ? 144 : width() < 1240 ? 176 : 208;
        if (navigation_->width() != navWidth) navigation_->setFixedWidth(navWidth);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
#ifdef _WIN32
        if (watched == startShortcut_ || watched == stopShortcut_ || watched == pickShortcut_) {
            if (event->type() == QEvent::FocusIn && hotkeys_ && !active_ && !capturePending_) {
                hotkeys_->suspend();
            } else if (event->type() == QEvent::FocusOut && hotkeys_) {
                QTimer::singleShot(0, this, [this] { applyShortcutEdits(); });
            }
        }
#endif
        return QMainWindow::eventFilter(watched, event);
    }

    // These strings are UI resources, not persisted keys or macro identifiers.
    // Language can be switched without restarting or rebuilding the application.
    void applyQtTranslator() {
        if (qtTranslator_) qApp->removeTranslator(qtTranslator_.get());
        qtTranslator_.reset();
        if (language_ != QStringLiteral("ru")) return;
        auto candidate = std::make_unique<QTranslator>();
        const QString translations = QCoreApplication::applicationDirPath()
                                   + QStringLiteral("/translations");
        if (candidate->load(QStringLiteral("qt_ru"), translations) ||
            candidate->load(QStringLiteral("qtbase_ru"), translations)) {
            qApp->installTranslator(candidate.get());
            qtTranslator_ = std::move(candidate);
        }
    }
    QString uiText(const QString& english) const {
        return translateUiText(english, language_);
    }
    void appendActivityLog(const QString& message) {
        if (!activityLog_ || message.trimmed().isEmpty()) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        // High-frequency click/step progress belongs to the current status,
        // not thousands of separate entries in the diagnostic history.
        if (message == previousLogText_ && now - previousLogTime_ < 2000) return;
        if (now - previousLogTime_ < 1200 &&
            (message.contains(QStringLiteral("Events:")) ||
             message.contains(QStringLiteral("Событий:")) ||
             message.contains(QStringLiteral("clicks:")) ||
             message.contains(QStringLiteral("кликов:")))) return;
        previousLogText_ = message;
        previousLogTime_ = now;
        activityLog_->appendPlainText(QDateTime::currentDateTime().toString(
            QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + message);
    }
    void refreshRunToolbar() {
        if (!quickStartAction_ || !quickStopAction_ || !stepStartAction_ || !stepStopAction_) return;
        const bool quick = !sequenceToolbarActive_;
        quickStartAction_->setVisible(quick);
        quickStopAction_->setVisible(quick);
        stepStartAction_->setVisible(!quick);
        stepStopAction_->setVisible(!quick);
        if (toolbarWorkflow_) toolbarWorkflow_->setText(quick ?
            uiText(QStringLiteral("Auto Clicker")) : uiText(QStringLiteral("Steps")));
    }
    void setupRunToolbarAndLog() {
        auto* toolbar = new QToolBar(QStringLiteral("Execution"), this);
        toolbar->setObjectName(QStringLiteral("executionToolbar"));
        toolbar->setMovable(false);
        toolbar->setFloatable(false);
        toolbar->setIconSize(QSize(22, 22));
        addToolBar(Qt::TopToolBarArea, toolbar);
        toolbarWorkflow_ = new QLabel(toolbar);
        toolbarWorkflow_->setObjectName(QStringLiteral("executionWorkflow"));
        toolbarWorkflow_->setMinimumWidth(110);
        toolbar->addWidget(toolbarWorkflow_);
        toolbar->addSeparator();
        // Reparent existing connected buttons; no second Run/Stop controls.
        quickStartAction_ = toolbar->addWidget(start_);
        quickStopAction_ = toolbar->addWidget(stop_);
        stepStartAction_ = toolbar->addWidget(inputLab_->runControl());
        stepStopAction_ = toolbar->addWidget(inputLab_->stopControl());
        for (auto* button : {start_, stop_, inputLab_->runControl(), inputLab_->stopControl()}) {
            button->setMinimumWidth(136);
            button->setMinimumHeight(42);
        }
        toolbar->addSeparator();
        logDock_ = new QDockWidget(uiText(QStringLiteral("Activity log")), this);
        logDock_->setObjectName(QStringLiteral("activityLogDock"));
        logDock_->setAllowedAreas(Qt::BottomDockWidgetArea);
        logDock_->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
        auto* logPanel = new QWidget(logDock_);
        auto* panelLayout = new QVBoxLayout(logPanel);
        panelLayout->setContentsMargins(6, 4, 6, 4);
        panelLayout->setSpacing(4);
        auto* logActions = new QHBoxLayout;
        auto* clear = new QPushButton(uiText(QStringLiteral("Clear log")), logPanel);
        clear->setObjectName(QStringLiteral("clearActivityLog"));
        clear->setToolTip(QStringLiteral("Clear only the visible diagnostic history; scenarios and recordings are not changed."));
        // Recovery belongs to diagnostics, not the frequently-used Record button.
        // It becomes visible only when an unfinished local checkpoint exists.
        logActions->addWidget(inputLab_->recoveryControl());
        logActions->addStretch();
        logActions->addWidget(clear);
        panelLayout->addLayout(logActions);
        activityLog_ = new QPlainTextEdit(logPanel);
        activityLog_->setObjectName(QStringLiteral("activityLogView"));
        activityLog_->setReadOnly(true);
        activityLog_->setMaximumBlockCount(1500);
        activityLog_->setMinimumHeight(70);
        activityLog_->setPlaceholderText(uiText(QStringLiteral("Execution and recorder events will appear here.")));
        panelLayout->addWidget(activityLog_);
        logDock_->setWidget(logPanel);
        addDockWidget(Qt::BottomDockWidgetArea, logDock_);
        resizeDocks({logDock_}, {185}, Qt::Vertical);
        connect(clear, &QPushButton::clicked, activityLog_, &QPlainTextEdit::clear);
        QAction* toggle = logDock_->toggleViewAction();
        toggle->setObjectName(QStringLiteral("toggleActivityLog"));
        toggle->setText(uiText(QStringLiteral("Log")));
        toolbar->addAction(toggle);
        inputLab_->setStatusSink([this](const QString& message) {
            appendActivityLog(message);
        });
        sequenceToolbarActive_ = navigation_->currentRow() == 1;
        refreshRunToolbar();
        appendActivityLog(uiText(QStringLiteral("Ready")));
    }
    void setStatus(const QString& english) {
        lastStatusEnglish_ = english;
        if (status_) status_->setText(uiText(english));
        appendActivityLog(uiText(english));
    }
    void localizeUi() {
        if (!pages_ || !navigation_) return;
        for (auto* label : findChildren<QLabel*>()) {
            if (label == status_ || label == activeSetup_ || label == setupHelp_ ||
                label == targetHelp_ || label == caution_ || label == sequenceReady_ ||
                label == sequencePreview_) continue;
            const QVariant stored = label->property("clicknova.english");
            const QString english = stored.isValid() ? stored.toString() : label->text();
            label->setProperty("clicknova.english", english);
            label->setText(uiText(english));
        }
        for (auto* group : findChildren<QGroupBox*>()) {
            const QVariant stored = group->property("clicknova.english");
            const QString english = stored.isValid() ? stored.toString() : group->title();
            group->setProperty("clicknova.english", english);
            group->setTitle(uiText(english));
        }
        for (auto* button : findChildren<QPushButton*>()) {
            if (button == start_ || button == stop_ || button == capture_) continue;
            const QVariant stored = button->property("clicknova.english");
            const QString english = stored.isValid() ? stored.toString() : button->text();
            button->setProperty("clicknova.english", english);
            button->setText(uiText(english));
        }
        for (auto* box : findChildren<QCheckBox*>()) {
            const QVariant stored = box->property("clicknova.english");
            const QString english = stored.isValid() ? stored.toString() : box->text();
            box->setProperty("clicknova.english", english);
            box->setText(uiText(english));
        }
        for (auto* combo : findChildren<QComboBox*>()) {
            if (combo == targetWindows_ || combo == profiles_ || combo == languageSelector_ ||
                combo == clickScenarios_ || combo == sequenceScenarios_) continue;
            const QVariant existing = combo->property("clicknova.englishItems");
            QStringList source;
            if (existing.isValid()) source = existing.toStringList();
            else for (int i = 0; i < combo->count(); ++i) source.append(combo->itemText(i));
            combo->setProperty("clicknova.englishItems", source);
            for (int i = 0; i < source.size() && i < combo->count(); ++i)
                combo->setItemText(i, uiText(source[i]));
        }
        for (int i = 0; i < navigation_->count(); ++i) {
            static const QStringList names = {"Auto Clicker", "Sequence", "Settings"};
            if (i < names.size()) navigation_->item(i)->setText(uiText(names[i]));
        }
        if (clickConfigTabs_) {
            clickConfigTabs_->setTabText(0, uiText(QStringLiteral("Click Position")));
            clickConfigTabs_->setTabText(1, uiText(QStringLiteral("Target Window")));
        }
        if (settingsTabs_) {
            settingsTabs_->setTabText(0, uiText(QStringLiteral("Profiles")));
            settingsTabs_->setTabText(1, uiText(QStringLiteral("Shortcuts")));
            settingsTabs_->setTabText(2, uiText(QStringLiteral("Language")));
        }
        if (profiles_) profiles_->setPlaceholderText(uiText(QStringLiteral("Choose a saved profile")));
        if (clickScenarios_) clickScenarios_->setPlaceholderText(uiText(QStringLiteral("Select a saved scenario")));
        if (sequenceScenarios_) sequenceScenarios_->setPlaceholderText(uiText(QStringLiteral("Select a saved scenario")));
        if (targetWindows_) targetWindows_->setPlaceholderText(uiText(QStringLiteral("Select a running window")));
        if (positionMode_) positionMode_->setItemText(1, uiText(targetMode_->currentIndex() == 1
            ? QStringLiteral("Fixed client coordinates") : QStringLiteral("Fixed screen coordinates")));
        if (sequenceEditor_) sequenceEditor_->setLanguage(language_);
        if (inputLab_) inputLab_->setRussian(language_ == QStringLiteral("ru"));
        refreshShortcutLabels();
        updateQuickOptions();
        updateTargetInputs();
        refreshRunToolbar();
        if (logDock_) logDock_->setWindowTitle(language_ == QStringLiteral("ru") ?
            QStringLiteral("Журнал") : QStringLiteral("Activity log"));
        if (auto* toggle = findChild<QAction*>(QStringLiteral("toggleActivityLog")))
            toggle->setText(language_ == QStringLiteral("ru") ? QStringLiteral("Журнал") : QStringLiteral("Log"));
        if (auto* clearLog = findChild<QPushButton*>(QStringLiteral("clearActivityLog")))
            clearLog->setText(language_ == QStringLiteral("ru") ? QStringLiteral("Очистить") : QStringLiteral("Clear log"));
        if (status_) status_->setText(uiText(lastStatusEnglish_.isEmpty()
                                    ? QStringLiteral("Ready") : lastStatusEnglish_));
        if (workflowFeedback_ && workflowFeedback_->isVisible())
            workflowFeedback_->setText(uiText(lastStatusEnglish_));
        applyHelpTips();
    }

    void applyHelpTips() {
        const auto help = [this](QWidget* widget, const QString& message) {
            if (!widget) return;
            const QString localized = uiText(message);
            widget->setToolTip(localized);
            widget->setWhatsThis(localized);
        };
        help(navigation_, QStringLiteral("Choose Auto Clicker for quick clicks, Sequence for mouse, keyboard, text and recordings, or Settings for preferences."));
        help(button_, QStringLiteral("Mouse button used for clicks, drags or holding."));
        help(quickAction_, QStringLiteral("Choose click, move without pressing, drag with a held button, or hold until Stop."));
        help(quickMoveX_, QStringLiteral("Destination X coordinate. Capture a point or type a value."));
        help(quickMoveY_, QStringLiteral("Destination Y coordinate. Capture a point or type a value."));
        help(quickMoveDuration_, QStringLiteral("How long the cursor travels to the destination, in milliseconds."));
        help(quickCaptureDestination_, QStringLiteral("Hide ClickNova and capture the end point with a countdown or capture shortcut."));
        help(quickChooseButton_, QStringLiteral("Detect Left, Right, Middle, X1 and X2 while the capture dialog is open. Vendor-only buttons may not generate Windows mouse events."));
        help(interval_, QStringLiteral("Wait between ordinary Auto Clicker clicks in milliseconds."));
        help(startDelayValue_, QStringLiteral("Wait before the first Auto Clicker action. Zero starts immediately; Stop cancels the countdown."));
        help(startDelayUnit_, QStringLiteral("Choose seconds, minutes, hours or days for the start timer."));
        help(startMode_, QStringLiteral("Choose a delay after pressing Start or a specific local Windows clock time."));
        help(startAtTime_, QStringLiteral("Uses Windows local time. If the chosen time has passed today, start tomorrow. Stop cancels the wait."));
        help(quickHoldDuration_, QStringLiteral("Keep each mouse button pressed for this duration before releasing it. Zero means an instant click."));
        help(infinite_, QStringLiteral("Keep clicking until you press Stop or the stop shortcut."));
        help(count_, QStringLiteral("Total clicks for Auto Clicker when unlimited mode is off."));
        help(customizeClicks_, QStringLiteral("Turn a limited run into separate editable sequence actions."));
        help(quickPosition_, QStringLiteral("Enable a fixed click point instead of the current cursor position."));
        help(editPosition_, QStringLiteral("Capture a point on the screen. ClickNova hides during capture."));
        help(quickWindow_, QStringLiteral("Restrict clicking to a chosen foreground window. This is not background input."));
        help(editWindow_, QStringLiteral("Choose an application window to limit clicks to."));
        help(chooseWindow_, QStringLiteral("Select a running application by its window title or process ID."));
        help(refreshWindows_, QStringLiteral("Refresh the list of currently available windows."));
        help(captureWindow_, QStringLiteral("Select a window by hovering over it during the countdown."));
        help(positionMode_, QStringLiteral("Use the cursor at runtime or the saved X and Y coordinates."));
        help(x_, QStringLiteral("Horizontal coordinate of the click position."));
        help(y_, QStringLiteral("Vertical coordinate of the click position."));
        help(positionCaptureSeconds_, QStringLiteral("Seconds before the cursor location is captured. The shortcut can finish early."));
        help(windowCaptureSeconds_, QStringLiteral("Seconds before selecting the window under the cursor."));
        help(targetMode_, QStringLiteral("Global clicks affect the desktop; window targeting requires foreground focus."));
        help(targetWindows_, QStringLiteral("Choose the application window that should receive clicks."));
        help(sequenceRepeatCount_, QStringLiteral("How many complete passes through the sequence to execute."));
        help(sequenceUnlimited_, QStringLiteral("Repeat the full sequence until Stop is pressed."));
        help(optionalDetails_, QStringLiteral("Show or hide less commonly used position and window settings."));
        help(clickScenarios_, QStringLiteral("Saved single-click setups; loading one does not replace a sequence."));
        help(sequenceScenarios_, QStringLiteral("Saved sequences with their actions and timing."));
        help(profiles_, QStringLiteral("Full profiles include settings from both workspaces."));
        help(start_, QStringLiteral("Start the workflow selected on the left. Stop remains available at all times."));
        help(stop_, QStringLiteral("Immediately stop the current automation."));
        help(startShortcut_, QStringLiteral("Shortcut to start automation when enabled."));
        help(stopShortcut_, QStringLiteral("Shortcut to stop automation even when another window is active."));
        help(pickShortcut_, QStringLiteral("Shortcut to capture the hovered position while picking a point."));
        for (int row = 0; row < navigation_->count(); ++row) {
            static const QStringList info = {
                QStringLiteral("Set up single clicks, speed, count and an optional position or window."),
                QStringLiteral("Create, edit, reorder and run a sequence of actions."),
                QStringLiteral("Manage saved scenarios, shortcuts and language.")};
            if (row < info.size()) navigation_->item(row)->setToolTip(uiText(info[row]));
        }
    }

    ShortcutOptions shortcutValues() const {
        ShortcutOptions value;
        value.enableStart = enableStartShortcut_->isChecked();
        value.enablePick = enablePickShortcut_->isChecked();
        value.start = startShortcut_->keySequence();
        value.stop = stopShortcut_->keySequence();
        value.pick = pickShortcut_->keySequence();
        return value;
    }
    QString stopHotkeyLabel() const {
        return stopShortcut_->keySequence().toString(QKeySequence::NativeText);
    }
    void refreshShortcutLabels() {
        const auto start = startShortcut_->keySequence().toString(QKeySequence::NativeText);
        start_->setText(start.isEmpty() || !enableStartShortcut_->isChecked()
            ? uiText(QStringLiteral("Start")) : uiText(QStringLiteral("Start · %1")).arg(start));
        stop_->setText(uiText(QStringLiteral("Stop · %1")).arg(stopHotkeyLabel()));
        if (inputLab_) inputLab_->refreshStartLabel();
        capture_->setText(uiText(QStringLiteral("Timed capture (%1 s)")).arg(positionCaptureSeconds_->value()));
        if (editPosition_) editPosition_->setText(
            uiText(QStringLiteral("Pick point in %1 s...")).arg(positionCaptureSeconds_->value()));
        if (sequenceEditor_) sequenceEditor_->setPickShortcutText(
            pickShortcut_->keySequence().toString(QKeySequence::NativeText), enablePickShortcut_->isChecked());
    }
    void restoreShortcutSettings() {
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        const QSignalBlocker a(enableStartShortcut_);
        const QSignalBlocker b(enablePickShortcut_);
        const QSignalBlocker c(startShortcut_);
        const QSignalBlocker d(stopShortcut_);
        const QSignalBlocker e(pickShortcut_);
        enableStartShortcut_->setChecked(settings.value(QStringLiteral("shortcuts/enableStart"), true).toBool());
        enablePickShortcut_->setChecked(settings.value(QStringLiteral("shortcuts/enablePick"), true).toBool());
        startShortcut_->setKeySequence(QKeySequence(settings.value(QStringLiteral("shortcuts/start"),
                                                                    QStringLiteral("F6")).toString()));
        stopShortcut_->setKeySequence(QKeySequence(settings.value(QStringLiteral("shortcuts/stop"),
                                                                  QStringLiteral("F7")).toString()));
        pickShortcut_->setKeySequence(QKeySequence(settings.value(QStringLiteral("shortcuts/pick"),
                                                                  QStringLiteral("F8")).toString()));
        startShortcut_->setEnabled(enableStartShortcut_->isChecked());
        pickShortcut_->setEnabled(enablePickShortcut_->isChecked());
#ifdef _WIN32
        if (!hotkeys_->configure(shortcutValues())) {
            // Always recover from corrupt or obsolete settings with known defaults.
            enableStartShortcut_->setChecked(true);
            enablePickShortcut_->setChecked(true);
            startShortcut_->setKeySequence(QKeySequence(QStringLiteral("F6")));
            stopShortcut_->setKeySequence(QKeySequence(QStringLiteral("F7")));
            pickShortcut_->setKeySequence(QKeySequence(QStringLiteral("F8")));
            hotkeys_->configure(shortcutValues());
        }
#endif
        refreshShortcutLabels();
    }
    void applyShortcutEdits() {
        if (active_ || capturePending_) return;
#ifdef _WIN32
        if (!hotkeys_) return;
        const auto updated = shortcutValues();
        hotkeys_->resume();
        if (!hotkeys_->configure(updated)) {
            const ShortcutOptions previous = hotkeys_->configured();
            const QSignalBlocker a(enableStartShortcut_);
            const QSignalBlocker b(enablePickShortcut_);
            const QSignalBlocker c(startShortcut_);
            const QSignalBlocker d(stopShortcut_);
            const QSignalBlocker e(pickShortcut_);
            enableStartShortcut_->setChecked(previous.enableStart);
            enablePickShortcut_->setChecked(previous.enablePick);
            startShortcut_->setKeySequence(previous.start);
            stopShortcut_->setKeySequence(previous.stop);
            pickShortcut_->setKeySequence(previous.pick);
            setStatus(QStringLiteral("Shortcut conflict or unsupported combination. Previous settings restored."));
            startShortcut_->setEnabled(previous.enableStart);
            pickShortcut_->setEnabled(previous.enablePick);
            refreshShortcutLabels();
            return;
        }
#endif
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.setValue(QStringLiteral("shortcuts/enableStart"), updated.enableStart);
        settings.setValue(QStringLiteral("shortcuts/enablePick"), updated.enablePick);
        settings.setValue(QStringLiteral("shortcuts/start"), updated.start.toString(QKeySequence::PortableText));
        settings.setValue(QStringLiteral("shortcuts/stop"), updated.stop.toString(QKeySequence::PortableText));
        settings.setValue(QStringLiteral("shortcuts/pick"), updated.pick.toString(QKeySequence::PortableText));
        refreshShortcutLabels();
        setStatus(QStringLiteral("Keyboard shortcuts saved."));
    }
    void setShortcutEditorsEnabled(bool enabled) {
        enableStartShortcut_->setEnabled(enabled);
        enablePickShortcut_->setEnabled(enabled);
        startShortcut_->setEnabled(enabled && enableStartShortcut_->isChecked());
        stopShortcut_->setEnabled(enabled);
        pickShortcut_->setEnabled(enabled && enablePickShortcut_->isChecked());
    }
    void start() {
        if (inputLab_ && inputLab_->isRunning()) {
            setStatus(QStringLiteral("Stop the sequence editor before starting another workflow."));
            return;
        }
        if (quickSequence_ && quickSequence_->isChecked() && !legacyRunRequested_) {
            if (inputLab_) inputLab_->requestRun();
            return;
        }
#ifndef _WIN32
        return;
#else
        if (active_ || capturePending_) return;
        const bool needsDefaultWindow = targetMode_->currentIndex() == 1 &&
            (!quickSequence_->isChecked() ||
             std::any_of(sequenceEditor_->steps().begin(), sequenceEditor_->steps().end(),
                         [](const auto& step) { return !step.ownWindow; }));
        if (needsDefaultWindow) {
            if (!selectedTarget_) {
                setStatus(QStringLiteral("Select a target window before starting."));
                return;
            }
            // A missing/reused HWND must never trigger a global click.
            const auto state = clicknova::platform::checkTarget(*selectedTarget_);
            if (state != clicknova::platform::TargetCheck::Ready &&
                state != clicknova::platform::TargetCheck::NotForeground) {
                setStatus(QString::fromUtf8(clicknova::platform::targetCheckMessage(state)));
                return;
            }
        }
        if (quickSequence_->isChecked()) {
            if (sequenceEditor_->hasUnsavedActionChanges()) {
                navigation_->setCurrentRow(1);
                setStatus(QStringLiteral("Apply changes to the selected action before starting."));
                return;
            }
            clicknova::sequence::Plan plan{sequenceEditor_->steps(), sequenceRepeatCount_->value(),
                                            sequenceUnlimited_->isChecked(), sequenceEditor_->passDelayMs()};
            const auto error = clicknova::sequence::validate(plan);
            if (!error.empty()) {
                navigation_->setCurrentRow(1);
                setStatus(QString::fromStdString(error));
                return;
            }
            for (const auto& step : plan.steps) {
                const auto expected = (step.ownWindow || targetMode_->currentIndex() == 1)
                    ? clicknova::sequence::Space::Client : clicknova::sequence::Space::Screen;
                if (step.space != expected) {
                    navigation_->setCurrentRow(1);
                    setStatus(QStringLiteral("Sequence coordinates do not match the action's target window."));
                    return;
                }
                if (step.ownWindow && (!step.windowHandle || !step.processId)) {
                    navigation_->setCurrentRow(1);
                    setStatus(QStringLiteral("Re-select the window for each bound action before starting."));
                    return;
                }
            }
            sequenceCursor_ = std::make_unique<clicknova::sequence::Cursor>(std::move(plan));
        } else sequenceCursor_.reset();
        if (!quickSequence_->isChecked() &&
            (quickAction_->currentIndex() == 1 || quickAction_->currentIndex() == 2) &&
            !destinationConfigured_) {
            setStatus(QStringLiteral("Choose a destination point before starting movement."));
            return;
        }
        // Compute wall-clock delay ONCE at Start. Once scheduled, use the
        // monotonic timer so any later Windows clock adjustments cannot cause
        // an unexpectedly early or repeated click. Like WaitUntil, a time
        // already passed today schedules the next calendar day (DST aware).
        const bool atClock = startMode_->currentIndex() == 1;
        const QTime selectedClock = startAtTime_->time();
        const std::optional<std::int64_t> requestedDelay = atClock
            ? std::optional<std::int64_t>{clicknova::input::millisecondsUntil(
                  {selectedClock.hour(), selectedClock.minute(), selectedClock.second()},
                  std::chrono::system_clock::now())}
            : clicknova::timing::startDelayMs(
                  static_cast<std::uint64_t>(startDelayValue_->value()),
                  startDelayUnit_->currentIndex());
        if (!requestedDelay || *requestedDelay < 0) {
            setStatus(QStringLiteral("Invalid start delay."));
            return;
        }
        // Reserve Stop only for the running session. Idle shortcuts do not
        // steal keys from games, editors or other applications.
        if (hotkeys_ && !hotkeys_->setRunning(true)) {
            navigation_->setCurrentRow(2);
            if (settingsTabs_) settingsTabs_->setCurrentIndex(1);
            setStatus(QStringLiteral("Stop shortcut is in use. Choose another shortcut before starting."));
            return;
        }
        saveLastSettings();
        // Quick mode runs on the GUI thread; request and release wake state
        // on that same thread, including the initial countdown.
        if (keepAwakeQuick_->isChecked()) awakeQuickGuard_.setEnabled(true);
        active_ = true;
        updateScenarioButtons();
        sequenceEditor_->setLocked(true);
        setShortcutEditorsEnabled(false);
        done_ = 0;
        // Monotonic elapsed time remains correct after any UI stalls during
        // multi-day waits. No QTimer uses an interval greater than 1000 ms.
        startDelayMs_ = *requestedDelay;
        countdownPending_ = startDelayMs_ != 0;
        countdownClock_.start();
        button_->setEnabled(false);
        quickAction_->setEnabled(false);
        quickMoveX_->setEnabled(false);
        quickMoveY_->setEnabled(false);
        quickMoveDuration_->setEnabled(false);
        quickCaptureDestination_->setEnabled(false);
        interval_->setEnabled(false);
        quickHoldDuration_->setEnabled(false);
        count_->setEnabled(false);
        infinite_->setEnabled(false);
        startDelayValue_->setEnabled(false);
        startDelayUnit_->setEnabled(false);
        startMode_->setEnabled(false);
        startAtTime_->setEnabled(false);
        positionMode_->setEnabled(false);
        x_->setEnabled(false);
        y_->setEnabled(false);
        capture_->setEnabled(false);
        profiles_->setEnabled(false);
        saveProfile_->setEnabled(false);
        loadProfile_->setEnabled(false);
        deleteProfile_->setEnabled(false);
        updateScenarioButtons();
        updateTargetInputs();
        start_->setEnabled(false);
        stop_->setEnabled(true);
        if (countdownPending_) countdownTick();
        else beginClicking();
#endif
    }
    QString countdownText(std::int64_t seconds) const {
        // Show the largest useful units instead of a huge seconds value.
        const auto days = seconds / 86400;
        const auto hours = (seconds / 3600) % 24;
        const auto minutes = (seconds / 60) % 60;
        const auto rest = seconds % 60;
        QStringList pieces;
        const bool ru = language_ == QStringLiteral("ru");
        if (days) pieces << QStringLiteral("%1 %2").arg(days).arg(ru ? QStringLiteral("д") : QStringLiteral("d"));
        if (hours) pieces << QStringLiteral("%1 %2").arg(hours).arg(ru ? QStringLiteral("ч") : QStringLiteral("h"));
        if (minutes) pieces << QStringLiteral("%1 min").arg(minutes);
        if (rest || pieces.isEmpty()) pieces << QStringLiteral("%1 s").arg(rest);
        return pieces.join(QLatin1Char(' '));
    }
    void beginClicking() {
        if (!active_) return;
        countdownPending_ = false;
        countdownTimer_.stop();
        // The start timer determines the first click, not the click interval.
        // Subsequent clicks remain spaced by interval_->value().
        if (sequenceCursor_) {
            if (sequenceCursor_->peek()) clickTimer_.start(sequenceCursor_->nextDelayMs());
            else { stop(); return; }
        } else clickTimer_.start(0);
        setStatus(QStringLiteral("Running · %1 to stop").arg(stopHotkeyLabel()));
    }
    void countdownTick() {
        if (!active_ || !countdownPending_) return;
        const auto remaining = clicknova::timing::remainingDelayMs(
            startDelayMs_, countdownClock_.elapsed());
        if (remaining <= 0) { beginClicking(); return; }
        const auto seconds = clicknova::timing::remainingWholeSeconds(
            startDelayMs_, countdownClock_.elapsed());
        const QString focus = targetMode_->currentIndex() == 1
            ? QStringLiteral("Focus the selected window.")
            : QStringLiteral("Position the cursor safely.");
        const bool atClock = startMode_->currentIndex() == 1;
        const QString when = atClock
            ? QDateTime::currentDateTime().addMSecs(remaining).toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"))
            : QString{};
        setStatus(atClock
            ? (language_ == QStringLiteral("ru")
                ? QStringLiteral("Запуск в %1 · осталось %2 · %3 Остановка: %4")
                    .arg(when, countdownText(seconds),
                         targetMode_->currentIndex() == 1
                             ? QStringLiteral("Перейдите в выбранное окно.")
                             : QStringLiteral("Переместите курсор в безопасное место."),
                         stopHotkeyLabel())
                : QStringLiteral("Starting at %1 · in %2 · %3 %4 to stop")
                    .arg(when, countdownText(seconds), focus, stopHotkeyLabel()))
            : language_ == QStringLiteral("ru")
            ? QStringLiteral("Запуск через %1 · %2 Остановка: %3")
                .arg(countdownText(seconds),
                     targetMode_->currentIndex() == 1
                         ? QStringLiteral("Перейдите в выбранное окно.")
                         : QStringLiteral("Переместите курсор в безопасное место."),
                     stopHotkeyLabel())
            : QStringLiteral("Starting in %1 · %2 %3 to stop")
                .arg(countdownText(seconds), focus, stopHotkeyLabel()));
        countdownTimer_.start(static_cast<int>(std::min<std::int64_t>(remaining, 1000)));
    }
    void quickMotionTick() {
#ifdef _WIN32
        if (!active_ || !quickMotionActive_) { quickMotionTimer_.stop(); return; }
        const auto point = clicknova::quick::sample(quickMotion_,
            static_cast<int>(quickMotionClock_.elapsed()));
        if (!point) { stopWithReason(QStringLiteral("Invalid movement parameters.")); return; }
        if (quickMotionWindow_) {
            const auto checked = clicknova::platform::checkScreenPoint(
                *quickMotionWindow_, point->x, point->y);
            if (checked != clicknova::platform::TargetCheck::Ready) {
                stopWithReason(QString::fromUtf8(clicknova::platform::targetCheckMessage(checked)));
                return;
            }
        }
        clicknova::input::WindowsInputBackend backend;
        clicknova::input::Event movement;
        movement.kind = clicknova::input::EventKind::PointerMove;
        movement.absolute = true;
        movement.x = point->x;
        movement.y = point->y;
        if (!backend.dispatch(movement)) {
            stopWithReason(QStringLiteral("Windows rejected cursor movement."));
            return;
        }
        if (quickMotionClock_.elapsed() < quickMotion_.durationMs) return;
        quickMotionTimer_.stop();
        quickMotionActive_ = false;
        quickMotionWindow_.reset();
        if (quickMouseDown_) {
            clicknova::input::Event release;
            release.kind = clicknova::input::EventKind::ButtonRelease;
            release.button = quickHeldButton_;
            if (!backend.dispatch(release)) {
                stopWithReason(QStringLiteral("Windows could not release the mouse button."));
                return;
            }
            quickMouseDown_ = false;
        }
        finishClickCycle();
#endif
    }
    void click() {
        if (!active_) return;
#ifdef _WIN32
        const auto step = sequenceCursor_ ? sequenceCursor_->peek() : std::nullopt;
        if (sequenceCursor_ && !step) { stop(); return; }
        const int buttonIndex = step ? step->button : button_->currentIndex();
        int screenX = step ? step->x : x_->value();
        int screenY = step ? step->y : y_->value();
        std::optional<clicknova::platform::WindowTarget> clickWindow;
        if (step && step->ownWindow) {
            clickWindow = clicknova::platform::WindowTarget{
                step->windowHandle, step->processId,
                QString::fromUtf8(step->windowTitle.c_str()).toStdWString()};
        } else if (targetMode_->currentIndex() == 1) clickWindow = selectedTarget_;
        if (clickWindow) {
            using clicknova::platform::TargetCheck;
            TargetCheck state = TargetCheck::Missing;
            if (step || positionMode_->currentIndex() == 1) {
                state = clicknova::platform::resolveClientPoint(
                    *clickWindow, step ? step->x : x_->value(),
                    step ? step->y : y_->value(), screenX, screenY);
            } else {
                POINT cursor{};
                if (GetCursorPos(&cursor)) {
                    screenX = cursor.x; screenY = cursor.y;
                    state = clicknova::platform::checkScreenPoint(*clickWindow, screenX, screenY);
                }
            }
            if (state != TargetCheck::Ready) {
                stopWithReason(QString::fromUtf8(clicknova::platform::targetCheckMessage(state)));
                return;
            }
        } else if (step && step->ownWindow) {
            stopWithReason(QStringLiteral("Action window is not selected."));
            return;
        }
        if ((step || positionMode_->currentIndex() == 1) && !SetCursorPos(screenX, screenY)) {
            stopWithReason(QStringLiteral("Windows could not move the cursor to the selected position."));
            return;
        }
        // Re-check immediately before injection: an overlay or focus change must stop us.
        if (clickWindow) {
            const auto state = clicknova::platform::checkScreenPoint(*clickWindow, screenX, screenY);
            if (state != clicknova::platform::TargetCheck::Ready) {
                stopWithReason(QString::fromUtf8(clicknova::platform::targetCheckMessage(state)));
                return;
            }
        }
        // Ordinary quick actions run separately from the legacy sequence cursor.
        const int quickMode = sequenceCursor_ ? 0 : quickAction_->currentIndex();
        if (quickMode == 1 || quickMode == 2) {
            if (!step && positionMode_->currentIndex() == 0) {
                POINT current{};
                if (!GetCursorPos(&current)) {
                    stopWithReason(QStringLiteral("Unable to read the current cursor position."));
                    return;
                }
                screenX = current.x;
                screenY = current.y;
            }
            int toX = quickMoveX_->value();
            int toY = quickMoveY_->value();
            if (clickWindow) {
                const int clientX = toX;
                const int clientY = toY;
                const auto checked = clicknova::platform::resolveClientPoint(
                    *clickWindow, clientX, clientY, toX, toY);
                if (checked != clicknova::platform::TargetCheck::Ready) {
                    stopWithReason(QString::fromUtf8(clicknova::platform::targetCheckMessage(checked)));
                    return;
                }
            }
            quickMotion_ = clicknova::quick::Motion{{screenX, screenY}, {toX, toY},
                                                   quickMoveDuration_->value()};
            if (!clicknova::quick::sample(quickMotion_, 0)) {
                stopWithReason(QStringLiteral("Invalid movement parameters."));
                return;
            }
            if (quickMode == 2) {
                clicknova::input::WindowsInputBackend backend;
                clicknova::input::Event down;
                down.kind = clicknova::input::EventKind::ButtonPress;
                down.button = static_cast<clicknova::input::Button>(buttonIndex);
                if (!backend.dispatch(down)) {
                    stopWithReason(QStringLiteral("Windows rejected the mouse press."));
                    return;
                }
                quickHeldButton_ = down.button;
                quickMouseDown_ = true;
            }
            quickMotionWindow_ = clickWindow;
            quickMotionActive_ = true;
            quickMotionClock_.start();
            quickMotionTick();
            if (quickMotionActive_) quickMotionTimer_.start();
            return;
        }
        // Each click is a press and release; in Quick mode release can be delayed.
        // The timer stays on the GUI thread so Stop can release immediately.
        const auto physicalButton = static_cast<clicknova::input::Button>(buttonIndex);
        clicknova::input::WindowsInputBackend backend;
        clicknova::input::Event event;
        event.kind = clicknova::input::EventKind::ButtonPress;
        event.button = physicalButton;
        if (!backend.dispatch(event)) {
            stopWithReason(QStringLiteral("Windows rejected the mouse press."));
            return;
        }
        quickHeldButton_ = physicalButton;
        quickMouseDown_ = true;
        if (quickMode == 3) {
            setStatus(QStringLiteral("Mouse button held. Press Stop (%1) to release it.")
                .arg(stopHotkeyLabel()));
            return;
        }
        const int duration = sequenceCursor_ ? 0 : quickHoldDuration_->value();
        if (duration > 0) {
            quickReleaseTimer_.start(duration);
            return;
        }
        event.kind = clicknova::input::EventKind::ButtonRelease;
        if (!backend.dispatch(event)) {
            stopWithReason(QStringLiteral("Windows rejected the mouse release."));
            return;
        }
        quickMouseDown_ = false;
        finishClickCycle();
#endif
    }
    void finishClickCycle() {
        if (!active_) return;
        ++done_;
        setStatus(QStringLiteral("Running · clicks: %1 · %2 to stop").arg(static_cast<qulonglong>(done_)).arg(stopHotkeyLabel()));
        if (sequenceCursor_) {
            sequenceCursor_->advance();
            const auto next = sequenceCursor_->peek();
            if (next) clickTimer_.start(sequenceCursor_->nextDelayMs());
            else stop();
        } else if (!infinite_->isChecked() && done_ >= count_->value()) {
            stop();
        } else clickTimer_.start(interval_->value());
    }
    void stop() {
        awakeQuickGuard_.setEnabled(false);
        if (inputLab_ && inputLab_->isRunning()) inputLab_->cancel();
        clickTimer_.stop();
        countdownTimer_.stop();
        countdownPending_ = false;
        quickReleaseTimer_.stop();
        quickMotionTimer_.stop();
        quickMotionActive_ = false;
        quickMotionWindow_.reset();
#ifdef _WIN32
        const bool mustRelease = quickMouseDown_;
        quickMouseDown_ = false;
        if (mustRelease) {
            clicknova::input::WindowsInputBackend backend;
            clicknova::input::Event event;
            event.kind = clicknova::input::EventKind::ButtonRelease;
            event.button = quickHeldButton_;
            if (!backend.dispatch(event))
                qWarning() << "Windows could not release the held mouse button";
        }
#endif
        if (!active_) return;
        active_ = false;
        updateScenarioButtons();
        sequenceCursor_.reset();
        sequenceEditor_->setLocked(false);
#ifdef _WIN32
        if (hotkeys_) hotkeys_->setRunning(false);
#endif
        setShortcutEditorsEnabled(true);
        button_->setEnabled(true);
        quickAction_->setEnabled(true);
        interval_->setEnabled(true);
        quickHoldDuration_->setEnabled(true);
        infinite_->setEnabled(true);
        startDelayValue_->setEnabled(true);
        startDelayUnit_->setEnabled(true);
        startMode_->setEnabled(true);
        startAtTime_->setEnabled(true);
        count_->setEnabled(!infinite_->isChecked());
        positionMode_->setEnabled(true);
        updatePositionInputs();
        profiles_->setEnabled(true);
        saveProfile_->setEnabled(true);
        updateProfileActions();
        start_->setEnabled(true);
        stop_->setEnabled(false);
        updateTargetInputs();
        updateQuickOptions();
        setStatus(QStringLiteral("Stopped · actions completed: %1").arg(static_cast<qulonglong>(done_)));
    }

    void stopWithReason(const QString& reason) {
        stop();
        setStatus(reason + QStringLiteral(" Automation stopped."));
    }
    void convertQuickClicksToSequence() {
        if (!inputLab_ || inputLab_->isRunning() || active_ || capturePending_) return;
        const bool bound = targetMode_->currentIndex() == 1;
        const int mode = quickAction_->currentIndex();
        if (mode == 3) {
            QMessageBox::information(this, QStringLiteral("ClickNova"), uiText(
                QStringLiteral("Hold-until-Stop cannot be represented as a finite sequence. Add a Mouse Hold action in Steps.")));
            return;
        }
        int count = count_->value();
        if (infinite_->isChecked()) {
            bool ok = false;
            count = QInputDialog::getInt(this, QStringLiteral("ClickNova"), uiText(
                QStringLiteral("Enter a finite number of repetitions to convert:")), 1, 1, std::numeric_limits<int>::max(), 1, &ok);
            if (!ok) return;
        }
        if (bound && !selectedTarget_) {
            setStatus(QStringLiteral("Choose a target window before converting window-bound clicks."));
            return;
        }
        clicknova::input::Plan plan;
        plan.requireTargetWindow = bound;
        const auto moveMode = bound ? clicknova::input::MoveMode::WindowClient
                                    : clicknova::input::MoveMode::Absolute;
        const clicknova::input::Button clickButton = static_cast<clicknova::input::Button>(button_->currentIndex());
        const bool fixed = positionMode_->currentIndex() == 1;
        const auto append = [&] {
            if (fixed) plan.actions.emplace_back(clicknova::input::MovePointer{
                moveMode, x_->value(), y_->value(), 0});
            if (mode == 0) {
                if (quickHoldDuration_->value() > 0) plan.actions.emplace_back(
                    clicknova::input::MouseHold{clickButton, quickHoldDuration_->value()});
                else plan.actions.emplace_back(clicknova::input::Click{clickButton, 1, 0});
            } else {
                if (!destinationConfigured_) return false;
                if (mode == 2) plan.actions.emplace_back(clicknova::input::ButtonDown{clickButton});
                plan.actions.emplace_back(clicknova::input::MovePointer{moveMode,
                    quickMoveX_->value(), quickMoveY_->value(), quickMoveDuration_->value()});
                if (mode == 2) plan.actions.emplace_back(clicknova::input::ButtonUp{clickButton});
            }
            return true;
        };
        if (mode != 0 && !destinationConfigured_) {
            setStatus(QStringLiteral("Capture the motion destination before converting."));
            return;
        }
        const int actionsPerClick = (fixed ? 1 : 0) + (mode == 2 ? 3 : 1);
        // Small finite macros become individually editable steps. Larger ones
        // use repeat-count to respect the 512-action memory/safety limit.
        const bool expand = static_cast<std::int64_t>(count) * (actionsPerClick + 1) <= 512;
        if (expand) {
            for (int i = 0; i < count; ++i) {
                if (i) plan.actions.emplace_back(clicknova::input::Wait{interval_->value()});
                if (!append()) return;
            }
        } else {
            plan.repetitions = count;
            plan.passDelayMs = interval_->value();
            if (!append()) return;
        }
        if (const auto error = clicknova::input::validate(plan); !error.empty()) {
            QMessageBox::warning(this, QStringLiteral("ClickNova"), QString::fromStdString(error));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("ClickNova"), uiText(
            QStringLiteral("Replace the common action list with these clicks?"))) != QMessageBox::Yes) return;
        inputLab_->replaceActions(plan);
        navigation_->setCurrentRow(1);
        setStatus(QStringLiteral("Quick actions transferred to the common sequence editor."));
    }
    void updateQuickOptions() {
        if (!quickPosition_ || !quickWindow_ || !positionMode_ || !targetMode_) return;
        const bool fixed = positionMode_->currentIndex() == 1;
        const bool bound = targetMode_->currentIndex() == 1;
        const bool sequenced = quickSequence_ && quickSequence_->isChecked();
        {
            const QSignalBlocker blockPosition(quickPosition_);
            const QSignalBlocker blockWindow(quickWindow_);
            quickPosition_->setChecked(fixed);
            quickWindow_->setChecked(bound);
        }
        const bool editable = !active_ && !capturePending_;
        if (quickClickGroup_) quickClickGroup_->setVisible(!sequenced);
        if (sequenceEnableOnPage_) {
            const QSignalBlocker blockingSequence(sequenceEnableOnPage_);
            sequenceEnableOnPage_->setChecked(sequenced);
            sequenceEnableOnPage_->setEnabled(editable);
        }
        if (sequenceUnlimited_) sequenceUnlimited_->setEnabled(editable);
        if (sequenceRepeatCount_)
            sequenceRepeatCount_->setEnabled(editable && !sequenceUnlimited_->isChecked());
        if (sequenceReady_ && sequenceEditor_) {
            const auto& actions = sequenceEditor_->steps();
            const int incomplete = static_cast<int>(std::count_if(actions.begin(), actions.end(),
                [](const auto& action) { return !action.configured; }));
            const bool windowUnbound = std::any_of(actions.begin(), actions.end(),
                [](const auto& action) { return action.ownWindow && (!action.windowHandle || !action.processId); });
            if (actions.empty())
                sequenceReady_->setText(uiText(QStringLiteral("No actions yet. Add an action to begin.")));
            else if (incomplete)
                sequenceReady_->setText(uiText(QStringLiteral("%1 actions still need a click position.")).arg(incomplete));
            else if (windowUnbound)
                sequenceReady_->setText(uiText(QStringLiteral("Some actions need their target windows selected again.")));
            else if (bound && !selectedTarget_ &&
                     std::any_of(actions.begin(), actions.end(), [](const auto& action) { return !action.ownWindow; }))
                sequenceReady_->setText(uiText(QStringLiteral("Choose a default target window before starting.")));
            else
                sequenceReady_->setText(uiText(QStringLiteral("Sequence ready: %1 actions.")).arg(static_cast<int>(actions.size())));
        }
        if (sequencePreview_ && sequenceEditor_ && sequenceUnlimited_ && sequenceRepeatCount_) {
            const clicknova::sequence::Plan plan{
                sequenceEditor_->steps(), sequenceRepeatCount_->value(),
                sequenceUnlimited_->isChecked(), sequenceEditor_->passDelayMs()};
            const auto summary = clicknova::sequence::preview(plan);
            if (!summary) {
                sequencePreview_->setText(uiText(QStringLiteral(
                    "Complete the actions to see the estimated clicks and waits.")));
            } else {
                const auto onePass = uiText(QStringLiteral(
                    "One pass: %1 clicks · %2 ms of scheduled waits"))
                    .arg(QString::number(static_cast<qulonglong>(summary->clicksPerPass)))
                    .arg(QString::number(static_cast<qulonglong>(summary->waitMsPerPass)));
                const auto allPasses = summary->totalClicks
                    ? uiText(QStringLiteral("All passes: %1 clicks · %2 ms of scheduled waits"))
                        .arg(QString::number(static_cast<qulonglong>(*summary->totalClicks)))
                        .arg(QString::number(static_cast<qulonglong>(*summary->totalWaitMs)))
                    : uiText(QStringLiteral("Unlimited repetition · no fixed total"));
                sequencePreview_->setText(onePass + QLatin1Char('\n') + allPasses +
                    QLatin1Char('\n') + uiText(QStringLiteral(
                        "Estimate excludes startup countdown and Windows input overhead.")));
            }
        }
        const int quickMode = quickAction_ ? quickAction_->currentIndex() : 0;
        if (quickAction_) quickAction_->setEnabled(editable && !sequenced);
        if (quickMoveGroup_) quickMoveGroup_->setVisible(!sequenced && (quickMode == 1 || quickMode == 2));
        if (quickMoveReadiness_) quickMoveReadiness_->setText(uiText(destinationConfigured_
            ? QStringLiteral("Destination ready. The selected action will move the pointer here.")
            : QStringLiteral("Destination not set: capture a point or enter coordinates.")));
        if (quickForm_) {
            quickForm_->setRowVisible(quickHoldDuration_, quickMode == 0);
            quickForm_->setRowVisible(interval_, quickMode != 3);
            quickForm_->setRowVisible(count_, quickMode != 3);
            quickForm_->setRowVisible(infinite_, quickMode != 3);
            quickForm_->setRowVisible(button_, quickMode != 1);
            quickForm_->setRowVisible(quickChooseButton_, quickMode != 1);
            quickForm_->setRowVisible(customizeClicks_, quickMode == 0);
        }
        const bool atClock = startMode_ && startMode_->currentIndex() == 1;
        if (startMode_) startMode_->setEnabled(editable && !sequenced);
        if (startDelayValue_) startDelayValue_->setEnabled(editable && !sequenced && !atClock);
        if (startDelayUnit_) startDelayUnit_->setEnabled(editable && !sequenced && !atClock);
        if (startAtTime_) startAtTime_->setEnabled(editable && !sequenced && atClock);
        if (quickForm_) {
            if (startDelayLabel_) quickForm_->setRowVisible(startDelayLabel_, !atClock && !sequenced);
            if (startAtTime_) quickForm_->setRowVisible(startAtTime_, atClock && !sequenced);
            if (startMode_) quickForm_->setRowVisible(startMode_, !sequenced);
        }
        if (keepAwakeQuick_) {
            keepAwakeQuick_->setEnabled(editable && !sequenced);
            keepAwakeQuick_->setText(language_ == QStringLiteral("ru")
                ? QStringLiteral("Не давать Windows и экрану засыпать")
                : QStringLiteral("Keep Windows and display awake while running"));
            quickForm_->setRowVisible(keepAwakeQuick_, !sequenced);
        }
        if (quickMoveX_) quickMoveX_->setEnabled(editable && !sequenced);
        if (quickMoveY_) quickMoveY_->setEnabled(editable && !sequenced);
        if (quickMoveDuration_) quickMoveDuration_->setEnabled(editable && !sequenced);
        if (quickCaptureDestination_) quickCaptureDestination_->setEnabled(editable && !sequenced);
        if (count_) count_->setEnabled(editable && !sequenced && quickMode != 3 && !infinite_->isChecked());
        if (infinite_) infinite_->setEnabled(editable && !sequenced && quickMode != 3);
        if (customizeClicks_) customizeClicks_->setEnabled(editable && !sequenced && quickMode == 0);
        quickPosition_->setEnabled(editable && !sequenced);
        button_->setEnabled(editable && !sequenced && quickMode != 1);
        if (quickHoldDuration_) quickHoldDuration_->setEnabled(editable && !sequenced && quickMode == 0);
        interval_->setEnabled(editable && !sequenced);
        positionMode_->setEnabled(editable && !sequenced);
        infinite_->setText(uiText(quickMode == 0 ? QStringLiteral("Unlimited clicks")
                                    : QStringLiteral("Unlimited actions")));
        if (countLabel_) countLabel_->setText(uiText(quickMode == 0 ? QStringLiteral("Click count")
                            : QStringLiteral("Action repetitions")));
        quickWindow_->setEnabled(editable);
        editPosition_->setEnabled(editable && !sequenced);
        quickSequence_->setEnabled(editable);
        editSequence_->setEnabled(editable);
        editWindow_->setEnabled(editable);
        // Only main workflows appear in the sidebar. Show sequence activity
        // without turning configuration options into additional navigation pages.
        if (auto* sequenceItem = navigation_->item(1)) {
            const QString name = uiText(QStringLiteral("Sequence"));
            sequenceItem->setText(name);
            sequenceItem->setToolTip(uiText(QStringLiteral("Open to edit and run a sequence")));
        }
        const QString position = fixed
            ? (bound ? uiText(QStringLiteral("Fixed point in target window: (%1, %2)"))
                         : uiText(QStringLiteral("Fixed point on screen: (%1, %2)")))
                    .arg(x_->value()).arg(y_->value())
            : uiText(QStringLiteral("Current cursor position"));
        const QString target = bound
            ? (selectedTarget_
                ? uiText(QStringLiteral("Selected window: %1")).arg(QString::fromStdWString(selectedTarget_->title))
                : uiText(QStringLiteral("Selected window: NOT SELECTED")))
            : uiText(QStringLiteral("Any foreground window (global mouse)"));
        if (sequenced) {
            activeSetup_->setText(uiText(QStringLiteral("Click sequence: %1 steps · %2 passes\n%3"))
                .arg(sequenceEditor_ ? static_cast<int>(sequenceEditor_->steps().size()) : 0)
                .arg(sequenceUnlimited_->isChecked() ? uiText(QStringLiteral("Unlimited"))
                                                       : QString::number(sequenceRepeatCount_->value()))
                .arg(target));
        } else activeSetup_->setText(uiText(QStringLiteral("%1 · Every %2 ms · %3\n%4\n%5"))
            .arg(quickMode == 0 ? button_->currentText() : quickAction_->currentText())
            .arg(interval_->value())
            .arg(quickMode == 3 ? uiText(QStringLiteral("Until Stop")) :
                 infinite_->isChecked() ? uiText(QStringLiteral("Unlimited")) :
                 quickMode == 0 ? uiText(QStringLiteral("%1 clicks")).arg(count_->value()) :
                                  uiText(QStringLiteral("%1 actions")).arg(count_->value()))
            .arg(target)
            .arg(position));
        setupHelp_->setText(bound && !selectedTarget_
            ? uiText(QStringLiteral("Choose a target window before pressing Start."))
            : bound
                ? uiText(QStringLiteral("Foreground only: keep the selected window focused. "
                                 "With Current cursor, the cursor must be inside its client area."))
                : uiText(QStringLiteral("No window restriction. Input uses the global cursor and may affect other applications.")));
    }

    void updatePositionInputs() {
        const bool enabled = positionMode_->currentIndex() == 1 && !active_ && !capturePending_ &&
                             !(quickSequence_ && quickSequence_->isChecked());
        x_->setEnabled(enabled);
        y_->setEnabled(enabled);
#ifdef _WIN32
        capture_->setEnabled(enabled && !capturePending_);
#else
        capture_->setEnabled(false);
#endif
        updateQuickOptions();
    }
    void minimizeForPointSelection() {
        if (!hiddenForCapture_) {
            wasMaximizedBeforeCapture_ = isMaximized();
            hiddenForCapture_ = true;
            hide();
        }
    }
    void restoreAfterPointSelection() {
        if (hiddenForCapture_) {
            hiddenForCapture_ = false;
            if (wasMaximizedBeforeCapture_) showMaximized();
            else showNormal();
        }
    }
    void showCaptureOverlayMessage(const QString& text) {
        if (!captureOverlay_) {
            // A detached tool window stays visible while the main window is hidden.
            captureOverlay_ = new QLabel(nullptr);
            captureOverlay_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput);
            captureOverlay_->setAttribute(Qt::WA_ShowWithoutActivating);
            captureOverlay_->setAttribute(Qt::WA_TransparentForMouseEvents);
            captureOverlay_->setWordWrap(true);
            captureOverlay_->setMaximumWidth(590);
            captureOverlay_->setStyleSheet(QStringLiteral(
                "QLabel { background: #162038; color: #ffffff; padding: 16px; "
                "font: bold 16pt 'Segoe UI'; border: 2px solid #78a4ff; border-radius: 10px; }"));
        }
        captureOverlay_->setText(text);
        captureOverlay_->adjustSize();
        if (auto* screen = QGuiApplication::primaryScreen()) {
            const QRect area = screen->availableGeometry();
            captureOverlay_->move(area.right() - captureOverlay_->width() - 28, area.top() + 28);
        }
        captureOverlay_->show();
    }
    void showCaptureCountdown() {
        const QString message = (captureWindowPending_ || captureStepWindowPending_
            ? uiText(QStringLiteral("Selecting window in %1 s · hover over its content"))
            : endpointCapture_ ? uiText(QStringLiteral("Capturing destination in %1 s · move cursor to end point"))
            : uiText(QStringLiteral("Capturing point in %1 s · move cursor to target")))
                                   .arg(captureRemaining_);
        QString shortcut;
#ifdef _WIN32
        if (hotkeys_ && hotkeys_->pickerReady())
            shortcut = QStringLiteral(" · %1: %2").arg(
                pickShortcut_->keySequence().toString(QKeySequence::NativeText),
                uiText(QStringLiteral("Capture now")));
#endif
        showCaptureOverlayMessage(message + shortcut);
        setStatus(message);
    }
    void hideCaptureCountdown() { if (captureOverlay_) captureOverlay_->hide(); }
    void startCaptureCountdown(int seconds, bool sequence) {
        sequenceTimedCapture_ = sequence;
        captureRemaining_ = seconds;
        showCaptureCountdown();
        captureCountdownTimer_.start();
    }
    void captureCursor() {
#ifdef _WIN32
        if (active_ || capturePending_ ||
            (!endpointCapture_ && positionMode_->currentIndex() != 1)) return;
        capturePending_ = true;
        ++captureToken_;
        updateTargetInputs();
        updatePositionInputs();
        setShortcutEditorsEnabled(false);
        positionMode_->setEnabled(false);
        start_->setEnabled(false);
        profiles_->setEnabled(false);
        saveProfile_->setEnabled(false);
        updateProfileActions();
        if (hotkeys_) hotkeys_->setPicking(true);
        const QString shortcut = hotkeys_ && hotkeys_->pickerReady()
            ? QStringLiteral(" or press %1").arg(pickShortcut_->keySequence().toString(QKeySequence::NativeText))
            : QString();
        Q_UNUSED(shortcut);
        startCaptureCountdown(positionCaptureSeconds_->value(), false);
        minimizeForPointSelection();
#endif
    }
    void finishCursorCapture() {
#ifdef _WIN32
        if (!capturePending_ ||
            (!endpointCapture_ && positionMode_->currentIndex() != 1)) return;
        captureCountdownTimer_.stop();
        hideCaptureCountdown();
        POINT point{};
        if (GetCursorPos(&point)) {
            if (targetMode_->currentIndex() == 1) {
                if (!selectedTarget_ || clicknova::platform::checkCapturePoint(
                         *selectedTarget_, point.x, point.y) != clicknova::platform::TargetCheck::Ready) {
                    setStatus(QStringLiteral("Place the cursor inside the focused target window."));
                } else {
                    HWND hwnd = reinterpret_cast<HWND>(selectedTarget_->handle);
                    POINT relative = point;
                    if (ScreenToClient(hwnd, &relative)) {
                        if (endpointCapture_) {
                            quickMoveX_->setValue(relative.x);
                            quickMoveY_->setValue(relative.y);
                            destinationConfigured_ = true;
                        } else {
                            x_->setValue(relative.x);
                            y_->setValue(relative.y);
                        }
                        setStatus(QStringLiteral("Captured client position: %1, %2")
                                             .arg(relative.x).arg(relative.y));
                        saveLastSettings();
                    }
                }
            } else {
                if (endpointCapture_) {
                    quickMoveX_->setValue(point.x);
                    quickMoveY_->setValue(point.y);
                    destinationConfigured_ = true;
                } else {
                    x_->setValue(point.x);
                    y_->setValue(point.y);
                }
                setStatus(QStringLiteral("Captured screen position: %1, %2")
                                     .arg(point.x).arg(point.y));
                saveLastSettings();
            }
        } else {
            setStatus(QStringLiteral("Unable to read the cursor position."));
        }
        capturePending_ = false;
        endpointCapture_ = false;
        ++captureToken_;
        restoreAfterPointSelection();
        if (hotkeys_) hotkeys_->setPicking(false);
        positionMode_->setEnabled(true);
        start_->setEnabled(true);
        profiles_->setEnabled(true);
        saveProfile_->setEnabled(true);
        setShortcutEditorsEnabled(true);
        updatePositionInputs();
        updateProfileActions();
        updateTargetInputs();
#endif
    }
    void updateTargetInputs() {
        if (!targetMode_ || !targetWindows_ || !targetHelp_) return;
        const bool bound = targetMode_->currentIndex() == 1;
        const bool canEdit = !active_ && !capturePending_;
        targetMode_->setEnabled(canEdit);
        targetWindows_->setEnabled(bound && canEdit);
        refreshWindows_->setEnabled(bound && canEdit);
#ifdef _WIN32
        captureWindow_->setEnabled(bound && canEdit);
#else
        captureWindow_->setEnabled(false);
#endif
        positionMode_->setItemText(1, uiText(bound ? QStringLiteral("Fixed client coordinates")
                                                : QStringLiteral("Fixed screen coordinates")));
        if (bound) {
            targetHelp_->setText(selectedTarget_
                ? uiText(QStringLiteral("Bound to: %1 (PID %2). Focus must remain on this window during clicks."))
                    .arg(QString::fromStdWString(selectedTarget_->title))
                    .arg(selectedTarget_->processId)
                : (savedWindowTitle_.isEmpty()
                   ? uiText(QStringLiteral("Select a window. Background clicks are not implemented; focus is required."))
                   : uiText(QStringLiteral("Saved window: %1. Please select it again before starting.")).arg(savedWindowTitle_)));
            caution_->setText(uiText(QStringLiteral("BOUND WINDOW: Uses the global mouse. Automation stops if the target loses focus or becomes covered.")));
        } else {
            targetHelp_->setText(uiText(QStringLiteral("Global mode does not bind to a window.")));
            caution_->setText(uiText(QStringLiteral("GLOBAL MOUSE: Clicking affects the desktop; background automation is not available in this mode.")));
        }
        updateQuickOptions();
    }
    void refreshWindowList() {
        if (active_ || capturePending_) return;
        const auto previous = selectedTarget_;
        availableWindows_ = clicknova::platform::listWindows();
        selectedTarget_.reset();
        const QSignalBlocker block(targetWindows_);
        targetWindows_->clear();
        int previousIndex = -1;
        for (int index = 0; index < static_cast<int>(availableWindows_.size()); ++index) {
            const auto& window = availableWindows_[static_cast<std::size_t>(index)];
            QString title = QString::fromStdWString(window.title).simplified();
            if (title.size() > 100) title = title.left(100) + QStringLiteral("...");
            targetWindows_->addItem(QStringLiteral("%1 (PID %2)")
                                    .arg(title).arg(window.processId));
            if (previous && window.handle == previous->handle && window.processId == previous->processId)
                previousIndex = index;
        }
        targetWindows_->setCurrentIndex(previousIndex);
        if (previousIndex >= 0) selectedTarget_ = availableWindows_[static_cast<std::size_t>(previousIndex)];
        updateTargetInputs();
    }
    void chooseWindowFromList(bool step) {
#ifdef _WIN32
        if (active_ || capturePending_) return;
        if (step && (!sequenceEditor_ || !sequenceEditor_->hasSelectedAction())) return;
        const auto chosen = chooseTargetWindow(this, language_);
        if (!chosen) return;
        if (step) {
            if (sequenceEditor_->assignPickedWindow(*chosen)) {
                setStatus(QStringLiteral("Window selected for action. Capture its click point again before running."));
            }
            return;
        }
        if (targetMode_->currentIndex() != 1) targetMode_->setCurrentIndex(1);
        refreshWindowList();
        for (int i = 0; i < static_cast<int>(availableWindows_.size()); ++i) {
            const auto& target = availableWindows_[static_cast<std::size_t>(i)];
            if (target.handle == chosen->handle && target.processId == chosen->processId) {
                targetWindows_->setCurrentIndex(i);
                setStatus(QStringLiteral("Window selected. Clicks require the window to be in the foreground."));
                return;
            }
        }
        // A window may close between the chooser and list refresh. Never bind by title alone.
        setStatus(QStringLiteral("Selected window is no longer available. Refresh and choose again."));
#else
        Q_UNUSED(step);
#endif
    }
    void captureActiveWindow() {
        beginWindowHoverCapture(false);
    }
    void beginWindowHoverCapture(bool step) {
#ifdef _WIN32
        if (active_ || capturePending_) return;
        if (step) {
            if (!sequenceEditor_ || !sequenceEditor_->hasSelectedAction()) return;
        } else if (targetMode_->currentIndex() != 1) return;
        capturePending_ = true;
        captureWindowPending_ = !step;
        captureStepWindowPending_ = step;
        setShortcutEditorsEnabled(false);
        if (step) sequenceEditor_->setLocked(true);
        updateTargetInputs();
        updatePositionInputs();
        positionMode_->setEnabled(false);
        targetMode_->setEnabled(false);
        start_->setEnabled(false);
        saveProfile_->setEnabled(false);
        updateProfileActions();
        if (hotkeys_) hotkeys_->setPicking(true);
        startCaptureCountdown(step ? sequenceEditor_->captureDelaySeconds()
                                   : windowCaptureSeconds_->value(), false);
        minimizeForPointSelection();
#endif
    }
#ifdef _WIN32
    void finishWindowHoverCapture() {
        if (!capturePending_) return;
        captureCountdownTimer_.stop();
        hideCaptureCountdown();
        const bool step = captureStepWindowPending_;
        auto picked = clicknova::platform::windowUnderCursor();
        captureWindowPending_ = false;
        captureStepWindowPending_ = false;
        capturePending_ = false;
        if (hotkeys_) hotkeys_->setPicking(false);
        restoreAfterPointSelection();
        setShortcutEditorsEnabled(true);
        positionMode_->setEnabled(true);
        targetMode_->setEnabled(true);
        start_->setEnabled(true);
        saveProfile_->setEnabled(true);
        if (step) sequenceEditor_->setLocked(false);
        if (!picked) {
            setStatus(QStringLiteral("No valid window under the cursor. Move the pointer over an application and try again."));
        } else if (step) {
            if (sequenceEditor_->assignPickedWindow(*picked)) {
                setStatus(QStringLiteral("Window selected for action. Capture its click point again before running."));
            } else setStatus(QStringLiteral("Select an action before choosing its window."));
        } else {
            refreshWindowList();
            const auto it = std::find_if(availableWindows_.begin(), availableWindows_.end(),
                [&picked](const auto& item) {
                    return item.handle == picked->handle && item.processId == picked->processId;
                });
            if (it == availableWindows_.end()) {
                availableWindows_.push_back(*picked);
                targetWindows_->addItem(QString::fromStdWString(picked->title));
                targetWindows_->setCurrentIndex(targetWindows_->count() - 1);
            } else targetWindows_->setCurrentIndex(static_cast<int>(std::distance(availableWindows_.begin(), it)));
            setStatus(QStringLiteral("Window selected. Clicks require the window to be in the foreground."));
        }
        updatePositionInputs();
        updateProfileActions();
        updateTargetInputs();
    }
#endif
    // Each action may override the global window. Focus remains mandatory:
    // this is never a background injection mechanism.
    std::optional<clicknova::platform::WindowTarget> captureSequenceWindow() const {
        if (sequenceEditor_->wantsOwnWindow()) return sequenceEditor_->pickedStepWindow();
        if (targetMode_->currentIndex() == 1) return selectedTarget_;
        return std::nullopt;
    }
#ifdef _WIN32
    void finishSequenceCapture(POINT pt) {
        const bool needsWindow = sequenceEditor_->wantsOwnWindow() || targetMode_->currentIndex() == 1;
        auto target = captureSequenceWindow();
        if (needsWindow && !target) {
            setStatus(QStringLiteral("Select the action's target window before picking a point."));
            return;
        }
        const auto space = needsWindow ? clicknova::sequence::Space::Client
                                       : clicknova::sequence::Space::Screen;
        if (target) {
            const auto status = clicknova::platform::checkCapturePoint(*target, pt.x, pt.y);
            if (status != clicknova::platform::TargetCheck::Ready) {
                setStatus(QString::fromUtf8(clicknova::platform::targetCheckMessage(status)));
                return;
            }
            HWND hwnd = reinterpret_cast<HWND>(target->handle);
            if (!ScreenToClient(hwnd, &pt)) {
                setStatus(QStringLiteral("Unable to resolve point inside the selected window."));
                return;
            }
        }
        sequenceEditor_->addCapturedPoint(pt.x, pt.y, space);
        setStatus(QStringLiteral("Point selected for the sequence action."));
    }
#endif
    void endSequencePicking() {
        captureCountdownTimer_.stop();
        hideCaptureCountdown();
        sequenceTimedCapture_ = false;
        sequenceHoverPicking_ = false;
#ifdef _WIN32
        mousePicker_.cancel();
        if (hotkeys_) hotkeys_->setPicking(false);
#endif
        capturePending_ = false;
        ++captureToken_;
        restoreAfterPointSelection();
        sequenceEditor_->setLocked(false);
        start_->setEnabled(true);
    }
    void beginSequenceHoverPicker() {
#ifdef _WIN32
        if (active_ || capturePending_ || !sequenceEditor_ || !hotkeys_) return;
        if ((sequenceEditor_->wantsOwnWindow() || targetMode_->currentIndex() == 1)
            && !captureSequenceWindow()) {
            setStatus(QStringLiteral("Select a valid target window before capturing a point."));
            return;
        }
        capturePending_ = true;
        sequenceHoverPicking_ = true;
        sequenceEditor_->setLocked(true);
        start_->setEnabled(false);
        hotkeys_->setPicking(true);
        if (!hotkeys_->pickerReady()) {
            endSequencePicking();
            setStatus(QStringLiteral("Enable a free point-picking shortcut in Shortcuts, or use timed capture."));
            return;
        }
        const int token = ++captureToken_;
        const QString shortcutLabel = pickShortcut_->keySequence().toString(QKeySequence::NativeText);
        setStatus(QStringLiteral("Move to the target window with Alt+Tab, hover the desired point, then press %1. No mouse click needed.")
                  .arg(shortcutLabel));
        minimizeForPointSelection();
        showCaptureOverlayMessage(uiText(QStringLiteral(
            "ClickNova is hidden. Hover over the click point and press %1 to capture it."))
            .arg(shortcutLabel));
        QTimer::singleShot(60000, this, [this, token] {
            if (capturePending_ && sequenceHoverPicking_ && token == captureToken_) {
                endSequencePicking();
                setStatus(QStringLiteral("Point picking timed out; no changes were made."));
            }
        });
#endif
    }
    void finishSequenceHoverPicking() {
#ifdef _WIN32
        if (!sequenceHoverPicking_ || !capturePending_) return;
        POINT point{};
        const bool ok = GetCursorPos(&point);
        endSequencePicking();
        if (ok) finishSequenceCapture(point);
        else setStatus(QStringLiteral("Unable to read cursor position."));
#endif
    }
    void beginClickPointPicker() {
#ifdef _WIN32
        if (active_ || capturePending_ || !sequenceEditor_) return;
        if ((sequenceEditor_->wantsOwnWindow() || targetMode_->currentIndex() == 1)
            && !captureSequenceWindow()) {
            setStatus(QStringLiteral("Select a valid target window before capturing a point."));
            return;
        }
        capturePending_ = true;
        sequenceEditor_->setLocked(true);
        start_->setEnabled(false);
        const int token = ++captureToken_;
        const bool started = mousePicker_.begin([this, token](POINT pt) {
            if (!capturePending_ || token != captureToken_) return;
            endSequencePicking();
            finishSequenceCapture(pt);
        }, [this, token] {
            if (!capturePending_ || token != captureToken_) return;
            endSequencePicking();
            setStatus(QStringLiteral("Point picking cancelled."));
        });
        if (!started) {
            endSequencePicking();
            setStatus(QStringLiteral("Windows could not start click-to-pick mode."));
            return;
        }
        setStatus(QStringLiteral("Left-click the desired position to capture it without sending a click. Esc cancels."));
        minimizeForPointSelection();
        showCaptureOverlayMessage(uiText(QStringLiteral(
            "ClickNova is hidden. Left-click the destination to capture it. Esc cancels.")));
        QTimer::singleShot(30000, this, [this, token] {
            if (capturePending_ && token == captureToken_) {
                endSequencePicking();
                setStatus(QStringLiteral("Point picking timed out; no changes were made."));
            }
        });
#endif
    }
    void finishSequenceTimedCapture() {
#ifdef _WIN32
        if (!capturePending_ || !sequenceTimedCapture_) return;
        POINT point{};
        const bool ok = GetCursorPos(&point);
        endSequencePicking();
        if (ok) finishSequenceCapture(point);
        else setStatus(QStringLiteral("Unable to read cursor position."));
#endif
    }
    void captureSequencePoint() {
#ifdef _WIN32
        if (active_ || capturePending_ || !sequenceEditor_) return;
        if ((sequenceEditor_->wantsOwnWindow() || targetMode_->currentIndex() == 1)
            && !captureSequenceWindow()) {
            setStatus(QStringLiteral("Select a valid target window before capturing a point."));
            return;
        }
        capturePending_ = true;
        sequenceEditor_->setLocked(true);
        start_->setEnabled(false);
        ++captureToken_;
        if (hotkeys_) hotkeys_->setPicking(true);
        startCaptureCountdown(sequenceEditor_->captureDelaySeconds(), true);
        minimizeForPointSelection();
#endif
    }

    using ScenarioKind = clicknova::storage::Kind;
    QComboBox* scenarioPicker(ScenarioKind kind) const {
        return kind == ScenarioKind::AutoClicker ? clickScenarios_ : sequenceScenarios_;
    }
    void updateScenarioButtons() {
        if (!clickScenarios_ || !sequenceScenarios_) return;
        const bool ready = !active_ && !capturePending_;
        saveClickScenario_->setEnabled(ready);
        saveSequenceScenario_->setEnabled(ready && !sequenceEditor_->hasUnsavedActionChanges());
        loadClickScenario_->setEnabled(ready && clickScenarios_->currentIndex() >= 0);
        deleteClickScenario_->setEnabled(ready && clickScenarios_->currentIndex() >= 0);
        loadSequenceScenario_->setEnabled(ready && sequenceScenarios_->currentIndex() >= 0);
        deleteSequenceScenario_->setEnabled(ready && sequenceScenarios_->currentIndex() >= 0);
    }
    void refreshScenarios(ScenarioKind kind, const QString& preferred = {}) {
        auto* picker = scenarioPicker(kind);
        if (!picker) return;
        const QSignalBlocker blocker(picker);
        const QString selection = preferred.isEmpty() ? picker->currentText() : preferred;
        picker->clear();
        picker->addItems(scenarioStore_.names(kind));
        picker->setCurrentIndex(picker->findText(selection));
        updateScenarioButtons();
    }
    void refreshScenarios() {
        refreshScenarios(ScenarioKind::AutoClicker);
        refreshScenarios(ScenarioKind::Sequence);
    }
    QVariantMap scenarioValues(ScenarioKind kind) const {
        if (kind == ScenarioKind::AutoClicker) {
            clicknova::storage::AutoClickerScenario scenario;
            scenario.button = button_->currentIndex();
            scenario.intervalMs = interval_->value();
            scenario.holdDurationMs = quickHoldDuration_->value();
            scenario.actionMode = quickAction_->currentIndex();
            scenario.destinationX = quickMoveX_->value();
            scenario.destinationY = quickMoveY_->value();
            scenario.movementMs = quickMoveDuration_->value();
            scenario.destinationConfigured = destinationConfigured_;
            scenario.unlimited = infinite_->isChecked();
            scenario.count = count_->value();
            scenario.startDelayValue = startDelayValue_->value();
            scenario.startDelayUnit = startDelayUnit_->currentIndex();
            scenario.startMode = startMode_->currentIndex();
            scenario.startTimeSeconds = QTime(0, 0).secsTo(startAtTime_->time());
            scenario.fixed = quickPosition_->isChecked();
            scenario.bound = quickWindow_->isChecked();
            scenario.screen = scenario.bound ? screenCoordinates_ : QPoint(x_->value(), y_->value());
            scenario.client = scenario.bound ? QPoint(x_->value(), y_->value()) : clientCoordinates_;
            scenario.windowTitle = selectedTarget_ ? QString::fromStdWString(selectedTarget_->title)
                                                    : savedWindowTitle_;
            return clicknova::storage::encodeScenario(scenario);
        }
        clicknova::storage::SequenceScenario scenario;
        scenario.repeatCount = sequenceRepeatCount_->value();
        scenario.unlimited = sequenceUnlimited_->isChecked();
        scenario.passDelayMs = sequenceEditor_->passDelayMs();
        scenario.steps = sequenceEditor_->steps();
        return clicknova::storage::encodeScenario(scenario);
    }
    bool applyScenario(ScenarioKind kind, const QVariantMap& values, QString* error = nullptr) {
        // Decode and validate the complete scenario before touching any UI state.
        // Invalid JSON must not partially overwrite a user's current configuration.
        if (kind == ScenarioKind::AutoClicker) {
            clicknova::storage::AutoClickerScenario scenario;
            if (!clicknova::storage::decodeScenario(values, scenario, error)) return false;
            restoringSettings_ = true;
            button_->setCurrentIndex(scenario.button);
            interval_->setValue(scenario.intervalMs);
            quickHoldDuration_->setValue(scenario.holdDurationMs);
            quickAction_->setCurrentIndex(scenario.actionMode);
            quickMoveX_->setValue(scenario.destinationX);
            quickMoveY_->setValue(scenario.destinationY);
            quickMoveDuration_->setValue(scenario.movementMs);
            infinite_->setChecked(scenario.unlimited);
            count_->setValue(scenario.count);
            startDelayValue_->setValue(scenario.startDelayValue);
            startDelayUnit_->setCurrentIndex(scenario.startDelayUnit);
            startMode_->setCurrentIndex(scenario.startMode);
            startAtTime_->setTime(QTime(0, 0).addSecs(scenario.startTimeSeconds));
            screenCoordinates_ = scenario.screen;
            clientCoordinates_ = scenario.client;
            targetMode_->setCurrentIndex(scenario.bound ? 1 : 0);
            positionMode_->setCurrentIndex(scenario.fixed ? 1 : 0);
            selectedTarget_.reset();
            savedWindowTitle_ = scenario.windowTitle;
            targetWindows_->setCurrentIndex(-1);
            destinationConfigured_ = scenario.destinationConfigured;
            const QPoint position = scenario.bound ? scenario.client : scenario.screen;
            x_->setValue(position.x());
            y_->setValue(position.y());
            quickSequence_->setChecked(false);
            restoringSettings_ = false;
            updateTargetInputs(); updatePositionInputs(); updateQuickOptions();
            navigation_->setCurrentRow(0);
        } else {
            clicknova::storage::SequenceScenario scenario;
            if (!clicknova::storage::decodeScenario(values, scenario, error)) return false;
            restoringSettings_ = true;
            sequenceEditor_->setSteps(scenario.steps);
            sequenceEditor_->setPassDelayMs(scenario.passDelayMs);
            sequenceRepeatCount_->setValue(scenario.repeatCount);
            sequenceUnlimited_->setChecked(scenario.unlimited);
            quickSequence_->setChecked(true);
            restoringSettings_ = false;
            updateQuickOptions();
            navigation_->setCurrentRow(1);
        }
        saveLastSettings();
        return true;
    }
    void saveScenario(ScenarioKind kind) {
        if (active_ || capturePending_) return;
        if (kind == ScenarioKind::Sequence && sequenceEditor_->hasUnsavedActionChanges()) {
            setStatus(QStringLiteral("Apply changes to the selected action before starting.")); return;
        }
        bool accepted = false;
        const QString name = QInputDialog::getText(this, uiText(QStringLiteral("Save scenario")),
            uiText(QStringLiteral("Scenario name:")), QLineEdit::Normal,
            scenarioPicker(kind)->currentText(), &accepted).trimmed();
        if (!accepted) return;
        if (!clicknova::storage::ScenarioStore::validName(name)) {
            QMessageBox::warning(this, uiText(QStringLiteral("Invalid name")),
                uiText(QStringLiteral("Use 1-40 letters, numbers, spaces, hyphens, or underscores. Start with a letter or number.")));
            return;
        }
        if (scenarioStore_.names(kind).contains(name) &&
            QMessageBox::question(this, uiText(QStringLiteral("Overwrite scenario")),
                uiText(QStringLiteral("Replace saved scenario '%1'?")).arg(name)) != QMessageBox::Yes) return;
        QString error;
        if (!scenarioStore_.save(kind, name, scenarioValues(kind), &error)) {
            QMessageBox::warning(this, uiText(QStringLiteral("Save scenario")), error); return;
        }
        refreshScenarios(kind, name);
        setStatus(QStringLiteral("Scenario saved: %1").arg(name));
    }
    void loadScenario(ScenarioKind kind) {
        if (active_ || capturePending_ || sequenceEditor_->hasUnsavedActionChanges()) return;
        const QString name = scenarioPicker(kind)->currentText();
        QString error;
        const auto scenarioData = scenarioStore_.load(kind, name, &error);
        if (!scenarioData || !applyScenario(kind, *scenarioData, &error)) {
            QMessageBox::warning(this, uiText(QStringLiteral("Load scenario")),
                error.isEmpty() ? uiText(QStringLiteral("Scenario contains invalid settings.")) : error);
            return;
        }
        setStatus(QStringLiteral("Scenario loaded: %1").arg(name));
    }
    void deleteScenario(ScenarioKind kind) {
        if (active_ || capturePending_) return;
        const QString name = scenarioPicker(kind)->currentText();
        if (name.isEmpty()) return;
        if (QMessageBox::question(this, uiText(QStringLiteral("Delete scenario")),
            uiText(QStringLiteral("Delete saved scenario '%1'?")).arg(name)) != QMessageBox::Yes) return;
        if (!scenarioStore_.remove(kind, name)) return;
        refreshScenarios(kind);
        setStatus(QStringLiteral("Scenario deleted: %1").arg(name));
    }

    static void saveFields(QSettings& settings, const MainWindow& window) {
        settings.setValue(QStringLiteral("button"), window.button_->currentIndex());
        settings.setValue(QStringLiteral("interval"), window.interval_->value());
        settings.setValue(QStringLiteral("quickHoldDurationMs"), window.quickHoldDuration_->value());
        settings.setValue(QStringLiteral("quickActionMode"), window.quickAction_->currentIndex());
        settings.setValue(QStringLiteral("quickMoveX"), window.quickMoveX_->value());
        settings.setValue(QStringLiteral("quickMoveY"), window.quickMoveY_->value());
        settings.setValue(QStringLiteral("quickMoveDuration"), window.quickMoveDuration_->value());
        settings.setValue(QStringLiteral("quickDestinationConfigured"), window.destinationConfigured_);
        settings.setValue(QStringLiteral("unlimited"), window.infinite_->isChecked());
        settings.setValue(QStringLiteral("count"), window.count_->value());
        settings.setValue(QStringLiteral("startDelayValue"), window.startDelayValue_->value());
        settings.setValue(QStringLiteral("startDelayUnit"), window.startDelayUnit_->currentIndex());
        settings.setValue(QStringLiteral("startMode"), window.startMode_->currentIndex());
        settings.setValue(QStringLiteral("startTimeSeconds"),
                          QTime(0, 0).secsTo(window.startAtTime_->time()));
        settings.setValue(QStringLiteral("position"), window.positionMode_->currentIndex());
        settings.setValue(QStringLiteral("targetMode"), window.targetMode_->currentIndex());
        settings.setValue(QStringLiteral("x"), window.x_->value());
        settings.setValue(QStringLiteral("y"), window.y_->value());
        const bool bound = window.targetMode_->currentIndex() == 1;
        const QPoint screen = bound ? window.screenCoordinates_
                                    : QPoint(window.x_->value(), window.y_->value());
        const QPoint client = bound ? QPoint(window.x_->value(), window.y_->value())
                                    : window.clientCoordinates_;
        settings.setValue(QStringLiteral("screenX"), screen.x());
        settings.setValue(QStringLiteral("screenY"), screen.y());
        settings.setValue(QStringLiteral("clientX"), client.x());
        settings.setValue(QStringLiteral("clientY"), client.y());
        settings.setValue(QStringLiteral("language"), window.language_);
        settings.setValue(QStringLiteral("targetWindowTitle"),
                          window.selectedTarget_
                            ? QString::fromStdWString(window.selectedTarget_->title)
                            : window.savedWindowTitle_);
        settings.setValue(QStringLiteral("sequenceEnabled"), window.quickSequence_->isChecked());
        settings.setValue(QStringLiteral("sequenceRepeatCount"), window.sequenceRepeatCount_->value());
        settings.setValue(QStringLiteral("sequenceUnlimited"), window.sequenceUnlimited_->isChecked());
        settings.setValue(QStringLiteral("sequencePassDelayMs"), window.sequenceEditor_->passDelayMs());
        settings.setValue(QStringLiteral("captureDelaySeconds"), window.sequenceEditor_->captureDelaySeconds());
        QVariantList storedSteps;
        for (const auto& step : window.sequenceEditor_->steps()) {
            QVariantMap map;
            map.insert(QStringLiteral("x"), step.x);
            map.insert(QStringLiteral("y"), step.y);
            map.insert(QStringLiteral("button"), step.button);
            map.insert(QStringLiteral("delayMs"), step.delayMs);
            map.insert(QStringLiteral("intervalMs"), step.intervalMs);
            map.insert(QStringLiteral("space"), static_cast<int>(step.space));
            map.insert(QStringLiteral("clickCount"), step.clickCount);
            map.insert(QStringLiteral("configured"), step.configured);
            map.insert(QStringLiteral("ownWindow"), step.ownWindow);
            map.insert(QStringLiteral("windowTitle"), QString::fromUtf8(step.windowTitle.c_str()));
            // Window handles and process IDs must never be serialized.
            storedSteps.append(map);
        }
        settings.setValue(QStringLiteral("sequenceSteps"), storedSteps);
        const ShortcutOptions keys = window.shortcutValues();
        settings.setValue(QStringLiteral("shortcutEnableStart"), keys.enableStart);
        settings.setValue(QStringLiteral("shortcutEnablePick"), keys.enablePick);
        settings.setValue(QStringLiteral("shortcutStart"), keys.start.toString(QKeySequence::PortableText));
        settings.setValue(QStringLiteral("shortcutStop"), keys.stop.toString(QKeySequence::PortableText));
        settings.setValue(QStringLiteral("shortcutPick"), keys.pick.toString(QKeySequence::PortableText));
    }
    void restoreFields(QSettings& settings) {
        button_->setCurrentIndex(qBound(0, settings.value(QStringLiteral("button"), 0).toInt(), 4));
        interval_->setValue(settings.value(QStringLiteral("interval"), 200).toInt());
        quickHoldDuration_->setValue(qBound(0, settings.value(QStringLiteral("quickHoldDurationMs"), 0).toInt(), 60000));
        { const QSignalBlocker xBlock(quickMoveX_), yBlock(quickMoveY_);
          quickMoveX_->setValue(settings.value(QStringLiteral("quickMoveX"), 0).toInt());
          quickMoveY_->setValue(settings.value(QStringLiteral("quickMoveY"), 0).toInt()); }
        quickMoveDuration_->setValue(settings.value(QStringLiteral("quickMoveDuration"), 600).toInt());
        destinationConfigured_ = settings.value(QStringLiteral("quickDestinationConfigured"), false).toBool();
        quickAction_->setCurrentIndex(qBound(0, settings.value(QStringLiteral("quickActionMode"), 0).toInt(), 3));
        infinite_->setChecked(settings.value(QStringLiteral("unlimited"), false).toBool());
        count_->setValue(settings.value(QStringLiteral("count"), 100).toInt());
        // Existing profiles get their original three-second countdown.
        startDelayValue_->setValue(qBound(0, settings.value(QStringLiteral("startDelayValue"), 3).toInt(),
                                         std::numeric_limits<int>::max()));
        startDelayUnit_->setCurrentIndex(qBound(0, settings.value(QStringLiteral("startDelayUnit"), 0).toInt(), 3));
        startMode_->setCurrentIndex(qBound(0, settings.value(QStringLiteral("startMode"), 0).toInt(), 1));
        startAtTime_->setTime(QTime(0, 0).addSecs(qBound(0,
            settings.value(QStringLiteral("startTimeSeconds"), 18 * 3600 + 30 * 60).toInt(), 86399)));
        positionMode_->setCurrentIndex(qBound(0, settings.value(QStringLiteral("position"), 0).toInt(), 1));
        const int mode = qBound(0, settings.value(QStringLiteral("targetMode"), 0).toInt(), 1);
        const int legacyX = settings.value(QStringLiteral("x"), 0).toInt();
        const int legacyY = settings.value(QStringLiteral("y"), 0).toInt();
        screenCoordinates_ = QPoint(settings.value(QStringLiteral("screenX"), mode == 0 ? legacyX : 0).toInt(),
                                    settings.value(QStringLiteral("screenY"), mode == 0 ? legacyY : 0).toInt());
        clientCoordinates_ = QPoint(settings.value(QStringLiteral("clientX"), mode == 1 ? legacyX : 0).toInt(),
                                    settings.value(QStringLiteral("clientY"), mode == 1 ? legacyY : 0).toInt());
        lastTargetMode_ = mode;
        // HWND values cannot survive restart or profile loading.
        {
            const QSignalBlocker block(targetMode_);
            targetMode_->setCurrentIndex(mode);
        }
        selectedTarget_.reset();
        savedWindowTitle_ = settings.value(QStringLiteral("targetWindowTitle")).toString();
        targetWindows_->setCurrentIndex(-1);
        const QPoint point = mode == 1 ? clientCoordinates_ : screenCoordinates_;
        x_->setValue(point.x());
        y_->setValue(point.y());
        // Missing keys in older profiles disable the new feature safely.
        std::vector<clicknova::sequence::Step> restored;
        const auto records = settings.value(QStringLiteral("sequenceSteps")).toList();
        bool valid = records.size() <= 128;
        for (const QVariant& record : records) {
            const auto map = record.toMap();
            if (map.size() < 5) { valid = false; break; }
            clicknova::sequence::Step step{
                map.value(QStringLiteral("x")).toInt(),
                map.value(QStringLiteral("y")).toInt(),
                map.value(QStringLiteral("button")).toInt(),
                map.value(QStringLiteral("delayMs")).toInt(),
                static_cast<clicknova::sequence::Space>(map.value(QStringLiteral("space")).toInt())};
            step.clickCount = map.value(QStringLiteral("clickCount"), 1).toInt();
            // Older profiles applied delayMs before every click; preserve that timing.
            step.intervalMs = map.value(QStringLiteral("intervalMs"), step.delayMs).toInt();
            step.configured = map.value(QStringLiteral("configured"), true).toBool();
            step.ownWindow = map.value(QStringLiteral("ownWindow"), false).toBool();
            step.windowTitle = map.value(QStringLiteral("windowTitle")).toString().toUtf8().toStdString();
            restored.push_back(std::move(step));
        }
        if (valid && !restored.empty()) {
            auto draftCheck = restored;
            for (auto& step : draftCheck) step.configured = true;
            clicknova::sequence::Plan preview{std::move(draftCheck), 1, false};
            valid = clicknova::sequence::validate(preview).empty();
        }
        restoringSettings_ = true;
        sequenceEditor_->setSteps(valid ? restored : std::vector<clicknova::sequence::Step>{});
        sequenceEditor_->setPassDelayMs(settings.value(QStringLiteral("sequencePassDelayMs"), 0).toInt());
        sequenceEditor_->setCaptureDelaySeconds(settings.value(QStringLiteral("captureDelaySeconds"), 5).toInt());
        { const QSignalBlocker block(positionCaptureSeconds_);
          positionCaptureSeconds_->setValue(sequenceEditor_->captureDelaySeconds()); }
        sequenceRepeatCount_->setValue(settings.value(QStringLiteral("sequenceRepeatCount"),
                                                      settings.value(QStringLiteral("count"), 100)).toInt());
        sequenceUnlimited_->setChecked(settings.value(QStringLiteral("sequenceUnlimited"),
                                                     settings.value(QStringLiteral("unlimited"), true)).toBool());
        quickSequence_->setChecked(valid && !restored.empty() &&
                                   settings.value(QStringLiteral("sequenceEnabled"), false).toBool());
        restoringSettings_ = false;
        updateTargetInputs();
        updatePositionInputs();
    }
    void saveLastSettings() {
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.beginGroup(QStringLiteral("last"));
        saveFields(settings, *this);
        settings.endGroup();
    }
    void restoreLastSettings() {
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.beginGroup(QStringLiteral("last"));
        restoreFields(settings);
        settings.endGroup();
    }
    void refreshProfiles(const QString& preferred = QString()) {
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.beginGroup(QStringLiteral("profiles"));
        const auto names = settings.childGroups();
        settings.endGroup();
        const QSignalBlocker blocked(profiles_);
        profiles_->clear();
        profiles_->addItems(names);
        const int selection = profiles_->findText(preferred);
        profiles_->setCurrentIndex(selection >= 0 ? selection : -1);
        updateProfileActions();
    }
    void updateProfileActions() {
        const bool chosen = profiles_ && profiles_->currentIndex() >= 0 && !active_ && !capturePending_;
        if (loadProfile_) loadProfile_->setEnabled(chosen);
        if (deleteProfile_) deleteProfile_->setEnabled(chosen);
    }
    void saveNamedProfile() {
        if (active_ || capturePending_) return;
        bool accepted = false;
        const QString name = QInputDialog::getText(
            this, uiText(QStringLiteral("Save profile")), uiText(QStringLiteral("Profile name:")),
            QLineEdit::Normal, profiles_->currentText(), &accepted).trimmed();
        if (!accepted) return;
        static const QRegularExpression validName(QStringLiteral("^[\\p{L}\\p{N}][\\p{L}\\p{N} _-]{0,39}$"));
        if (!validName.match(name).hasMatch()) {
            QMessageBox::warning(this, uiText(QStringLiteral("Invalid name")),
                uiText(QStringLiteral("Use 1-40 letters, numbers, spaces, hyphens, or underscores. Start with a letter or number.")));
            return;
        }
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.beginGroup(QStringLiteral("profiles"));
        if (settings.childGroups().contains(name) &&
            QMessageBox::question(this, uiText(QStringLiteral("Overwrite profile")),
                                  uiText(QStringLiteral("Replace the existing profile '%1'?")).arg(name)) != QMessageBox::Yes) {
            return;
        }
        settings.beginGroup(name);
        saveFields(settings, *this);
        settings.endGroup();
        settings.endGroup();
        refreshProfiles(name);
        setStatus(QStringLiteral("Saved profile: %1").arg(name));
    }
    void loadNamedProfile() {
        if (active_ || capturePending_ || profiles_->currentIndex() < 0) return;
        if (sequenceEditor_->hasUnsavedActionChanges()) {
            navigation_->setCurrentRow(1);
            setStatus(QStringLiteral("Apply changes to the selected action before starting."));
            return;
        }
        const auto name = profiles_->currentText();
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.beginGroup(QStringLiteral("profiles"));
        if (!settings.childGroups().contains(name)) return;
        settings.beginGroup(name);
        // Older profiles without shortcut keys remain compatible.
        if (settings.contains(QStringLiteral("shortcutStop"))) {
            ShortcutOptions keys = shortcutValues();
            keys.enableStart = settings.value(QStringLiteral("shortcutEnableStart"), keys.enableStart).toBool();
            keys.enablePick = settings.value(QStringLiteral("shortcutEnablePick"), keys.enablePick).toBool();
            keys.start = QKeySequence(settings.value(QStringLiteral("shortcutStart"),
                                      keys.start.toString(QKeySequence::PortableText)).toString());
            keys.stop = QKeySequence(settings.value(QStringLiteral("shortcutStop"),
                                     keys.stop.toString(QKeySequence::PortableText)).toString());
            keys.pick = QKeySequence(settings.value(QStringLiteral("shortcutPick"),
                                     keys.pick.toString(QKeySequence::PortableText)).toString());
            bool valid = true;
#ifdef _WIN32
            valid = validateShortcuts(keys);
            if (valid && hotkeys_) valid = hotkeys_->configure(keys);
#endif
            if (!valid) {
                settings.endGroup();
                settings.endGroup();
                QMessageBox::warning(this, uiText(QStringLiteral("Profiles")),
                    uiText(QStringLiteral("Profile shortcut unavailable. No settings were changed.")));
                return;
            }
            const QSignalBlocker a(enableStartShortcut_);
            const QSignalBlocker b(enablePickShortcut_);
            const QSignalBlocker c(startShortcut_);
            const QSignalBlocker d(stopShortcut_);
            const QSignalBlocker e(pickShortcut_);
            enableStartShortcut_->setChecked(keys.enableStart);
            enablePickShortcut_->setChecked(keys.enablePick);
            startShortcut_->setKeySequence(keys.start);
            stopShortcut_->setKeySequence(keys.stop);
            pickShortcut_->setKeySequence(keys.pick);
            startShortcut_->setEnabled(keys.enableStart);
            pickShortcut_->setEnabled(keys.enablePick);
            // Persist the loaded bindings for subsequent launches.
            QSettings userSettings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
            userSettings.setValue(QStringLiteral("shortcuts/enableStart"), keys.enableStart);
            userSettings.setValue(QStringLiteral("shortcuts/enablePick"), keys.enablePick);
            userSettings.setValue(QStringLiteral("shortcuts/start"), keys.start.toString(QKeySequence::PortableText));
            userSettings.setValue(QStringLiteral("shortcuts/stop"), keys.stop.toString(QKeySequence::PortableText));
            userSettings.setValue(QStringLiteral("shortcuts/pick"), keys.pick.toString(QKeySequence::PortableText));
            refreshShortcutLabels();
        }
        restoreFields(settings);
        const QString preferredLanguage = settings.value(QStringLiteral("language"), language_).toString();
        settings.endGroup();
        settings.endGroup();
        if (preferredLanguage == QStringLiteral("en") || preferredLanguage == QStringLiteral("ru")) {
            language_ = preferredLanguage;
            const QSignalBlocker block(languageSelector_);
            languageSelector_->setCurrentIndex(language_ == QStringLiteral("ru") ? 1 : 0);
            QSettings userSettings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
            userSettings.setValue(QStringLiteral("ui/language"), language_);
            applyQtTranslator();
            localizeUi();
        }
        saveLastSettings();
        setStatus(QStringLiteral("Loaded profile: %1").arg(name));
    }
    void deleteNamedProfile() {
        if (active_ || capturePending_ || profiles_->currentIndex() < 0) return;
        const auto name = profiles_->currentText();
        if (QMessageBox::question(this, uiText(QStringLiteral("Delete profile")),
            uiText(QStringLiteral("Delete saved profile '%1'?")).arg(name)) != QMessageBox::Yes) return;
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.remove(QStringLiteral("profiles/") + name);
        refreshProfiles();
        setStatus(QStringLiteral("Deleted profile: %1").arg(name));
    }

    std::unique_ptr<QTranslator> qtTranslator_;
    QString language_{QStringLiteral("en")};
    QString lastStatusEnglish_{QStringLiteral("Ready")};
    QString savedWindowTitle_;
    QComboBox* languageSelector_{nullptr};
    QListWidget* navigation_{nullptr};
    InputLabWidget* inputLab_{nullptr};
    bool legacyRunRequested_{false};
    QStackedWidget* pages_{nullptr};
    QTabWidget* clickConfigTabs_{nullptr};
    QTabWidget* settingsTabs_{nullptr};
    QCheckBox* optionalDetails_{nullptr};
    QKeySequenceEdit* startShortcut_{nullptr};
    QKeySequenceEdit* stopShortcut_{nullptr};
    QKeySequenceEdit* pickShortcut_{nullptr};
    QCheckBox* enableStartShortcut_{nullptr};
    QCheckBox* enablePickShortcut_{nullptr};
    int captureToken_{0};
    QComboBox* button_{nullptr};
    QComboBox* quickAction_{nullptr};
    QFormLayout* quickForm_{nullptr};
    QPushButton* quickChooseButton_{nullptr};
    QGroupBox* quickMoveGroup_{nullptr};
    QLabel* quickMoveReadiness_{nullptr};
    QSpinBox* quickMoveX_{nullptr};
    QSpinBox* quickMoveY_{nullptr};
    QSpinBox* quickMoveDuration_{nullptr};
    QPushButton* quickCaptureDestination_{nullptr};
    bool destinationConfigured_{false};
    bool endpointCapture_{false};
    QSpinBox* interval_{nullptr};
    QSpinBox* quickHoldDuration_{nullptr};
    QSpinBox* count_{nullptr};
    QLabel* startDelayLabel_{nullptr};
    QComboBox* startMode_{nullptr};
    QTimeEdit* startAtTime_{nullptr};
    QSpinBox* startDelayValue_{nullptr};
    QComboBox* startDelayUnit_{nullptr};
    QCheckBox* keepAwakeQuick_{nullptr};
    clicknova::platform::WindowsAwakeGuard awakeQuickGuard_{false};
    QLabel* countLabel_{nullptr};
    QCheckBox* infinite_{nullptr};
    QComboBox* positionMode_{nullptr};
    QComboBox* targetMode_{nullptr};
    QCheckBox* quickPosition_{nullptr};
    QCheckBox* quickWindow_{nullptr};
    QCheckBox* quickSequence_{nullptr};
    QGroupBox* quickClickGroup_{nullptr};
    QPushButton* customizeClicks_{nullptr};
    QCheckBox* sequenceEnableOnPage_{nullptr};
    QCheckBox* sequenceUnlimited_{nullptr};
    QSpinBox* sequenceRepeatCount_{nullptr};
    QLabel* sequenceReady_{nullptr};
    QLabel* sequencePreview_{nullptr};
    QPushButton* editSequence_{nullptr};
    SequenceEditor* sequenceEditor_{nullptr};
    std::unique_ptr<clicknova::sequence::Cursor> sequenceCursor_;
    QPushButton* editPosition_{nullptr};
    QPushButton* editWindow_{nullptr};
    QLabel* activeSetup_{nullptr};
    QLabel* setupHelp_{nullptr};
    QPoint screenCoordinates_{0, 0};
    QPoint clientCoordinates_{0, 0};
    int lastTargetMode_{0};
    QComboBox* targetWindows_{nullptr};
    QPushButton* chooseWindow_{nullptr};
    QPushButton* refreshWindows_{nullptr};
    QPushButton* captureWindow_{nullptr};
    QLabel* targetHelp_{nullptr};
    QLabel* caution_{nullptr};
    std::vector<clicknova::platform::WindowTarget> availableWindows_;
    std::optional<clicknova::platform::WindowTarget> selectedTarget_;
    QSpinBox* x_{nullptr};
    QSpinBox* y_{nullptr};
    QPushButton* capture_{nullptr};
    QSpinBox* positionCaptureSeconds_{nullptr};
    QSpinBox* windowCaptureSeconds_{nullptr};
    clicknova::storage::ScenarioStore scenarioStore_;
    QComboBox* clickScenarios_{nullptr};
    QComboBox* sequenceScenarios_{nullptr};
    QPushButton* saveClickScenario_{nullptr};
    QPushButton* saveSequenceScenario_{nullptr};
    QPushButton* loadClickScenario_{nullptr};
    QPushButton* loadSequenceScenario_{nullptr};
    QPushButton* deleteClickScenario_{nullptr};
    QPushButton* deleteSequenceScenario_{nullptr};
    QComboBox* profiles_{nullptr};
    QPushButton* saveProfile_{nullptr};
    QPushButton* loadProfile_{nullptr};
    QPushButton* deleteProfile_{nullptr};
    bool capturePending_{false};
    bool restoringSettings_{false};
    QPushButton* start_{nullptr};
    QPushButton* stop_{nullptr};
    QDockWidget* logDock_{nullptr};
    QPlainTextEdit* activityLog_{nullptr};
    QLabel* toolbarWorkflow_{nullptr};
    QAction* quickStartAction_{nullptr};
    QAction* quickStopAction_{nullptr};
    QAction* stepStartAction_{nullptr};
    QAction* stepStopAction_{nullptr};
    bool sequenceToolbarActive_{false};
    QString previousLogText_;
    qint64 previousLogTime_{0};
    QLabel* workflowFeedback_{nullptr};
    QLabel* status_{nullptr};
    QTimer clickTimer_;
    QTimer quickReleaseTimer_;
    QTimer quickMotionTimer_;
    QElapsedTimer quickMotionClock_;
    clicknova::quick::Motion quickMotion_;
    bool quickMotionActive_{false};
    std::optional<clicknova::platform::WindowTarget> quickMotionWindow_;
    bool quickMouseDown_{false};
    clicknova::input::Button quickHeldButton_{clicknova::input::Button::Left};
    QTimer countdownTimer_;
    QTimer captureCountdownTimer_;
    QLabel* captureOverlay_{nullptr};
    bool hiddenForCapture_{false};
    bool wasMaximizedBeforeCapture_{false};
    int captureRemaining_{0};
    bool sequenceTimedCapture_{false};
    bool captureWindowPending_{false};
    bool captureStepWindowPending_{false};
    bool sequenceHoverPicking_{false};
    bool active_{false};
    std::uint64_t done_{0};
    std::int64_t startDelayMs_{3000};
    QElapsedTimer countdownClock_;
    bool countdownPending_{false};
#ifdef _WIN32
    MousePointPicker mousePicker_;
    std::unique_ptr<Hotkeys> hotkeys_;
#endif
};

} // namespace

QMainWindow* createClickNovaMainWindow() { return new MainWindow(); }
bool clicknovaShortcutSmoke() {
#ifdef _WIN32
    {
        ShortcutOptions defaults;
        ShortcutOptions duplicate = defaults;
        duplicate.stop = duplicate.start;
        ShortcutOptions bareLetter = defaults;
        bareLetter.stop = QKeySequence(QStringLiteral("A"));
        ShortcutOptions different = defaults;
        different.enableStart = false;
        different.stop = QKeySequence(QStringLiteral("Ctrl+Alt+K"));
        different.pick = QKeySequence(QStringLiteral("Shift+F9"));
        const bool valid = validateShortcuts(defaults) &&
                           !validateShortcuts(duplicate) &&
                           !validateShortcuts(bareLetter) &&
                           validateShortcuts(different);
        return valid;
    }
#else
    return true;
#endif
}
