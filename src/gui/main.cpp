// SPDX-License-Identifier: Apache-2.0
// Windows desktop input prototype; the Linux UI does not inject input yet.
#include <QApplication>
#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPoint>
#include <QListWidget>
#include <QStackedWidget>
#include <QKeySequenceEdit>
#include <QKeyCombination>
#include <QEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QMessageBox>
#include <QPushButton>
#include <QDebug>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <iterator>
#include <algorithm>
#include <optional>
#include <vector>
#include <memory>
#include <utility>
#include "clicknova/version.hpp"
#include "clicknova/window_target.hpp"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <QAbstractNativeEventFilter>
#endif

namespace {
struct ShortcutOptions {
    QKeySequence start{QStringLiteral("F6")};
    QKeySequence stop{QStringLiteral("F7")};
    QKeySequence pick{QStringLiteral("F8")};
    bool enableStart{true};
    bool enablePick{true};
};

#ifdef _WIN32
// Use the real foreground input stream. This is NOT targeted background input.
bool sendClick(DWORD buttonDown, DWORD buttonUp) {
    INPUT events[2]{};
    events[0].type = INPUT_MOUSE;
    events[0].mi.dwFlags = buttonDown;
    events[1].type = INPUT_MOUSE;
    events[1].mi.dwFlags = buttonUp;
    const UINT sent = SendInput(2, events, sizeof(INPUT));
    if (sent == 1) {
        // A partial injection may leave a button pressed; attempt to release it.
        INPUT release{};
        release.type = INPUT_MOUSE;
        release.mi.dwFlags = buttonUp;
        SendInput(1, &release, sizeof(INPUT));
    }
    return sent == 2;
}

// Only explicitly supported hotkeys are registered. OS-reserved combinations
// and shortcuts in use by other applications are never silently overridden.
struct ShortcutBinding {
    UINT modifiers{0};
    UINT key{0};
    bool valid{false};
};

ShortcutBinding toNativeBinding(const QKeySequence& sequence) {
    if (sequence.count() != 1) return {};
    const QKeyCombination chord = sequence[0];
    const Qt::KeyboardModifiers modifiers = chord.keyboardModifiers();
    if (modifiers.testFlag(Qt::MetaModifier) || modifiers.testFlag(Qt::GroupSwitchModifier)) return {};
    const int code = static_cast<int>(chord.key());
    UINT virtualKey = 0;
    if (code >= Qt::Key_F1 && code <= Qt::Key_F11) {
        virtualKey = VK_F1 + static_cast<UINT>(code - Qt::Key_F1);
    } else if (code >= Qt::Key_A && code <= Qt::Key_Z) {
        virtualKey = 'A' + static_cast<UINT>(code - Qt::Key_A);
    } else if (code >= Qt::Key_0 && code <= Qt::Key_9) {
        virtualKey = '0' + static_cast<UINT>(code - Qt::Key_0);
    } else {
        return {};
    }
    UINT nativeMods = MOD_NOREPEAT;
    if (modifiers.testFlag(Qt::ControlModifier)) nativeMods |= MOD_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier)) nativeMods |= MOD_ALT;
    if (modifiers.testFlag(Qt::ShiftModifier)) nativeMods |= MOD_SHIFT;
    // Do not intercept ordinary typing with bare alphanumeric shortcuts.
    if (code < Qt::Key_F1 && !(nativeMods & (MOD_CONTROL | MOD_ALT))) return {};
    return {nativeMods, virtualKey, true};
}

bool sameBinding(const ShortcutBinding& left, const ShortcutBinding& right) {
    return left.modifiers == right.modifiers && left.key == right.key;
}

bool validateShortcuts(const ShortcutOptions& options) {
    const auto stop = toNativeBinding(options.stop);
    const auto start = toNativeBinding(options.start);
    const auto pick = toNativeBinding(options.pick);
    if (!stop.valid || (options.enableStart && !start.valid) ||
        (options.enablePick && !pick.valid)) return false;
    if (options.enableStart && sameBinding(stop, start)) return false;
    if (options.enablePick && (sameBinding(stop, pick) ||
        (options.enableStart && sameBinding(start, pick)))) return false;
    return true;
}

class Hotkeys final : public QAbstractNativeEventFilter {
public:
    Hotkeys(std::function<void()> onStart, std::function<void()> onStop,
            std::function<void()> onPick)
        : onStart_(std::move(onStart)), onStop_(std::move(onStop)),
          onPick_(std::move(onPick)) {}
    ~Hotkeys() override { unregisterAll(); }

    bool configure(const ShortcutOptions& options) {
        if (!validateShortcuts(options)) return false;
        const auto original = options_;
        const bool previouslyConfigured = configured_;
        unregisterAll();
        options_ = options;
        configured_ = true;
        if (suspended_ || registerAll()) return true;
        unregisterAll();
        options_ = original;
        configured_ = previouslyConfigured;
        if (configured_ && !suspended_) registerAll();
        return false;
    }
    void suspend() { suspended_ = true; unregisterAll(); }
    void resume() {
        if (!suspended_) return;
        suspended_ = false;
        if (configured_) registerAll();
    }
    void setPicking(bool picking) {
        picking_ = picking;
        if (suspended_) return;
        unregisterAll();
        if (configured_) registerAll();
    }
    bool setRunning(bool running) {
        if (running_ == running) return true;
        unregisterAll();
        running_ = running;
        if (configured_ && !suspended_ && !registerAll()) {
            unregisterAll();
            running_ = !running;
            if (configured_) registerAll();
            return false;
        }
        return true;
    }
    bool ready() const { return configured_ && (!options_.enableStart || registeredStart_); }
    bool pickerReady() const { return registeredPick_; }
    const ShortcutOptions& configured() const { return options_; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override {
        Q_UNUSED(result);
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") return false;
        const auto* msg = static_cast<MSG*>(message);
        if (msg->message != WM_HOTKEY) return false;
        if (registeredStop_ && msg->wParam == kStopId) { onStop_(); return true; }
        if (registeredStart_ && msg->wParam == kStartId) { onStart_(); return true; }
        if (registeredPick_ && msg->wParam == kPickId) { onPick_(); return true; }
        return false;
    }
private:
    static constexpr int kStartId = 0x0C06;
    static constexpr int kStopId = 0x0C07;
    static constexpr int kPickId = 0x0C08;
    void unregisterAll() {
        if (registeredStart_) UnregisterHotKey(nullptr, kStartId);
        if (registeredStop_) UnregisterHotKey(nullptr, kStopId);
        if (registeredPick_) UnregisterHotKey(nullptr, kPickId);
        registeredStart_ = registeredStop_ = registeredPick_ = false;
    }
    bool registerAll() {
        if (!configured_ || suspended_) return false;
        if (running_) {
            const auto stop = toNativeBinding(options_.stop);
            registeredStop_ = RegisterHotKey(nullptr, kStopId, stop.modifiers, stop.key) != 0;
            if (!registeredStop_) return false;
        } else if (!picking_ && options_.enableStart) {
            const auto start = toNativeBinding(options_.start);
            registeredStart_ = RegisterHotKey(nullptr, kStartId, start.modifiers, start.key) != 0;
            if (!registeredStart_) return false;
        }
        if (picking_ && options_.enablePick) {
            const auto pick = toNativeBinding(options_.pick);
            registeredPick_ = RegisterHotKey(nullptr, kPickId, pick.modifiers, pick.key) != 0;
            // Timed capture remains available when the pick shortcut is in use.
        }
        return true;
    }
    ShortcutOptions options_;
    bool configured_{false};
    bool suspended_{false};
    bool picking_{false};
    bool running_{false};
    bool registeredStart_{false};
    bool registeredStop_{false};
    bool registeredPick_{false};
    std::function<void()> onStart_, onStop_, onPick_;
};
#endif

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("ClickNova — Auto Clicker v") +
                       QString::fromLatin1(CLICKNOVA_VERSION));
        resize(980, 730);
        setMinimumSize(460, 360);
        auto* root = new QWidget(this);
        auto* layout = new QVBoxLayout(root);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(12);
        auto* brand = new QLabel(QStringLiteral("ClickNova"));
        brand->setObjectName(QStringLiteral("heading"));
        layout->addWidget(brand);

        auto* body = new QHBoxLayout;
        body->setSpacing(16);
        navigation_ = new QListWidget(root);
        navigation_->setObjectName(QStringLiteral("navigation"));
        navigation_->setFixedWidth(190);
        navigation_->setSpacing(6);
        navigation_->addItems({QStringLiteral("Auto Clicker"), QStringLiteral("Position"),
                               QStringLiteral("Windows"), QStringLiteral("Profiles"),
                               QStringLiteral("Shortcuts")});
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
                                     QStringLiteral("Configure clicks here. Enable optional position and window targeting independently below."));
        auto* positionPage = createPage(QStringLiteral("Position"),
                                        QStringLiteral("Optional: set a fixed position when enabled on the Auto Clicker page. Otherwise the current cursor is used."));
        auto* windowsPage = createPage(QStringLiteral("Windows"),
                                       QStringLiteral("Optional: select a window to restrict clicking. This is NOT background input; the target must stay foreground."));
        auto* profilePage = createPage(QStringLiteral("Profiles"),
                                       QStringLiteral("Save and restore your frequently used configurations."));
        auto* shortcutsPage = createPage(QStringLiteral("Shortcuts"),
                                         QStringLiteral("Customize keyboard controls to avoid conflicts with other apps."));
        for (auto* page : {clickPage, positionPage, windowsPage, profilePage, shortcutsPage})
            page->addStretch();
        connect(navigation_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);
        navigation_->setCurrentRow(0);

        auto* group = new QGroupBox(QStringLiteral("Quick Auto Clicker"));
        auto* form = new QFormLayout(group);
        form->setContentsMargins(16, 20, 16, 16);
        form->setHorizontalSpacing(16);
        form->setVerticalSpacing(12);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        group->setMinimumHeight(286);
        group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
        button_ = new QComboBox;
        button_->setMinimumHeight(44);
        button_->addItems({QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Middle")});
        form->addRow(QStringLiteral("Mouse button"), button_);
        interval_ = new QSpinBox;
        interval_->setMinimumHeight(44);
        interval_->setRange(10, 60000);
        interval_->setValue(200);
        interval_->setSuffix(QStringLiteral(" ms"));
        form->addRow(QStringLiteral("Interval"), interval_);
        infinite_ = new QCheckBox(QStringLiteral("Unlimited clicks"));
        infinite_->setMinimumHeight(44);
        infinite_->setChecked(true);
        form->addRow(QStringLiteral("Mode"), infinite_);
        count_ = new QSpinBox;
        count_->setMinimumHeight(44);
        count_->setRange(1, 1000000);
        count_->setValue(100);
        count_->setEnabled(false);
        form->addRow(QStringLiteral("Click count"), count_);
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
        connect(positionMode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { updatePositionInputs(); });
        connect(capture_, &QPushButton::clicked, this, [this] { captureCursor(); });
        updatePositionInputs();
        positionPage->insertWidget(positionPage->count() - 1, positionGroup);
        clickPage->insertWidget(clickPage->count() - 1, group);

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
        auto* targetActionsLayout = new QHBoxLayout(targetActions);
        targetActionsLayout->setContentsMargins(0, 0, 0, 0);
        targetActionsLayout->setSpacing(8);
        refreshWindows_ = new QPushButton(QStringLiteral("Refresh list"));
        captureWindow_ = new QPushButton(QStringLiteral("Pick active window after 3 seconds"));
        refreshWindows_->setMinimumHeight(44);
        captureWindow_->setMinimumHeight(44);
        targetActionsLayout->addWidget(refreshWindows_);
        targetActionsLayout->addWidget(captureWindow_);
        targetForm->addRow(QStringLiteral("Select"), targetActions);
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
                    const QPoint point = lastTargetMode_ == 1 ? clientCoordinates_ : screenCoordinates_;
                    x_->setValue(point.x());
                    y_->setValue(point.y());
                    updateTargetInputs();
                    updatePositionInputs();
                    if (!qApp->arguments().contains(QStringLiteral("--smoke-test-ui")))
                        saveLastSettings();
                });
        connect(targetWindows_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int selected) {
                    if (selected >= 0 && selected < static_cast<int>(availableWindows_.size())) {
                        selectedTarget_ = availableWindows_[static_cast<std::size_t>(selected)];
                    } else {
                        selectedTarget_.reset();
                    }
                    updateTargetInputs();
                });
        connect(refreshWindows_, &QPushButton::clicked, this, [this] { refreshWindowList(); });
        connect(captureWindow_, &QPushButton::clicked, this, [this] { captureActiveWindow(); });

        // These are independent options, not separate automation modes.
        auto* optionsGroup = new QGroupBox(QStringLiteral("Optional features"));
        auto* optionsLayout = new QVBoxLayout(optionsGroup);
        optionsLayout->setContentsMargins(16, 22, 16, 16);
        optionsLayout->setSpacing(12);
        auto* optionsIntro = new QLabel(QStringLiteral(
            "The auto clicker always runs. Choose neither option, either option, or both. "
            "The Position and Windows pages only configure the options you enable here."));
        optionsIntro->setWordWrap(true);
        optionsIntro->setObjectName(QStringLiteral("subheading"));
        optionsLayout->addWidget(optionsIntro);

        auto* positionRow = new QHBoxLayout;
        quickPosition_ = new QCheckBox(QStringLiteral("Use fixed click position"));
        quickPosition_->setObjectName(QStringLiteral("optionUseFixedPosition"));
        editPosition_ = new QPushButton(QStringLiteral("Configure position..."));
        positionRow->addWidget(quickPosition_, 1);
        positionRow->addWidget(editPosition_);
        optionsLayout->addLayout(positionRow);

        auto* windowRow = new QHBoxLayout;
        quickWindow_ = new QCheckBox(QStringLiteral("Limit clicks to a selected window"));
        quickWindow_->setObjectName(QStringLiteral("optionLimitToWindow"));
        editWindow_ = new QPushButton(QStringLiteral("Choose window..."));
        windowRow->addWidget(quickWindow_, 1);
        windowRow->addWidget(editWindow_);
        optionsLayout->addLayout(windowRow);

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
            if (!qApp->arguments().contains(QStringLiteral("--smoke-test-ui"))) saveLastSettings();
        });
        connect(quickWindow_, &QCheckBox::toggled, this, [this](bool enabled) {
            if (active_ || capturePending_) return;
            targetMode_->setCurrentIndex(enabled ? 1 : 0);
            updateTargetInputs();
        });
        connect(editPosition_, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(1); });
        connect(editWindow_, &QPushButton::clicked, this, [this] { navigation_->setCurrentRow(2); });
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

        auto* profileGroup = new QGroupBox(QStringLiteral("Profiles"));
        auto* profileLayout = new QVBoxLayout(profileGroup);
        profiles_ = new QComboBox;
        profiles_->setMinimumHeight(44);
        profiles_->setPlaceholderText(QStringLiteral("Choose a saved profile"));
        profileLayout->addWidget(profiles_);
        auto* profileActions = new QHBoxLayout;
        saveProfile_ = new QPushButton(QStringLiteral("Save as..."));
        loadProfile_ = new QPushButton(QStringLiteral("Load"));
        deleteProfile_ = new QPushButton(QStringLiteral("Delete"));
        for (auto* action : {saveProfile_, loadProfile_, deleteProfile_}) {
            action->setMinimumHeight(44);
            profileActions->addWidget(action);
        }
        profileLayout->addLayout(profileActions);
        profilePage->insertWidget(profilePage->count() - 1, profileGroup);
        connect(saveProfile_, &QPushButton::clicked, this, [this] { saveNamedProfile(); });
        connect(loadProfile_, &QPushButton::clicked, this, [this] { loadNamedProfile(); });
        connect(deleteProfile_, &QPushButton::clicked, this, [this] { deleteNamedProfile(); });
        connect(profiles_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { updateProfileActions(); });

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
        caution_ = new QLabel;
        caution_->setWordWrap(true);
        caution_->setObjectName(QStringLiteral("caution"));
        layout->addWidget(caution_);

        auto* buttons = new QHBoxLayout;
        start_ = new QPushButton(QStringLiteral("Start"));
        start_->setObjectName(QStringLiteral("primaryButton"));
        stop_ = new QPushButton(QStringLiteral("Stop"));
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
        setCentralWidget(root);
        refreshWindowList();
        if (!qApp->arguments().contains(QStringLiteral("--smoke-test-ui"))) {
            restoreLastSettings();
            refreshProfiles();
        }
        updateProfileActions();
        updateTargetInputs();

        connect(start_, &QPushButton::clicked, this, [this] { start(); });
        connect(stop_, &QPushButton::clicked, this, [this] { stop(); });
        connect(&clickTimer_, &QTimer::timeout, this, [this] { click(); });
        connect(&countdownTimer_, &QTimer::timeout, this, [this] { countdownTick(); });
        clickTimer_.setTimerType(Qt::PreciseTimer);
#ifdef _WIN32
        if (!qApp->arguments().contains(QStringLiteral("--smoke-test-ui"))) {
            hotkeys_ = std::make_unique<Hotkeys>([this] { start(); }, [this] { stop(); },
                                                 [this] { finishCursorCapture(); });
            qApp->installNativeEventFilter(hotkeys_.get());
            restoreShortcutSettings();
            if (!hotkeys_->ready()) {
                status_->setText(QStringLiteral("Start shortcut unavailable. Choose another key in Shortcuts."));
            }
        } else {
            refreshShortcutLabels();
        }
#else
        start_->setEnabled(false);
        status_->setText(QStringLiteral("Linux input injection is not available in this prototype."));
#endif
    }
    ~MainWindow() override {
        stop();
        if (!qApp->arguments().contains(QStringLiteral("--smoke-test-ui"))) saveLastSettings();
#ifdef _WIN32
        if (hotkeys_) qApp->removeNativeEventFilter(hotkeys_.get());
#endif
    }
private:
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
        start_->setText(enableStartShortcut_->isChecked()
            ? QStringLiteral("Start · %1").arg(start) : QStringLiteral("Start"));
        stop_->setText(QStringLiteral("Stop · %1").arg(stopHotkeyLabel()));
        capture_->setText(enablePickShortcut_->isChecked()
            ? QStringLiteral("Capture in 3 seconds · or %1").arg(
                  pickShortcut_->keySequence().toString(QKeySequence::NativeText))
            : QStringLiteral("Capture cursor after 3 seconds"));
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
            status_->setText(QStringLiteral("Shortcut conflict or unsupported combination. Previous settings restored."));
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
        status_->setText(QStringLiteral("Keyboard shortcuts saved."));
    }
    void setShortcutEditorsEnabled(bool enabled) {
        enableStartShortcut_->setEnabled(enabled);
        enablePickShortcut_->setEnabled(enabled);
        startShortcut_->setEnabled(enabled && enableStartShortcut_->isChecked());
        stopShortcut_->setEnabled(enabled);
        pickShortcut_->setEnabled(enabled && enablePickShortcut_->isChecked());
    }
    void start() {
#ifndef _WIN32
        return;
#else
        if (active_ || capturePending_) return;
        if (targetMode_->currentIndex() == 1) {
            if (!selectedTarget_) {
                status_->setText(QStringLiteral("Select a target window before starting."));
                return;
            }
            // A missing/reused HWND must never trigger a global click.
            const auto state = clicknova::platform::checkTarget(*selectedTarget_);
            if (state != clicknova::platform::TargetCheck::Ready &&
                state != clicknova::platform::TargetCheck::NotForeground) {
                status_->setText(QString::fromUtf8(clicknova::platform::targetCheckMessage(state)));
                return;
            }
        }
        // Reserve Stop only for the running session. Idle shortcuts do not
        // steal keys from games, editors or other applications.
        if (hotkeys_ && !hotkeys_->setRunning(true)) {
            navigation_->setCurrentRow(4);
            status_->setText(QStringLiteral("Stop shortcut is in use. Choose another shortcut before starting."));
            return;
        }
        saveLastSettings();
        active_ = true;
        setShortcutEditorsEnabled(false);
        done_ = 0;
        secondsToStart_ = 3;
        button_->setEnabled(false);
        interval_->setEnabled(false);
        count_->setEnabled(false);
        infinite_->setEnabled(false);
        positionMode_->setEnabled(false);
        x_->setEnabled(false);
        y_->setEnabled(false);
        capture_->setEnabled(false);
        profiles_->setEnabled(false);
        saveProfile_->setEnabled(false);
        loadProfile_->setEnabled(false);
        deleteProfile_->setEnabled(false);
        updateTargetInputs();
        start_->setEnabled(false);
        stop_->setEnabled(true);
        status_->setText(targetMode_->currentIndex() == 1
            ? QStringLiteral("Starting in 3 seconds. Focus the selected window. %1 to stop.").arg(stopHotkeyLabel())
            : QStringLiteral("Starting in 3 seconds. Move the cursor to a safe position. %1 to stop.").arg(stopHotkeyLabel()));
        countdownTimer_.start(1000);
#endif
    }
    void countdownTick() {
        if (!active_) return;
        --secondsToStart_;
        if (secondsToStart_ > 0) {
            status_->setText(QStringLiteral("Starting in %1 s · %2 to stop").arg(secondsToStart_).arg(stopHotkeyLabel()));
            return;
        }
        countdownTimer_.stop();
        clickTimer_.start(interval_->value());
        status_->setText(QStringLiteral("Running · %1 to stop").arg(stopHotkeyLabel()));
    }
    void click() {
        if (!active_) return;
#ifdef _WIN32
        constexpr DWORD down[] = {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_MIDDLEDOWN};
        constexpr DWORD up[] = {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP};
        const int buttonIndex = button_->currentIndex();
        int screenX = x_->value();
        int screenY = y_->value();
        if (targetMode_->currentIndex() == 1) {
            if (!selectedTarget_) {
                stopWithReason(QStringLiteral("Target window is not selected."));
                return;
            }
            using clicknova::platform::TargetCheck;
            TargetCheck state = TargetCheck::Missing;
            if (positionMode_->currentIndex() == 1) {
                state = clicknova::platform::resolveClientPoint(
                    *selectedTarget_, x_->value(), y_->value(), screenX, screenY);
            } else {
                POINT cursor{};
                if (GetCursorPos(&cursor)) {
                    screenX = cursor.x;
                    screenY = cursor.y;
                    state = clicknova::platform::checkScreenPoint(*selectedTarget_, screenX, screenY);
                }
            }
            if (state != TargetCheck::Ready) {
                stopWithReason(QString::fromUtf8(clicknova::platform::targetCheckMessage(state)));
                return;
            }
        }
        if (positionMode_->currentIndex() == 1 && !SetCursorPos(screenX, screenY)) {
            stopWithReason(QStringLiteral("Windows could not move the cursor to the selected position."));
            return;
        }
        // Re-check immediately before injection: an overlay or focus change must stop us.
        if (targetMode_->currentIndex() == 1 && selectedTarget_) {
            const auto state = clicknova::platform::checkScreenPoint(*selectedTarget_, screenX, screenY);
            if (state != clicknova::platform::TargetCheck::Ready) {
                stopWithReason(QString::fromUtf8(clicknova::platform::targetCheckMessage(state)));
                return;
            }
        }
        if (!sendClick(down[buttonIndex], up[buttonIndex])) {
            stop();
            QMessageBox::warning(this, QStringLiteral("ClickNova"),
                QStringLiteral("Windows rejected the input event. Automation has stopped."));
            return;
        }
        ++done_;
        status_->setText(QStringLiteral("Running · clicks: %1 · %2 to stop").arg(done_).arg(stopHotkeyLabel()));
        if (!infinite_->isChecked() && done_ >= count_->value()) stop();
#endif
    }
    void stop() {
        clickTimer_.stop();
        countdownTimer_.stop();
        if (!active_) return;
        active_ = false;
        if (hotkeys_) hotkeys_->setRunning(false);
        setShortcutEditorsEnabled(true);
        button_->setEnabled(true);
        interval_->setEnabled(true);
        infinite_->setEnabled(true);
        count_->setEnabled(!infinite_->isChecked());
        positionMode_->setEnabled(true);
        updatePositionInputs();
        profiles_->setEnabled(true);
        saveProfile_->setEnabled(true);
        updateProfileActions();
        start_->setEnabled(true);
        stop_->setEnabled(false);
        updateTargetInputs();
        status_->setText(QStringLiteral("Stopped · clicks completed: %1").arg(done_));
    }

    void stopWithReason(const QString& reason) {
        stop();
        status_->setText(reason + QStringLiteral(" Automation stopped."));
    }
    void updateQuickOptions() {
        if (!quickPosition_ || !quickWindow_ || !positionMode_ || !targetMode_) return;
        const bool fixed = positionMode_->currentIndex() == 1;
        const bool bound = targetMode_->currentIndex() == 1;
        {
            const QSignalBlocker blockPosition(quickPosition_);
            const QSignalBlocker blockWindow(quickWindow_);
            quickPosition_->setChecked(fixed);
            quickWindow_->setChecked(bound);
        }
        const bool editable = !active_ && !capturePending_;
        quickPosition_->setEnabled(editable);
        quickWindow_->setEnabled(editable);
        editPosition_->setEnabled(editable);
        editWindow_->setEnabled(editable);
        navigation_->item(1)->setText(fixed ? QStringLiteral("Position  · ON") : QStringLiteral("Position  · OFF"));
        navigation_->item(2)->setText(bound ? QStringLiteral("Windows  · ON") : QStringLiteral("Windows  · OFF"));
        const QString position = fixed
            ? (bound ? QStringLiteral("Fixed point in target window: (%1, %2)")
                         : QStringLiteral("Fixed point on screen: (%1, %2)"))
                    .arg(x_->value()).arg(y_->value())
            : QStringLiteral("Current cursor position");
        const QString target = bound
            ? (selectedTarget_
                ? QStringLiteral("Selected window: %1").arg(QString::fromStdWString(selectedTarget_->title))
                : QStringLiteral("Selected window: NOT SELECTED"))
            : QStringLiteral("Any foreground window (global mouse)");
        activeSetup_->setText(QStringLiteral("%1 · Every %2 ms · %3\n%4\n%5")
            .arg(button_->currentText())
            .arg(interval_->value())
            .arg(infinite_->isChecked() ? QStringLiteral("Unlimited")
                                        : QStringLiteral("%1 clicks").arg(count_->value()))
            .arg(target)
            .arg(position));
        setupHelp_->setText(bound && !selectedTarget_
            ? QStringLiteral("Select a window on the Windows page before pressing Start.")
            : bound
                ? QStringLiteral("Foreground only: keep the selected window focused. "
                                 "With Current cursor, the cursor must be inside its client area.")
                : QStringLiteral("No window restriction. Clicks use the global mouse and can affect other applications."));
    }

    void updatePositionInputs() {
        const bool enabled = positionMode_->currentIndex() == 1 && !active_ && !capturePending_;
        x_->setEnabled(enabled);
        y_->setEnabled(enabled);
#ifdef _WIN32
        capture_->setEnabled(enabled && !capturePending_);
#else
        capture_->setEnabled(false);
#endif
        updateQuickOptions();
    }
    void captureCursor() {
#ifdef _WIN32
        if (active_ || capturePending_ || positionMode_->currentIndex() != 1) return;
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
        status_->setText(QStringLiteral("Move the cursor to the target. Capturing in 3 seconds%1.").arg(shortcut));
        const int token = captureToken_;
        QTimer::singleShot(3000, this, [this, token] {
            if (capturePending_ && token == captureToken_) finishCursorCapture();
        });
#endif
    }
    void finishCursorCapture() {
#ifdef _WIN32
        if (!capturePending_ || positionMode_->currentIndex() != 1) return;
        POINT point{};
        if (GetCursorPos(&point)) {
            if (targetMode_->currentIndex() == 1) {
                if (!selectedTarget_ || clicknova::platform::checkScreenPoint(
                         *selectedTarget_, point.x, point.y) != clicknova::platform::TargetCheck::Ready) {
                    status_->setText(QStringLiteral("Place the cursor inside the focused target window."));
                } else {
                    HWND hwnd = reinterpret_cast<HWND>(selectedTarget_->handle);
                    POINT relative = point;
                    if (ScreenToClient(hwnd, &relative)) {
                        x_->setValue(relative.x);
                        y_->setValue(relative.y);
                        status_->setText(QStringLiteral("Captured client position: %1, %2")
                                             .arg(relative.x).arg(relative.y));
                        saveLastSettings();
                    }
                }
            } else {
                x_->setValue(point.x);
                y_->setValue(point.y);
                status_->setText(QStringLiteral("Captured screen position: %1, %2")
                                     .arg(point.x).arg(point.y));
                saveLastSettings();
            }
        } else {
            status_->setText(QStringLiteral("Unable to read the cursor position."));
        }
        capturePending_ = false;
        ++captureToken_;
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
        positionMode_->setItemText(1, bound ? QStringLiteral("Fixed client coordinates")
                                                : QStringLiteral("Fixed screen coordinates"));
        if (bound) {
            targetHelp_->setText(selectedTarget_
                ? QStringLiteral("Bound to: %1 (PID %2). Focus must remain on this window during clicks.")
                    .arg(QString::fromStdWString(selectedTarget_->title))
                    .arg(selectedTarget_->processId)
                : QStringLiteral("Select a window. Background clicks are not implemented; focus is required."));
            caution_->setText(QStringLiteral("BOUND WINDOW: Uses the global mouse. Automation stops if the target loses focus or becomes covered."));
        } else {
            targetHelp_->setText(QStringLiteral("Global mode does not bind to a window."));
            caution_->setText(QStringLiteral("GLOBAL MOUSE: Clicking affects the desktop; background automation is not available in this mode."));
        }
        updateQuickOptions();
    }
    void refreshWindowList() {
        if (active_ || capturePending_) return;
        availableWindows_ = clicknova::platform::listWindows();
        selectedTarget_.reset();
        const QSignalBlocker block(targetWindows_);
        targetWindows_->clear();
        for (const auto& window : availableWindows_) {
            QString title = QString::fromStdWString(window.title).simplified();
            if (title.size() > 100) title = title.left(100) + QStringLiteral("...");
            targetWindows_->addItem(QStringLiteral("%1 (PID %2)")
                                    .arg(title).arg(window.processId));
        }
        targetWindows_->setCurrentIndex(-1);
        updateTargetInputs();
    }
    void captureActiveWindow() {
#ifdef _WIN32
        if (active_ || capturePending_ || targetMode_->currentIndex() != 1) return;
        capturePending_ = true;
        setShortcutEditorsEnabled(false);
        updateTargetInputs();
        updatePositionInputs();
        positionMode_->setEnabled(false);
        start_->setEnabled(false);
        saveProfile_->setEnabled(false);
        updateProfileActions();
        updateTargetInputs();
        status_->setText(QStringLiteral("Focus your target window. Capturing in 3 seconds..."));
        QTimer::singleShot(3000, this, [this] {
            capturePending_ = false;
            setShortcutEditorsEnabled(true);
            const auto picked = clicknova::platform::foregroundWindow();
            if (!picked) {
                status_->setText(QStringLiteral("Could not capture a valid external foreground window."));
            } else {
                refreshWindowList();
                auto it = std::find_if(availableWindows_.begin(), availableWindows_.end(),
                    [&picked](const auto& item) {
                        return item.handle == picked->handle && item.processId == picked->processId;
                    });
                if (it == availableWindows_.end()) {
                    availableWindows_.push_back(*picked);
                    targetWindows_->addItem(QString::fromStdWString(picked->title));
                    targetWindows_->setCurrentIndex(targetWindows_->count() - 1);
                } else {
                    targetWindows_->setCurrentIndex(static_cast<int>(std::distance(availableWindows_.begin(), it)));
                }
                status_->setText(QStringLiteral("Window selected. Clicks require it to stay foreground."));
            }
            start_->setEnabled(true);
            positionMode_->setEnabled(true);
            saveProfile_->setEnabled(true);
            updatePositionInputs();
            updateProfileActions();
            updateTargetInputs();
        });
#endif
    }
    static void saveFields(QSettings& settings, const MainWindow& window) {
        settings.setValue(QStringLiteral("button"), window.button_->currentIndex());
        settings.setValue(QStringLiteral("interval"), window.interval_->value());
        settings.setValue(QStringLiteral("unlimited"), window.infinite_->isChecked());
        settings.setValue(QStringLiteral("count"), window.count_->value());
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
    }
    void restoreFields(QSettings& settings) {
        button_->setCurrentIndex(qBound(0, settings.value(QStringLiteral("button"), 0).toInt(), 2));
        interval_->setValue(settings.value(QStringLiteral("interval"), 200).toInt());
        infinite_->setChecked(settings.value(QStringLiteral("unlimited"), true).toBool());
        count_->setValue(settings.value(QStringLiteral("count"), 100).toInt());
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
        targetWindows_->setCurrentIndex(-1);
        const QPoint point = mode == 1 ? clientCoordinates_ : screenCoordinates_;
        x_->setValue(point.x());
        y_->setValue(point.y());
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
            this, QStringLiteral("Save profile"), QStringLiteral("Profile name:"),
            QLineEdit::Normal, profiles_->currentText(), &accepted).trimmed();
        if (!accepted) return;
        static const QRegularExpression validName(QStringLiteral("^[\\p{L}\\p{N}][\\p{L}\\p{N} _-]{0,39}$"));
        if (!validName.match(name).hasMatch()) {
            QMessageBox::warning(this, QStringLiteral("Invalid name"),
                QStringLiteral("Use 1-40 letters, numbers, spaces, hyphens, or underscores. Start with a letter or number."));
            return;
        }
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.beginGroup(QStringLiteral("profiles"));
        if (settings.childGroups().contains(name) &&
            QMessageBox::question(this, QStringLiteral("Overwrite profile"),
                                  QStringLiteral("Replace the existing profile '%1'?").arg(name)) != QMessageBox::Yes) {
            return;
        }
        settings.beginGroup(name);
        saveFields(settings, *this);
        settings.endGroup();
        settings.endGroup();
        refreshProfiles(name);
        status_->setText(QStringLiteral("Saved profile: %1").arg(name));
    }
    void loadNamedProfile() {
        if (active_ || capturePending_ || profiles_->currentIndex() < 0) return;
        const auto name = profiles_->currentText();
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.beginGroup(QStringLiteral("profiles"));
        if (!settings.childGroups().contains(name)) return;
        settings.beginGroup(name);
        restoreFields(settings);
        settings.endGroup();
        settings.endGroup();
        saveLastSettings();
        status_->setText(QStringLiteral("Loaded profile: %1").arg(name));
    }
    void deleteNamedProfile() {
        if (active_ || capturePending_ || profiles_->currentIndex() < 0) return;
        const auto name = profiles_->currentText();
        if (QMessageBox::question(this, QStringLiteral("Delete profile"),
            QStringLiteral("Delete saved profile '%1'?").arg(name)) != QMessageBox::Yes) return;
        QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        settings.remove(QStringLiteral("profiles/") + name);
        refreshProfiles();
        status_->setText(QStringLiteral("Deleted profile: %1").arg(name));
    }

    QListWidget* navigation_{nullptr};
    QStackedWidget* pages_{nullptr};
    QKeySequenceEdit* startShortcut_{nullptr};
    QKeySequenceEdit* stopShortcut_{nullptr};
    QKeySequenceEdit* pickShortcut_{nullptr};
    QCheckBox* enableStartShortcut_{nullptr};
    QCheckBox* enablePickShortcut_{nullptr};
    int captureToken_{0};
    QComboBox* button_{nullptr};
    QSpinBox* interval_{nullptr};
    QSpinBox* count_{nullptr};
    QCheckBox* infinite_{nullptr};
    QComboBox* positionMode_{nullptr};
    QComboBox* targetMode_{nullptr};
    QCheckBox* quickPosition_{nullptr};
    QCheckBox* quickWindow_{nullptr};
    QPushButton* editPosition_{nullptr};
    QPushButton* editWindow_{nullptr};
    QLabel* activeSetup_{nullptr};
    QLabel* setupHelp_{nullptr};
    QPoint screenCoordinates_{0, 0};
    QPoint clientCoordinates_{0, 0};
    int lastTargetMode_{0};
    QComboBox* targetWindows_{nullptr};
    QPushButton* refreshWindows_{nullptr};
    QPushButton* captureWindow_{nullptr};
    QLabel* targetHelp_{nullptr};
    QLabel* caution_{nullptr};
    std::vector<clicknova::platform::WindowTarget> availableWindows_;
    std::optional<clicknova::platform::WindowTarget> selectedTarget_;
    QSpinBox* x_{nullptr};
    QSpinBox* y_{nullptr};
    QPushButton* capture_{nullptr};
    QComboBox* profiles_{nullptr};
    QPushButton* saveProfile_{nullptr};
    QPushButton* loadProfile_{nullptr};
    QPushButton* deleteProfile_{nullptr};
    bool capturePending_{false};
    QPushButton* start_{nullptr};
    QPushButton* stop_{nullptr};
    QLabel* status_{nullptr};
    QTimer clickTimer_;
    QTimer countdownTimer_;
    bool active_{false};
    int done_{0};
    int secondsToStart_{0};
#ifdef _WIN32
    std::unique_ptr<Hotkeys> hotkeys_;
#endif
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
#ifdef _WIN32
    if (app.arguments().contains(QStringLiteral("--smoke-test-shortcuts"))) {
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
        return valid ? 0 : 1;
    }
#endif
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(QStringLiteral(R"(
        QWidget { background: #171a20; color: #e9eef6; font-size: 13px; }
        QLabel#heading { font-size: 25px; font-weight: 700; color: #81aaff; }
        QLabel#pageHeading { font-size: 21px; font-weight: 700; color: #e9eef6; }
        QListWidget#navigation { border: 1px solid #303947; border-radius: 10px; background: #1d222b; padding: 8px; }
        QListWidget#navigation::item { border-radius: 7px; padding: 13px 9px; }
        QListWidget#navigation::item:selected { background: #294e92; color: #ffffff; }
        QListWidget#navigation::item:hover:!selected { background: #293341; }
        QLabel#subheading { color: #aab5c5; }
        QLabel#caution { color: #ebc581; }
        QGroupBox { border: 1px solid #394250; border-radius: 10px; margin-top: 12px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; }
        QPushButton { border: 1px solid #4b5973; padding: 12px; border-radius: 7px; background: #293341; }
        QPushButton#primaryButton { background: #285bd6; color: white; font-weight: bold; }
        QPushButton:hover:enabled { background: #3763ab; }
        QPushButton:disabled { color: #7d8998; }
        QScrollArea { border: 0; }
        QComboBox, QSpinBox, QKeySequenceEdit { border: 1px solid #4b5973; padding: 6px; border-radius: 6px; background: #242a35; }
        QCheckBox { padding: 4px; }
    )"));
    MainWindow window;
    window.show();
    if (app.arguments().contains(QStringLiteral("--smoke-test-ui"))) {
        // Headless-ish layout check: does not send clicks or register a macro.
        QTimer::singleShot(50, &app, [&app, &window] {
            window.resize(500, 360);
            app.processEvents();
            bool fieldsHaveMinimumHeight = true;
            for (const auto* spin : window.findChildren<QAbstractSpinBox*>()) {
                fieldsHaveMinimumHeight &= spin->height() >= 44;
            }
            for (const auto* combo : window.findChildren<QComboBox*>()) {
                fieldsHaveMinimumHeight &= combo->height() >= 44;
            }
            fieldsHaveMinimumHeight &= window.findChild<QComboBox*>(QStringLiteral("positionMode")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QComboBox*>(QStringLiteral("targetMode")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QComboBox*>(QStringLiteral("targetWindows")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QSpinBox*>(QStringLiteral("coordinateX")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QSpinBox*>(QStringLiteral("coordinateY")) != nullptr;
            const auto* nav = window.findChild<QListWidget*>(QStringLiteral("navigation"));
            fieldsHaveMinimumHeight &= nav != nullptr && nav->count() == 5;
            auto* fixed = window.findChild<QCheckBox*>(QStringLiteral("optionUseFixedPosition"));
            auto* bound = window.findChild<QCheckBox*>(QStringLiteral("optionLimitToWindow"));
            auto* positionMode = window.findChild<QComboBox*>(QStringLiteral("positionMode"));
            auto* targetMode = window.findChild<QComboBox*>(QStringLiteral("targetMode"));
            auto* setup = window.findChild<QLabel*>(QStringLiteral("activeSetup"));
            fieldsHaveMinimumHeight &= fixed && bound && positionMode && targetMode && setup;
            if (fixed && bound && positionMode && targetMode && setup) {
                for (int p = 0; p < 2; ++p) {
                    for (int w = 0; w < 2; ++w) {
                        fixed->setChecked(p == 1);
                        bound->setChecked(w == 1);
                        fieldsHaveMinimumHeight &= positionMode->currentIndex() == p;
                        fieldsHaveMinimumHeight &= targetMode->currentIndex() == w;
                        fieldsHaveMinimumHeight &= setup->text().contains(
                            w ? QStringLiteral("Selected window") : QStringLiteral("Any foreground window"));
                    }
                }
                // Screen and client positions must not overwrite one another.
                auto* x = window.findChild<QSpinBox*>(QStringLiteral("coordinateX"));
                auto* y = window.findChild<QSpinBox*>(QStringLiteral("coordinateY"));
                if (x && y) {
                    bound->setChecked(false);
                    x->setValue(120);
                    y->setValue(130);
                    bound->setChecked(true);
                    x->setValue(16);
                    y->setValue(18);
                    bound->setChecked(false);
                    fieldsHaveMinimumHeight &= x->value() == 120 && y->value() == 130;
                    bound->setChecked(true);
                    fieldsHaveMinimumHeight &= x->value() == 16 && y->value() == 18;
                }
                // Restore the safe defaults after the combination regression test.
                fixed->setChecked(false);
                bound->setChecked(false);
            }
            fieldsHaveMinimumHeight &= window.findChild<QKeySequenceEdit*>(QStringLiteral("shortcutStart")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QKeySequenceEdit*>(QStringLiteral("shortcutStop")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QKeySequenceEdit*>(QStringLiteral("shortcutPick")) != nullptr;
            const auto scrolls = window.findChildren<QScrollArea*>();
            fieldsHaveMinimumHeight &= scrolls.size() == 5;
            if (!fieldsHaveMinimumHeight) {
                qCritical() << "ClickNova UI test failed: fields are compressed or scrolling is unavailable";
            }
            app.exit(fieldsHaveMinimumHeight ? 0 : 1);
        });
    }
    return app.exec();
}
