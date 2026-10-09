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
#include <memory>
#include <utility>
#include "clicknova/version.hpp"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <QAbstractNativeEventFilter>
#endif

namespace {
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

class Hotkeys final : public QAbstractNativeEventFilter {
public:
    explicit Hotkeys(std::function<void()> onStart, std::function<void()> onStop)
        : onStart_(std::move(onStart)), onStop_(std::move(onStop)) {
        startAvailable_ = RegisterHotKey(nullptr, 0x0C06, MOD_NOREPEAT, VK_F6) != 0;
        stopAvailable_ = RegisterHotKey(nullptr, 0x0C07, MOD_NOREPEAT, VK_F7) != 0;
        if (!startAvailable_ || !stopAvailable_) {
            // Do not allow a global start hotkey without a global stop hotkey.
            if (startAvailable_) UnregisterHotKey(nullptr, 0x0C06);
            if (stopAvailable_) UnregisterHotKey(nullptr, 0x0C07);
            startAvailable_ = false;
            stopAvailable_ = false;
        }
    }
    ~Hotkeys() override {
        if (startAvailable_) UnregisterHotKey(nullptr, 0x0C06);
        if (stopAvailable_) UnregisterHotKey(nullptr, 0x0C07);
    }
    bool ready() const { return startAvailable_ && stopAvailable_; }
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override {
        Q_UNUSED(result);
        if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") return false;
        const auto* msg = static_cast<MSG*>(message);
        if (msg->message != WM_HOTKEY) return false;
        if (msg->wParam == 0x0C06) { onStart_(); return true; }
        if (msg->wParam == 0x0C07) { onStop_(); return true; }
        return false;
    }
private:
    std::function<void()> onStart_, onStop_;
    bool startAvailable_{false};
    bool stopAvailable_{false};
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
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(12);

        // A resizable window must scroll rather than crush controls vertically.
        auto* scroll = new QScrollArea(root);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* formPage = new QWidget;
        auto* pageLayout = new QVBoxLayout(formPage);
        pageLayout->setContentsMargins(6, 6, 6, 12);
        pageLayout->setSpacing(15);

        auto* heading = new QLabel(QStringLiteral("ClickNova"));
        heading->setObjectName(QStringLiteral("heading"));
        pageLayout->addWidget(heading);
        auto* subheading = new QLabel(QStringLiteral("Windows auto clicker · desktop prototype"));
        subheading->setObjectName(QStringLiteral("subheading"));
        subheading->setWordWrap(true);
        pageLayout->addWidget(subheading);

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

        positionMode_ = new QComboBox;
        positionMode_->setObjectName(QStringLiteral("positionMode"));
        positionMode_->setMinimumHeight(44);
        positionMode_->addItems({QStringLiteral("Current cursor"), QStringLiteral("Fixed screen coordinates")});
        form->addRow(QStringLiteral("Position"), positionMode_);

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
        form->addRow(QStringLiteral("Coordinates"), coordinates);

        capture_ = new QPushButton(QStringLiteral("Capture cursor after 3 seconds"));
        capture_->setMinimumHeight(44);
#ifndef _WIN32
        capture_->setEnabled(false);
#endif
        form->addRow(QStringLiteral("Pick coordinates"), capture_);
        connect(positionMode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { updatePositionInputs(); });
        connect(capture_, &QPushButton::clicked, this, [this] { captureCursor(); });
        updatePositionInputs();
        pageLayout->addWidget(group);

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
        pageLayout->addWidget(profileGroup);
        connect(saveProfile_, &QPushButton::clicked, this, [this] { saveNamedProfile(); });
        connect(loadProfile_, &QPushButton::clicked, this, [this] { loadNamedProfile(); });
        connect(deleteProfile_, &QPushButton::clicked, this, [this] { deleteNamedProfile(); });
        connect(profiles_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) { updateProfileActions(); });

        auto* caution = new QLabel(QStringLiteral("This mode controls the GLOBAL mouse, including when fixed coordinates are used. It cannot click a background window independently. Press F7 to stop."));
        caution->setWordWrap(true);
        caution->setObjectName(QStringLiteral("caution"));
        pageLayout->addWidget(caution);
        pageLayout->addStretch();
        scroll->setWidget(formPage);
        layout->addWidget(scroll, 1);

        auto* buttons = new QHBoxLayout;
        start_ = new QPushButton(QStringLiteral("Start · F6"));
        start_->setObjectName(QStringLiteral("primaryButton"));
        stop_ = new QPushButton(QStringLiteral("Stop · F7"));
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
        if (!qApp->arguments().contains(QStringLiteral("--smoke-test-ui"))) {
            restoreLastSettings();
            refreshProfiles();
        }
        updateProfileActions();

        connect(start_, &QPushButton::clicked, this, [this] { start(); });
        connect(stop_, &QPushButton::clicked, this, [this] { stop(); });
        connect(&clickTimer_, &QTimer::timeout, this, [this] { click(); });
        connect(&countdownTimer_, &QTimer::timeout, this, [this] { countdownTick(); });
        clickTimer_.setTimerType(Qt::PreciseTimer);
#ifdef _WIN32
        hotkeys_ = std::make_unique<Hotkeys>([this] { start(); }, [this] { stop(); });
        qApp->installNativeEventFilter(hotkeys_.get());
        if (!hotkeys_->ready()) {
            status_->setText(QStringLiteral("Warning: F6/F7 hotkeys are already in use. Use the buttons instead."));
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
    void start() {
#ifndef _WIN32
        return;
#else
        if (active_ || capturePending_) return;
        saveLastSettings();
        active_ = true;
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
        start_->setEnabled(false);
        stop_->setEnabled(true);
        status_->setText(QStringLiteral("Starting in 3 seconds. Move the cursor to a safe position; press F7 to cancel."));
        countdownTimer_.start(1000);
#endif
    }
    void countdownTick() {
        if (!active_) return;
        --secondsToStart_;
        if (secondsToStart_ > 0) {
            status_->setText(QStringLiteral("Starting in %1 s · F7 to stop").arg(secondsToStart_));
            return;
        }
        countdownTimer_.stop();
        clickTimer_.start(interval_->value());
        status_->setText(QStringLiteral("Running · F7 to stop"));
    }
    void click() {
        if (!active_) return;
#ifdef _WIN32
        constexpr DWORD down[] = {MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_MIDDLEDOWN};
        constexpr DWORD up[] = {MOUSEEVENTF_LEFTUP, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_MIDDLEUP};
        const int buttonIndex = button_->currentIndex();
        if (positionMode_->currentIndex() == 1 && !SetCursorPos(x_->value(), y_->value())) {
            stop();
            QMessageBox::warning(this, QStringLiteral("ClickNova"),
                QStringLiteral("Windows could not move the cursor to the selected coordinates. Automation stopped."));
            return;
        }
        if (!sendClick(down[buttonIndex], up[buttonIndex])) {
            stop();
            QMessageBox::warning(this, QStringLiteral("ClickNova"),
                QStringLiteral("Windows rejected the input event. Automation has stopped."));
            return;
        }
        ++done_;
        status_->setText(QStringLiteral("Running · clicks: %1 · F7 to stop").arg(done_));
        if (!infinite_->isChecked() && done_ >= count_->value()) stop();
#endif
    }
    void stop() {
        clickTimer_.stop();
        countdownTimer_.stop();
        if (!active_) return;
        active_ = false;
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
        status_->setText(QStringLiteral("Stopped · clicks completed: %1").arg(done_));
    }

    void updatePositionInputs() {
        const bool enabled = positionMode_->currentIndex() == 1 && !active_;
        x_->setEnabled(enabled);
        y_->setEnabled(enabled);
#ifdef _WIN32
        capture_->setEnabled(enabled && !capturePending_);
#else
        capture_->setEnabled(false);
#endif
    }
    void captureCursor() {
#ifdef _WIN32
        if (active_ || capturePending_ || positionMode_->currentIndex() != 1) return;
        capturePending_ = true;
        capture_->setEnabled(false);
        start_->setEnabled(false);
        profiles_->setEnabled(false);
        saveProfile_->setEnabled(false);
        updateProfileActions();
        status_->setText(QStringLiteral("Move the mouse to the target position. Capturing in 3 seconds..."));
        QTimer::singleShot(3000, this, [this] {
            POINT point{};
            if (GetCursorPos(&point)) {
                x_->setValue(point.x);
                y_->setValue(point.y);
                status_->setText(QStringLiteral("Captured cursor position: %1, %2")
                                     .arg(point.x).arg(point.y));
                saveLastSettings();
            } else {
                status_->setText(QStringLiteral("Unable to read the cursor position."));
            }
            capturePending_ = false;
            start_->setEnabled(true);
            profiles_->setEnabled(true);
            saveProfile_->setEnabled(true);
            updatePositionInputs();
            updateProfileActions();
        });
#endif
    }
    static void saveFields(QSettings& settings, const MainWindow& window) {
        settings.setValue(QStringLiteral("button"), window.button_->currentIndex());
        settings.setValue(QStringLiteral("interval"), window.interval_->value());
        settings.setValue(QStringLiteral("unlimited"), window.infinite_->isChecked());
        settings.setValue(QStringLiteral("count"), window.count_->value());
        settings.setValue(QStringLiteral("position"), window.positionMode_->currentIndex());
        settings.setValue(QStringLiteral("x"), window.x_->value());
        settings.setValue(QStringLiteral("y"), window.y_->value());
    }
    void restoreFields(QSettings& settings) {
        button_->setCurrentIndex(qBound(0, settings.value(QStringLiteral("button"), 0).toInt(), 2));
        interval_->setValue(settings.value(QStringLiteral("interval"), 200).toInt());
        infinite_->setChecked(settings.value(QStringLiteral("unlimited"), true).toBool());
        count_->setValue(settings.value(QStringLiteral("count"), 100).toInt());
        positionMode_->setCurrentIndex(qBound(0, settings.value(QStringLiteral("position"), 0).toInt(), 1));
        x_->setValue(settings.value(QStringLiteral("x"), 0).toInt());
        y_->setValue(settings.value(QStringLiteral("y"), 0).toInt());
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

    QComboBox* button_{nullptr};
    QSpinBox* interval_{nullptr};
    QSpinBox* count_{nullptr};
    QCheckBox* infinite_{nullptr};
    QComboBox* positionMode_{nullptr};
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
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(QStringLiteral(R"(
        QWidget { background: #171a20; color: #e9eef6; font-size: 13px; }
        QLabel#heading { font-size: 27px; font-weight: 700; color: #81aaff; }
        QLabel#subheading { color: #aab5c5; }
        QLabel#caution { color: #ebc581; }
        QGroupBox { border: 1px solid #394250; border-radius: 10px; margin-top: 12px; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; }
        QPushButton { border: 1px solid #4b5973; padding: 12px; border-radius: 7px; background: #293341; }
        QPushButton#primaryButton { background: #285bd6; color: white; font-weight: bold; }
        QPushButton:hover:enabled { background: #3763ab; }
        QPushButton:disabled { color: #7d8998; }
        QScrollArea { border: 0; }
        QComboBox, QSpinBox { border: 1px solid #4b5973; padding: 6px; border-radius: 6px; background: #242a35; }
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
            fieldsHaveMinimumHeight &= window.findChild<QSpinBox*>(QStringLiteral("coordinateX")) != nullptr;
            fieldsHaveMinimumHeight &= window.findChild<QSpinBox*>(QStringLiteral("coordinateY")) != nullptr;
            const auto* scroll = window.findChild<QScrollArea*>();
            fieldsHaveMinimumHeight &= scroll != nullptr &&
                                       scroll->verticalScrollBar()->maximum() > 0;
            if (!fieldsHaveMinimumHeight) {
                qCritical() << "ClickNova UI test failed: fields are compressed or scrolling is unavailable";
            }
            app.exit(fieldsHaveMinimumHeight ? 0 : 1);
        });
    }
    return app.exec();
}
