// SPDX-License-Identifier: Apache-2.0
#include "input_lab.hpp"
#include "action_table_widget.hpp"
#include "clicknova/action_list_edit.hpp"
#include "clicknova/duration_expression.hpp"
#include "input_capture_dialog.hpp"
#include "input_recorder_dialog.hpp"
#include "recording_checkpoint.hpp"
#include "recording_review_dialog.hpp"
#include "window_chooser.hpp"
#include "wrap_layout.hpp"
#include "ui_icons.hpp"
#include "clicknova/input_recording.hpp"
#include "../storage/input_plan_codec.hpp"
#include "../storage/scenario_codec.hpp"
#include "clicknova/sequence_to_input.hpp"
#include "clicknova/windows_awake.hpp"
#ifdef _WIN32
#include "clicknova/windows_input_backend.hpp"
#endif
#include <QComboBox>
#include <QCheckBox>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QSettings>
#include <QDateTime>
#include <QTimeEdit>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <limits>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QContextMenuEvent>
#include <QAction>
#include <QShortcut>
#include <QToolButton>
#include <QAbstractItemView>
#include <QItemSelectionModel>
#include <algorithm>
#include <string>
#include <initializer_list>
#include <type_traits>
#include <variant>
#include <utility>
#include <array>

using namespace clicknova::input;

#ifdef _WIN32
// Script processes run only after the user explicitly starts a plan. Stop
// terminates the child PowerShell process; code is never executed on load.
class ScriptBackend final : public clicknova::input::IBackend {
public:
    explicit ScriptBackend(std::optional<clicknova::platform::WindowTarget> target)
        : defaultTarget_(target), native_(std::move(target)) {}
    bool prepareStepWindow(const clicknova::input::StepWindow& step, std::stop_token stop) override {
        if (step.title.empty() || !step.enforce) {
            native_.setTarget(defaultTarget_);
            return !stop.stop_requested();
        }
        const auto title = QString::fromStdString(step.title).toStdWString();
        const auto process = QString::fromStdString(step.process).toStdWString();
        const auto started = std::chrono::steady_clock::now();
        while (!stop.stop_requested()) {
            if (const auto found = clicknova::platform::findWindow(title, process)) {
                if ((!step.activate && clicknova::platform::checkTarget(*found) ==
                     clicknova::platform::TargetCheck::Ready) ||
                    (step.activate && clicknova::platform::activateWindow(*found) &&
                     clicknova::platform::checkTarget(*found) == clicknova::platform::TargetCheck::Ready)) {
                    native_.setTarget(*found);
                    return true;
                }
            }
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count() >= step.waitMs) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }
    bool dispatch(const clicknova::input::Event& e) override { return native_.dispatch(e); }
    bool releaseOnCleanup(const clicknova::input::Event& e) override { return native_.releaseOnCleanup(e); }
    bool boundToWindow() const override { return native_.boundToWindow(); }
    std::optional<clicknova::input::PointerPosition> cursorPosition() override { return native_.cursorPosition(); }
    std::optional<clicknova::input::PointerPosition> clientToScreen(int x, int y) override {
        return native_.clientToScreen(x, y);
    }
    bool runScript(const std::u16string& code, std::stop_token stop) override {
        QTemporaryDir directory;
        if (!directory.isValid() || stop.stop_requested()) return false;
        const QString path = directory.filePath(QStringLiteral("clicknova-step.ps1"));
        QFile script(path);
        if (!script.open(QIODevice::WriteOnly)) return false;
        // PowerShell 5.1 expects a BOM to read UTF-8 non-ASCII source reliably.
        const QByteArray bytes = QByteArray("\xEF\xBB\xBF", 3) +
            QString::fromStdU16String(code).toUtf8();
        if (script.write(bytes) != bytes.size()) return false;
        script.close();
        QProcess process;
        process.setProgram(QStringLiteral("powershell.exe"));
        process.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
            QStringLiteral("-File"), path});
        // Never buffer unbounded script output in GUI process memory.
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        process.start();
        if (!process.waitForStarted(10000)) return false;
        while (process.state() != QProcess::NotRunning) {
            if (stop.stop_requested()) {
                process.kill();
                process.waitForFinished(5000);
                return false;
            }
            process.waitForFinished(100);
        }
        return !stop.stop_requested() && process.exitStatus() == QProcess::NormalExit &&
            process.exitCode() == 0;
    }
private:
    std::optional<clicknova::platform::WindowTarget> defaultTarget_;
    clicknova::input::WindowsInputBackend native_;
};
#endif
namespace {
constexpr std::int64_t kInvalidWideNumber = -1;
QLineEdit* largeIntegerEdit(const char* name, const QString& initial) {
    auto* edit = new QLineEdit;
    edit->setObjectName(QString::fromLatin1(name));
    edit->setText(initial);
    edit->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,19}")), edit));
    edit->setMinimumHeight(44);
    return edit;
}
std::int64_t nonnegative(const QLineEdit* edit) {
    bool ok = false;
    const auto n = edit->text().toLongLong(&ok);
    return ok && n >= 0 ? n : kInvalidWideNumber;
}
// Keep the countdown readable for multi-day pauses without integer overflow.
QString readableRemaining(std::int64_t milliseconds, bool ru) {
    const auto seconds = milliseconds / 1000 + (milliseconds % 1000 != 0);
    const auto days = seconds / 86400;
    const auto hours = (seconds / 3600) % 24;
    const auto minutes = (seconds / 60) % 60;
    const auto secs = seconds % 60;
    if (days > 0) return QStringLiteral("%1%2 %3:%4:%5")
        .arg(days).arg(ru ? QStringLiteral(" д") : QStringLiteral(" d"))
        .arg(hours, 2, 10, QChar('0')).arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
    return QStringLiteral("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(secs, 2, 10, QChar('0'));
}
QString predictedLocalTime(std::int64_t ms, bool ru) {
    // Qt calendar formatting isn't meaningful for millennia-away dates; never
    // overflow its date arithmetic on an arbitrary 64-bit wait duration.
    constexpr std::int64_t kCalendarHorizon = 200LL * 366 * 86400000;
    if (ms < 0 || ms > kCalendarHorizon)
        return ru ? QStringLiteral("за пределами календарного диапазона") :
                    QStringLiteral("beyond calendar display range");
    const QDateTime at = QDateTime::currentDateTime().addMSecs(ms);
    if (!at.isValid()) return ru ? QStringLiteral("недоступно") : QStringLiteral("unavailable");
    return at.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss"));
}
} // namespace

InputLabWidget::InputLabWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("inputLab"));
    // GUI smoke tests operate on temporary plans. Never overwrite or restore
    // real user autosave while --smoke-test-* is running.
    for (const QString& arg : QApplication::arguments()) {
        if (arg.startsWith(QStringLiteral("--smoke-test"))) autosaveEnabled_ = false;
    }
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 8);
    layout->setSpacing(12);
    help_ = new QLabel;
    help_->setWordWrap(true);
    help_->setObjectName(QStringLiteral("sectionHint"));
    layout->addWidget(help_);
    record_ = new QPushButton;
    record_->setObjectName(QStringLiteral("inputLabRecord"));
    record_->setMinimumHeight(42);
    auto* recorderButtons = new QHBoxLayout;
    recorderButtons->addWidget(record_, 2);
    recoverRecording_ = new QPushButton;
    recoverRecording_->setObjectName(QStringLiteral("inputLabRecoverRecording"));
    recoverRecording_->setMinimumHeight(42);
    recoverRecording_->setProperty("actionRole", QStringLiteral("secondary"));
    recorderButtons->addWidget(recoverRecording_);
    recoverRecording_->hide();
    layout->addLayout(recorderButtons);

    table_ = new ActionTableWidget(0, 3);
    // QAbstractScrollArea remaps the table's context-menu signal to viewport
    // coordinates. Connecting both table and viewport used to dispatch a single
    // right-click twice and could mistakenly open the list-wide popup for a row.
    // Consume the raw event once, then map its global position to the viewport.
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->installEventFilter(this);
    table_->viewport()->installEventFilter(this);
    // QHeaderView is a scroll area: native context-menu events can arrive
    // at either the header object or its internal viewport. Handle both
    // explicitly so right-click never depends on Qt's forwarding path.
    table_->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->horizontalHeader()->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->horizontalHeader()->installEventFilter(this);
    table_->horizontalHeader()->viewport()->installEventFilter(this);
    table_->horizontalHeader()->setSectionsClickable(true);
    table_->setDragEnabled(true);
    table_->setAcceptDrops(true);
    table_->setDropIndicatorShown(true);
    table_->setDragDropMode(QAbstractItemView::InternalMove);
    table_->setDefaultDropAction(Qt::MoveAction);
    table_->setDragDropOverwriteMode(false);
    table_->setObjectName(QStringLiteral("inputLabActions"));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(2, 46);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setMinimumHeight(180);
    layout->addWidget(table_);

    // One optional foreground-window binding for the shared action list.
    // No Win32 handles are serialized with saved scenarios.
    auto* windowRow = new QHBoxLayout;
    chooseWindow_ = new QPushButton;
    chooseWindow_->setObjectName(QStringLiteral("inputLabChooseWindow"));
    clearWindow_ = new QPushButton;
    clearWindow_->setObjectName(QStringLiteral("inputLabClearWindow"));
    windowStatus_ = new QLabel;
    windowStatus_->setObjectName(QStringLiteral("inputLabWindowStatus"));
    windowStatus_->setWordWrap(true);
    windowRow->addWidget(chooseWindow_);
    windowRow->addWidget(clearWindow_);
    windowRow->addWidget(windowStatus_, 1);
    layout->insertLayout(2, windowRow);

    // The most frequent actions get one-click buttons; editing tools stay optional.
    auto* quickActions = new WrapLayout(nullptr, 7);
    quickClick_ = new QPushButton;
    quickClick_->setObjectName(QStringLiteral("inputLabQuickClick"));
    quickClick_->setProperty("actionRole", QStringLiteral("primary"));
    quickKey_ = new QPushButton;
    quickKey_->setObjectName(QStringLiteral("inputLabQuickKey"));
    quickText_ = new QPushButton;
    quickText_->setObjectName(QStringLiteral("inputLabQuickText"));
    quickWait_ = new QPushButton;
    quickWait_->setObjectName(QStringLiteral("inputLabQuickWait"));
    quickWaitUntil_ = new QPushButton;
    quickWaitUntil_->setObjectName(QStringLiteral("inputLabQuickWaitUntil"));
    quickCode_ = new QPushButton;
    quickCode_->setObjectName(QStringLiteral("inputLabQuickCode"));
    selectAll_ = new QPushButton;
    selectAll_->setObjectName(QStringLiteral("inputLabSelectAll"));
    for (auto* b : {quickClick_, quickKey_, quickText_, quickWait_, quickWaitUntil_, quickCode_, selectAll_}) {
        b->setMinimumHeight(38);
        quickActions->addWidget(b);
    }
    // Keep common actions above the list; users needn't scroll past it to add a step.
    layout->insertLayout(2, quickActions);
    editorGroup_ = new QGroupBox;
    editorGroup_->setObjectName(QStringLiteral("inputLabEditor"));
    auto* form = new QFormLayout(editorGroup_);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setVerticalSpacing(8);
    type_ = new QComboBox;
    type_->setObjectName(QStringLiteral("inputLabType"));
    for (int i = 0; i <= WaitForTime; ++i) type_->addItem(QString(), i);
    typeLabel_ = new QLabel;
    form->addRow(typeLabel_, type_);
    // Window identity belongs to the SELECTED action, not the whole plan.
    // A window is looked up again on each run; no HWND or PID is persisted.
    auto* stepWindowRow = new QHBoxLayout;
    chooseStepWindow_ = new QPushButton;
    chooseStepWindow_->setObjectName(QStringLiteral("inputLabStepChooseWindow"));
    clearStepWindow_ = new QPushButton;
    clearStepWindow_->setObjectName(QStringLiteral("inputLabStepClearWindow"));
    chooseStepWindow_->setMinimumHeight(38);
    clearStepWindow_->setMinimumHeight(38);
    stepWindowStatus_ = new QLabel;
    stepWindowStatus_->setObjectName(QStringLiteral("inputLabStepWindowStatus"));
    stepWindowStatus_->setWordWrap(true);
    stepWindowRow->addWidget(chooseStepWindow_);
    stepWindowRow->addWidget(clearStepWindow_);
    stepWindowRow->addWidget(stepWindowStatus_, 1);
    form->addRow(stepWindowRow);
    stepEnforce_ = new QCheckBox;
    stepEnforce_->setObjectName(QStringLiteral("inputLabStepEnforceWindow"));
    form->addRow(stepEnforce_);
    stepActivate_ = new QCheckBox;
    stepActivate_->setObjectName(QStringLiteral("inputLabStepActivateWindow"));
    form->addRow(stepActivate_);
    stepWindowWait_ = largeIntegerEdit("inputLabStepWindowWaitMs", QStringLiteral("0"));
    stepWindowWait_->setToolTip(QStringLiteral("How long to wait for the selected window, in milliseconds. 0 = no waiting."));
    form->addRow(new QLabel(QStringLiteral("Window timeout (ms)")), stepWindowWait_);
    connect(chooseStepWindow_, &QPushButton::clicked, this, [this] { chooseStepWindow(); });
    connect(clearStepWindow_, &QPushButton::clicked, this, [this] { clearStepWindow(); });
    connect(stepEnforce_, &QCheckBox::toggled, this, [this] { editStepWindow(); });
    connect(stepActivate_, &QCheckBox::toggled, this, [this] { editStepWindow(); });
    connect(stepWindowWait_, &QLineEdit::editingFinished, this, [this] { editStepWindow(); });
    mouseButton_ = new QComboBox;
    for (int i = 0; i < 5; ++i) mouseButton_->addItem(QString(), i);
    mouseLabel_ = new QLabel;
    form->addRow(mouseLabel_, mouseButton_);
    captureMouse_ = new QPushButton;
    captureMouse_->setObjectName(QStringLiteral("inputLabCaptureMouse"));
    form->addRow(captureMouse_);
    keyPreset_ = new QComboBox;
    keyPreset_->addItem(QStringLiteral("Custom VK code"), -1);
    for (int ch = 'A'; ch <= 'Z'; ++ch) keyPreset_->addItem(QString(QChar(ch)), ch);
    for (int ch = '0'; ch <= '9'; ++ch) keyPreset_->addItem(QString(QChar(ch)), ch);
    for (int f = 1; f <= 24; ++f) keyPreset_->addItem(QStringLiteral("F%1").arg(f), 0x70 + f - 1);
    for (const auto& key : std::initializer_list<std::pair<const char*, int>>{
            {"Ctrl",0x11},{"Shift",0x10},{"Alt",0x12},{"Win",0x5B},
            {"Enter",0x0D},{"Space",0x20},{"Tab",0x09},{"Esc",0x1B},
            {"Backspace",0x08},{"Left",0x25},{"Up",0x26},{"Right",0x27},{"Down",0x28},
            {"Home",0x24},{"End",0x23},{"Insert",0x2D},{"Delete",0x2E}})
        keyPreset_->addItem(QString::fromLatin1(key.first), key.second);
    keyPresetLabel_ = new QLabel;
    form->addRow(keyPresetLabel_, keyPreset_);
    virtualKey_ = new QSpinBox;
    virtualKey_->setObjectName(QStringLiteral("inputLabVirtualKey"));
    virtualKey_->setRange(8, 254);
    virtualKey_->setValue(0x41);
    keyLabel_ = new QLabel;
    form->addRow(keyLabel_, virtualKey_);
    captureKey_ = new QPushButton;
    captureKey_->setObjectName(QStringLiteral("inputLabCaptureKey"));
    form->addRow(captureKey_);
    clickCount_ = largeIntegerEdit("inputLabClickCount", QStringLiteral("1"));
    clickCount_->setToolTip(QStringLiteral("Positive click count, up to 64-bit integer maximum. Stop hotkey remains active."));
    clickCountLabel_ = new QLabel;
    form->addRow(clickCountLabel_, clickCount_);
    clickInterval_ = new QSpinBox;
    clickInterval_->setRange(0, 60000);
    clickInterval_->setValue(200);
    clickInterval_->setSuffix(QStringLiteral(" ms"));
    clickIntervalLabel_ = new QLabel;
    form->addRow(clickIntervalLabel_, clickInterval_);
    wheelAxis_ = new QComboBox;
    wheelAxis_->addItems({QStringLiteral("Vertical"), QStringLiteral("Horizontal")});
    wheelAxisLabel_ = new QLabel;
    form->addRow(wheelAxisLabel_, wheelAxis_);
    wheelDelta_ = new QSpinBox;
    wheelDelta_->setRange(-1200, 1200);
    wheelDelta_->setSingleStep(120);
    wheelDelta_->setValue(120);
    wheelDeltaLabel_ = new QLabel;
    form->addRow(wheelDeltaLabel_, wheelDelta_);
    waitExpression_ = new QLineEdit;
    waitExpression_->setObjectName(QStringLiteral("inputLabWaitExpression"));
    waitExpression_->setText(QStringLiteral("500 мс"));
    waitExpression_->setMinimumHeight(44);
    waitExpression_->setClearButtonEnabled(true);
    waitLabel_ = new QLabel;
    form->addRow(waitLabel_, waitExpression_);
    waitSyntaxHint_ = new QLabel;
    waitSyntaxHint_->setObjectName(QStringLiteral("inputLabWaitSyntaxHint"));
    waitSyntaxHint_->setWordWrap(true);
    form->addRow(waitSyntaxHint_);
    waitNextAt_ = new QLabel;
    waitNextAt_->setObjectName(QStringLiteral("inputLabWaitNextAt"));
    waitNextAt_->setWordWrap(true);
    form->addRow(waitNextAt_);
    waitClock_ = new QTimeEdit;
    waitClock_->setObjectName(QStringLiteral("inputLabWaitUntilClock"));
    waitClock_->setDisplayFormat(QStringLiteral("HH:mm:ss"));
    waitClock_->setTime(QTime::currentTime().addSecs(3600));
    waitClock_->setMinimumHeight(44);
    waitClock_->setToolTip(QStringLiteral("Local Windows clock time. If it has already passed today, the next step starts tomorrow."));
    waitClockLabel_ = new QLabel;
    form->addRow(waitClockLabel_, waitClock_);
    waitClockPreview_ = new QLabel;
    waitClockPreview_->setObjectName(QStringLiteral("inputLabWaitUntilPreview"));
    waitClockPreview_->setWordWrap(true);
    form->addRow(waitClockPreview_);
    holdDuration_ = new QSpinBox;
    holdDuration_->setObjectName(QStringLiteral("inputLabHoldMs"));
    holdDuration_->setRange(1, 60000);
    holdDuration_->setValue(500);
    holdDuration_->setSuffix(QStringLiteral(" ms"));
    holdDurationLabel_ = new QLabel;
    form->addRow(holdDurationLabel_, holdDuration_);
    textInput_ = new QPlainTextEdit;
    textInput_->setObjectName(QStringLiteral("inputLabText"));
    textInput_->setPlaceholderText(QStringLiteral("Text to type into the focused application"));
    textInput_->setMaximumHeight(115);
    textInputLabel_ = new QLabel;
    form->addRow(textInputLabel_, textInput_);
    scriptInput_ = new QPlainTextEdit;
    scriptInput_->setObjectName(QStringLiteral("inputLabScript"));
    scriptInput_->setPlaceholderText(QStringLiteral("# PowerShell: local script, run explicitly with the scenario"));
    scriptInput_->setMinimumHeight(130);
    scriptInputLabel_ = new QLabel;
    form->addRow(scriptInputLabel_, scriptInput_);
    moveMode_ = new QComboBox;
    moveMode_->setObjectName(QStringLiteral("inputLabMoveMode"));
    moveMode_->addItems({QString(), QString(), QString()});
    moveModeLabel_ = new QLabel;
    form->addRow(moveModeLabel_, moveMode_);
    moveX_ = new QSpinBox;
    moveX_->setObjectName(QStringLiteral("inputLabMoveX"));
    moveX_->setRange(-32768, 32767);
    moveX_->setValue(100);
    moveXLabel_ = new QLabel;
    form->addRow(moveXLabel_, moveX_);
    moveY_ = new QSpinBox;
    moveY_->setObjectName(QStringLiteral("inputLabMoveY"));
    moveY_->setRange(-32768, 32767);
    moveYLabel_ = new QLabel;
    form->addRow(moveYLabel_, moveY_);
    moveDuration_ = new QSpinBox;
    moveDuration_->setObjectName(QStringLiteral("inputLabMoveDuration"));
    moveDuration_->setRange(0, 60000);
    moveDuration_->setValue(500);
    moveDuration_->setSuffix(QStringLiteral(" ms"));
    moveDurationLabel_ = new QLabel;
    form->addRow(moveDurationLabel_, moveDuration_);
    layout->addWidget(editorGroup_);

    auto* settings = new QGridLayout;
    repetitions_ = new QSpinBox;
    repetitions_->setObjectName(QStringLiteral("inputLabRepetitions"));
    repetitions_->setRange(1, std::numeric_limits<int>::max());
    repetitions_->setValue(1);
    passDelay_ = largeIntegerEdit("inputLabPassDelayMs", QStringLiteral("0"));
    passDelay_->setToolTip(QStringLiteral("Delay between complete passes in milliseconds; 86400000 = 1 day."));
    repetitionsLabel_ = new QLabel;
    passDelayLabel_ = new QLabel;
    settings->addWidget(repetitionsLabel_, 0, 0);
    settings->addWidget(repetitions_, 0, 1);
    settings->addWidget(passDelayLabel_, 1, 0);
    settings->addWidget(passDelay_, 1, 1);
    keepAwake_ = new QCheckBox;
    keepAwake_->setObjectName(QStringLiteral("inputLabKeepAwake"));
    QSettings appSettings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
    keepAwake_->setChecked(appSettings.value(QStringLiteral("keepAwakeSteps"), true).toBool());
    settings->addWidget(keepAwake_, 2, 0, 1, 2);
    connect(keepAwake_, &QCheckBox::toggled, this, [](bool checked) {
        QSettings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"))
            .setValue(QStringLiteral("keepAwakeSteps"), checked);
    });
    layout->addLayout(settings);

    nextStepTime_ = new QLabel;
    nextStepTime_->setObjectName(QStringLiteral("inputLabNextStepTime"));
    nextStepTime_->setWordWrap(true);
    nextStepTime_->setMinimumHeight(38);
    nextStepTime_->setToolTip(QStringLiteral("Countdown and local Windows clock time for the upcoming action, updated during each pause."));
    // Keep the schedule visible above the steps instead of below the large editor.
    layout->insertWidget(2, nextStepTime_);
    nextStepTimer_ = new QTimer(this);
    nextStepTimer_->setInterval(1000);
    connect(nextStepTimer_, &QTimer::timeout, this, [this] { refreshNextStepTime(); });

    savedGroup_ = new QGroupBox;
    savedGroup_->setObjectName(QStringLiteral("inputLabSavedGroup"));
    auto* library = new QVBoxLayout(savedGroup_);
    savedPlans_ = new QComboBox;
    savedPlans_->setObjectName(QStringLiteral("inputLabSavedPlans"));
    savedPlans_->setMinimumHeight(38);
    library->addWidget(savedPlans_);
    importCurrentLegacy_ = new QPushButton;
    importCurrentLegacy_->setObjectName(QStringLiteral("inputLabImportPrevious"));
    library->addWidget(importCurrentLegacy_);
    importCurrentLegacy_->hide();
    auto* libraryActions = new QHBoxLayout;
    savePlan_ = new QPushButton;
    savePlan_->setObjectName(QStringLiteral("inputLabSavePlan"));
    loadPlan_ = new QPushButton;
    loadPlan_->setObjectName(QStringLiteral("inputLabLoadPlan"));
    deletePlan_ = new QPushButton;
    deletePlan_->setObjectName(QStringLiteral("inputLabDeletePlan"));
    for (auto* button : {savePlan_, loadPlan_, deletePlan_}) {
        button->setMinimumHeight(38);
        libraryActions->addWidget(button);
    }
    library->addLayout(libraryActions);
    savedToggle_ = new QPushButton;
    savedToggle_->setCheckable(true);
    savedToggle_->setObjectName(QStringLiteral("inputLabSavedToggle"));
    savedToggle_->setProperty("actionRole", QStringLiteral("secondary"));
    layout->addWidget(savedToggle_);
    autosaveStatus_ = new QLabel;
    autosaveStatus_->setObjectName(QStringLiteral("inputLabAutosaveStatus"));
    autosaveStatus_->setWordWrap(true);
    layout->addWidget(autosaveStatus_);
    autosaveTimer_ = new QTimer(this);
    autosaveTimer_->setSingleShot(true);
    autosaveTimer_->setInterval(450);
    connect(autosaveTimer_, &QTimer::timeout, this, [this] { writeAutosave(); });
    savedGroup_->hide();
    layout->addWidget(savedGroup_);

    auto* bottom = new QHBoxLayout;
    run_ = new QPushButton;
    run_->setObjectName(QStringLiteral("inputLabRun"));
    run_->setMinimumHeight(46);
    run_->setProperty("actionRole", QStringLiteral("primary"));
    stop_ = new QPushButton;
    stop_->setObjectName(QStringLiteral("inputLabStop"));
    stop_->setProperty("actionRole", QStringLiteral("danger"));
    record_->setProperty("actionRole", QStringLiteral("record"));
    savePlan_->setProperty("actionRole", QStringLiteral("success"));
    deletePlan_->setProperty("actionRole", QStringLiteral("danger"));
    stop_->setMinimumHeight(46);
    bottom->addWidget(run_, 1);
    bottom->addWidget(stop_, 1);
    layout->addLayout(bottom);
    // MainWindow relocates the existing Run/Stop widgets to its shared toolbar.
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("inputLabStatus"));
    status_->setWordWrap(true);
    layout->addWidget(status_);
    status_->hide(); // Status history is displayed in the shared IDE-style log dock.

    connect(chooseWindow_, &QPushButton::clicked, this, [this] {
        if (isRunning()) return;
        const auto selected = chooseTargetWindow(this, russian_ ? QStringLiteral("ru") : QStringLiteral("en"));
        if (!selected) return;
        if (targetWindow_ && (targetWindow_->handle != selected->handle ||
                              targetWindow_->processId != selected->processId)) {
            const bool clientSteps = std::any_of(actions_.begin(), actions_.end(), [](const Action& action) {
                const auto* move = std::get_if<MovePointer>(&action);
                return move && move->mode == MoveMode::WindowClient;
            });
            if (clientSteps && QMessageBox::question(this, QStringLiteral("ClickNova"),
                    trText("Changing the window changes where client coordinates point. Recheck every window-relative step before running. Continue?",
                           "Другое окно изменит положение относительных координат. Проверьте все шаги с координатами окна перед запуском. Продолжить?")) != QMessageBox::Yes)
                return;
        }
        targetWindow_ = selected;
        requireWindowBinding_ = true;
        windowStatus_->setText(QString::fromStdWString(selected->title).simplified());
        reportStatus(trText("Target selected. Activate it before playback; background clicks are not supported.",
                                "Окно выбрано. Активируйте его перед запуском; фоновые клики не поддерживаются."));
        updateButtons();
        scheduleAutosave();
    });
    connect(clearWindow_, &QPushButton::clicked, this, [this] {
        if (isRunning()) return;
        targetWindow_.reset();
        requireWindowBinding_ = false;
        windowStatus_->setText(trText("Any foreground application", "Любое активное приложение"));
        reportStatus(trText("Window restriction cleared. Client-coordinate steps still require a window.",
                                "Ограничение окна снято. Для шагов с координатами окна необходимо выбрать цель."));
        updateButtons();
        scheduleAutosave();
    });
    connect(record_, &QPushButton::clicked, this, [this] {
        if (recorder_) recorder_->bringToFront();
        else beginRecording();
    });
    QTimer::singleShot(0, this, [this] { refreshRecoveryAvailability(); });
    connect(recoverRecording_, &QPushButton::clicked, this, [this] { recoverRecording(); });
    connect(quickClick_, &QPushButton::clicked, this, [this] { addActionOfType(MouseClick); });
    connect(quickKey_, &QPushButton::clicked, this, [this] { addActionOfType(KeyTap); });
    connect(quickText_, &QPushButton::clicked, this, [this] { addActionOfType(TypeText); });
    connect(quickWait_, &QPushButton::clicked, this, [this] { addActionOfType(Wait); });
    connect(quickWaitUntil_, &QPushButton::clicked, this, [this] { addActionOfType(WaitForTime); });
    connect(quickCode_, &QPushButton::clicked, this, [this] { addActionOfType(PowerShell); });
    connect(selectAll_, &QPushButton::clicked, table_, &QTableWidget::selectAll);
    connect(savedToggle_, &QPushButton::toggled, savedGroup_, &QWidget::setVisible);
    table_->requestMove = [this](int from, int to) { moveActionRow(from, to); };
    table_->requestMoveRows = [this](const std::vector<int>& rows, int insertion) {
        moveSelectedRowsTo(rows, insertion);
    };
    // All mouse context menus (table, viewport, row button and header) are
    // dispatched by eventFilter(). Do not connect both scroll area signals:
    // Qt may emit their customContextMenuRequested in different coordinate
    // systems for the same physical click.
    // The header's ellipsis opens the list-wide menu even with no empty rows.
    connect(table_->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section != 2) return;
        const QPoint anchor(table_->horizontalHeader()->sectionViewportPosition(2),
                            table_->horizontalHeader()->height());
        showActionMenu(-1, table_->horizontalHeader()->mapToGlobal(anchor));
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (isRunning()) return;
        table_->setCurrentCell(row, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        type_->setFocus(Qt::OtherFocusReason);
    });
    auto shortcut = [this](const QKeySequence& key, auto fn) {
        auto* item = new QShortcut(key, table_);
        item->setContext(Qt::WidgetWithChildrenShortcut);
        connect(item, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence(QKeySequence::SelectAll), [this] { table_->selectAll(); });
    shortcut(QKeySequence(Qt::Key_Delete), [this] { deleteSelectedRows(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_D), [this] { duplicateSelectedRows(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Up), [this] { moveSelectedRows(-1); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Down), [this] { moveSelectedRows(1); });
    shortcut(QKeySequence(QKeySequence::Copy), [this] { copySelectedRows(); });
    shortcut(QKeySequence(QKeySequence::Paste), [this] { pasteActionAt(table_->currentRow() < 0 ? static_cast<int>(actions_.size()) : table_->currentRow() + 1); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), [this] {
        pasteActionAt(table_->currentRow() < 0 ? static_cast<int>(actions_.size()) : table_->currentRow());
    });
    shortcut(QKeySequence(QKeySequence::Undo), [this] { undoActionEdit(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { redoActionEdit(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this] { redoActionEdit(); });
    // Keyboard-only access to the same row/list context menu. Qt does not
    // consistently forward the native Menu key to CustomContextMenu views.
    const auto keyboardContextMenu = [this] {
        if (isRunning()) return;
        const int row = table_->currentRow();
        QWidget* anchor = row >= 0 && row < table_->rowCount()
            ? table_->cellWidget(row, 2) : nullptr;
        const QPoint globalPos = anchor
            ? anchor->mapToGlobal(QPoint(0, anchor->height()))
            : table_->viewport()->mapToGlobal(table_->viewport()->rect().center());
        showActionMenu(row, globalPos);
    };
    shortcut(QKeySequence(Qt::SHIFT | Qt::Key_F10), keyboardContextMenu);
    shortcut(QKeySequence(Qt::Key_Menu), keyboardContextMenu);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
        selectAction(table_->currentRow());
    });
    connect(type_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateFields(); updateSelected(); });
    connect(mouseButton_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateSelected(); });
    connect(captureMouse_, &QPushButton::clicked, this, [this] {
        if (isRunning()) return;
        if (auto b = chooseMouseButtonByPress(this, russian_))
            mouseButton_->setCurrentIndex(static_cast<int>(*b));
    });
    connect(captureKey_, &QPushButton::clicked, this, [this] {
        if (isRunning()) return;
        if (auto key = chooseVirtualKeyByPress(this, russian_)) {
            virtualKey_->setValue(*key);
            keyPreset_->setCurrentIndex(std::max(0, keyPreset_->findData(*key)));
        }
    });
    connect(textInput_, &QPlainTextEdit::textChanged, this, [this] { updateSelected(); });
    connect(scriptInput_, &QPlainTextEdit::textChanged, this, [this] { updateSelected(); });
    connect(wheelAxis_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateSelected(); });
    connect(moveMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { updateSelected(); });
    connect(keyPreset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (updating_) return;
        int code = keyPreset_->currentData().toInt();
        if (code >= 8) virtualKey_->setValue(code);
    });
    for (auto* spin : {virtualKey_, clickInterval_, wheelDelta_, holdDuration_, moveX_, moveY_, moveDuration_, repetitions_})
        spin->setMinimumHeight(44);
    for (auto* combo : {type_, mouseButton_, keyPreset_, wheelAxis_, moveMode_}) combo->setMinimumHeight(44);
    for (auto* spin : {virtualKey_, clickInterval_, wheelDelta_, holdDuration_, moveX_, moveY_, moveDuration_})
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { updateSelected(); });
    connect(clickCount_, &QLineEdit::textEdited, this, [this] { updateSelected(); });
    connect(waitExpression_, &QLineEdit::textChanged, this, [this] {
        updateSelected();
        refreshWaitPreview();
    });
    connect(waitClock_, &QTimeEdit::timeChanged, this, [this] {
        updateSelected(); refreshWaitPreview();
    });
    connect(repetitions_, QOverload<int>::of(&QSpinBox::valueChanged), this,
        [this] { scheduleAutosave(); });
    connect(passDelay_, &QLineEdit::textEdited, this, [this] { scheduleAutosave(); });
    connect(savePlan_, &QPushButton::clicked, this, [this] { savePlan(); });
    connect(importCurrentLegacy_, &QPushButton::clicked, this, [this] {
        if (!isRunning() && legacyImporter_) legacyImporter_();
    });
    connect(loadPlan_, &QPushButton::clicked, this, [this] { loadPlan(); });
    connect(deletePlan_, &QPushButton::clicked, this, [this] { deletePlan(); });
    connect(savedPlans_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        [this](int) { updateButtons(); });
    connect(run_, &QPushButton::clicked, this, [this] { beginPlayback(); });
    connect(stop_, &QPushButton::clicked, this, [this] { stopPlayback(); });
    // Unified pictograms for the compact actions and the row context menu.
    using clicknova::gui::Icon;
    using clicknova::gui::setButtonIcon;
    setButtonIcon(record_, Icon::Record, QColor(QStringLiteral("#ffffff")));
    setButtonIcon(chooseWindow_, Icon::Window);
    setButtonIcon(clearWindow_, Icon::Close);
    setButtonIcon(chooseStepWindow_, Icon::Window);
    setButtonIcon(clearStepWindow_, Icon::Close);
    setButtonIcon(quickClick_, Icon::Click, QColor(QStringLiteral("#ffffff")));
    setButtonIcon(quickKey_, Icon::Keyboard);
    setButtonIcon(quickText_, Icon::Text);
    setButtonIcon(quickWait_, Icon::Wait);
    setButtonIcon(quickWaitUntil_, Icon::Wait);
    setButtonIcon(quickCode_, Icon::Text);
    setButtonIcon(selectAll_, Icon::Copy);
    setButtonIcon(captureMouse_, Icon::Mouse);
    setButtonIcon(captureKey_, Icon::Keyboard);
    setButtonIcon(savedToggle_, Icon::Folder);
    setButtonIcon(savePlan_, Icon::Save, QColor(QStringLiteral("#d8fff1")));
    setButtonIcon(loadPlan_, Icon::Folder);
    setButtonIcon(deletePlan_, Icon::Delete, QColor(QStringLiteral("#ffcbd2")));
    setButtonIcon(run_, Icon::Play, QColor(QStringLiteral("#ffffff")));
    setButtonIcon(stop_, Icon::Stop, QColor(QStringLiteral("#ffcbd2")));
    setRussian(false);
    addAction();
    actionHistory_.clear(); // The initial template step is not a user edit.
    refreshSavedPlans();
    restoreAutosave();
}

InputLabWidget::~InputLabWidget() {
    if (autosaveTimer_ && autosaveTimer_->isActive()) writeAutosave();
    if (recorder_) { delete recorder_; recorder_ = nullptr; }
    worker_.request_stop();
    if (worker_.joinable()) worker_.join();
    // The owner unregisters the native hotkey before QWidget children are destroyed.
    // Avoid callbacks into the already-destroyed main-window hotkey controller.
}

void InputLabWidget::setStartShortcut(std::function<QString()> label) {
    startHotkeyLabel_ = std::move(label);
    updateStartLabel();
}

void InputLabWidget::setLegacyImporter(std::function<void()> importer) {
    legacyImporter_ = std::move(importer);
    // Compatibility importer is retained internally, not as an extra old-steps UI.
    importCurrentLegacy_->hide();
}

void InputLabWidget::refreshLibrary() { refreshSavedPlans(); }

void InputLabWidget::updateStartLabel() {
    const QString label = startHotkeyLabel_ ? startHotkeyLabel_() : QString();
    run_->setText(trText("Start in 3 seconds", "Старт через 3 секунды") +
                  (label.isEmpty() ? QString() : QStringLiteral(" · ") + label));
}

void InputLabWidget::setStopHotkeyController(std::function<bool(bool)> controller,
                                             std::function<QString()> label) {
    stopHotkeyController_ = std::move(controller);
    stopHotkeyLabel_ = std::move(label);
}

QString InputLabWidget::trText(const char* en, const char* ru) const {
    return QString::fromUtf8(russian_ ? ru : en);
}

void InputLabWidget::setRussian(bool enabled) {
    russian_ = enabled;
    help_->setText(trText(
        "Record, edit, multi-select and reorder actions or add local PowerShell code. Drafts are autosaved. Gamepads are not supported.",
        "Записывайте, редактируйте и выделяйте несколько шагов; добавляйте локальный код PowerShell. Черновик сохраняется автоматически."));
    chooseStepWindow_->setText(trText("Choose step window", "Окно шага"));
    clearStepWindow_->setText(trText("Clear", "Сброс"));
    stepEnforce_->setText(trText("Require this window for playback (recorded window is informational by default)",
                                    "Требовать это окно при запуске (после записи — только информация)"));
    stepActivate_->setText(trText("Activate this window before the step", "Активировать окно перед шагом"));
    chooseWindow_->setText(trText("Choose window…", "Выбрать окно…"));
    clearWindow_->setText(trText("Clear", "Сбросить"));
    chooseWindow_->setToolTip(trText("Restrict ALL actions to this foreground window. The window must be selected again after restarting; it is never saved as a system handle.",
        "Ограничить ВСЕ действия выбранным активным окном. После перезапуска его нужно выбрать заново; системный идентификатор не сохраняется."));
    clearWindow_->setToolTip(trText("Remove the window restriction. Window-client coordinates cannot run until another window is selected.",
        "Снять ограничение окна. Шаги с координатами окна не запустятся до нового выбора цели."));
    windowStatus_->setText(targetWindow_ ? QString::fromStdWString(targetWindow_->title).simplified() :
        requireWindowBinding_ ? trText("Select the required foreground window", "Выберите необходимое активное окно") :
                                trText("Any foreground application", "Любое активное приложение"));
    record_->setText(trText("Record actions", "Записать действия"));
    recoverRecording_->setText(trText("Recover recording", "Восстановить запись"));
    recoverRecording_->setToolTip(trText("Browse timestamped local recovery copies of unfinished recordings.",
        "Выбрать запись из локального автосохранения по дате и времени."));
    record_->setToolTip(trText("Opens a movable, always-visible control panel. Record mouse activity, optionally keyboard. Pause, resume, stop or discard the recording. The panel is not recorded.",
        "Открывает перемещаемую панель записи. Записывает мышь и, по отдельному согласию, клавиатуру. Пауза, продолжение, стоп и отмена. Сама панель в запись не входит."));
    quickClick_->setText(trText("Click", "Клик"));
    quickKey_->setText(trText("Key", "Клавиша"));
    quickText_->setText(trText("Text", "Текст"));
    quickWait_->setText(trText("Wait", "Ожидание"));
    quickWaitUntil_->setText(trText("Until time", "До времени"));
    quickWaitUntil_->setToolTip(trText(
        "Add a clock-based wait action. The NEXT step runs at the specified local Windows time; if it has passed, tomorrow.",
        "Добавить шаг ожидания до времени по часам Windows. СЛЕДУЮЩИЙ шаг начнётся в указанное время; если оно прошло — завтра."));
    quickCode_->setText(trText("Code", "Код"));
    selectAll_->setText(trText("Select all", "Выделить всё"));
    selectAll_->setToolTip(trText("Select all steps (Ctrl+A); use Delete, Ctrl+D or right-click for group actions.",
        "Выделить все шаги (Ctrl+A). Delete, Ctrl+D или ПКМ — групповые действия."));
    quickCode_->setToolTip(trText("Add a local PowerShell code step. It runs only when you press Start, after confirmation.",
        "Добавить локальный шаг PowerShell. Выполняется только после команды Старт и подтверждения."));
    savedToggle_->setText(trText("Named scenarios…", "Именованные сценарии…"));
    autosaveStatus_->setText(trText("Autosave: active · changes are saved locally",
        "Автосохранение: включено · изменения сохраняются локально"));
    scriptInputLabel_->setText(trText("PowerShell code", "Код PowerShell"));
    quickClick_->setToolTip(trText("Add a mouse click. Set its button, count and interval below.",
        "Добавить клик мыши. Ниже настройте кнопку, количество и интервал."));
    quickKey_->setToolTip(trText("Add a keyboard press. Pick a common key or capture one from your keyboard.",
        "Добавить нажатие клавиши. Выберите из списка или нажмите клавишу на устройстве."));
    quickText_->setToolTip(trText("Add text for the foreground application. Do not store passwords in scenarios.",
        "Добавить текст для активного приложения. Не сохраняйте пароли в сценариях."));
    quickWait_->setToolTip(trText("Add a pause between actions.", "Добавить паузу между действиями."));
    savedToggle_->setToolTip(trText("Manually named scenarios. Your current draft is saved automatically.",
        "Именованные сценарии. Текущий черновик сохраняется автоматически."));
    importCurrentLegacy_->setText(trText("Import previous unsaved clicks into this list…",
        "Перенести предыдущие несохранённые клики в этот список…"));
    table_->setHorizontalHeaderLabels({trText("Action", "Действие"),
                                       trText("Details", "Параметры"), QStringLiteral("⋯")});
    table_->setToolTip(trText("Ctrl+A selects all, Ctrl/Shift selects multiple, Delete removes, Ctrl+D duplicates, drag reorders. Right-click for operations.",
        "Ctrl+A — выделить всё, Ctrl/Shift — несколько строк, Delete — удалить, Ctrl+D — дублировать, перетаскивание — изменить порядок."));
    editorGroup_->setTitle(trText("Selected action — changes are immediate", "Выбранное действие — изменения применяются сразу"));
    typeLabel_->setText(trText("Action type", "Тип действия"));
    mouseLabel_->setText(trText("Mouse button", "Кнопка мыши"));
    keyPresetLabel_->setText(trText("Common keys", "Частые клавиши"));
    keyLabel_->setText(trText("Windows key code (VK)", "Код клавиши Windows (VK)"));
    clickCountLabel_->setText(trText("Clicks", "Нажатий"));
    clickIntervalLabel_->setText(trText("Between clicks", "Между кликами"));
    wheelAxisLabel_->setText(trText("Wheel direction", "Направление прокрутки"));
    wheelDeltaLabel_->setText(trText("Wheel amount (+ / -)", "Прокрутка (+ / -)"));
    waitLabel_->setText(trText("Wait duration", "Длительность ожидания"));
    waitSyntaxHint_->setText(trText(
        "Combine values: 2h 15min 8s 250ms, 1d 3h or 01:02:03.250. Plain number = milliseconds. Weeks (w) also work.",
        "Смешивайте единицы: 2ч 15мин 8с 250мс, 1д 3ч или 01:02:03.250. Число без единицы = миллисекунды. Также доступны недели (нед)."));
    waitExpression_->setPlaceholderText(trText("e.g. 1h 20min 5s 100ms", "Например: 1ч 20мин 5с 100мс"));
    waitExpression_->setToolTip(trText("Enter a sum of nonnegative durations using w/d/h/min/s/ms, including Russian abbreviations, or HH:MM:SS.mmm. Maximum is signed 64-bit milliseconds.",
        "Введите сумму неотрицательных длительностей: нед/д/ч/мин/с/мс или ЧЧ:ММ:СС.мс. Максимум — 64-битные миллисекунды."));
    waitClockLabel_->setText(trText("Continue at (local clock)", "Продолжить в (по часам Windows)"));
    refreshWaitPreview();
    refreshNextStepTime();
    holdDurationLabel_->setText(trText("Hold duration", "Время удержания"));
    moveModeLabel_->setText(trText("Coordinates", "Координаты"));
    moveXLabel_->setText(trText("X / offset X", "X / смещение X"));
    moveYLabel_->setText(trText("Y / offset Y", "Y / смещение Y"));
    moveDurationLabel_->setText(trText("Move over (0 = instant)", "Время движения (0 = сразу)"));
    moveMode_->setItemText(0, trText("Relative to cursor", "Относительно курсора"));
    moveMode_->setItemText(1, trText("Screen coordinates", "Координаты экрана"));
    moveMode_->setItemText(2, trText("Selected window (client X/Y)", "Выбранное окно (X/Y в области окна)"));
    moveMode_->setToolTip(trText("Relative: offsets from the cursor. Screen: virtual desktop pixels. Selected window: pixels inside its client area; choose and focus the window before playback.",
        "Относительно: смещение от курсора. Экран: пиксели рабочего стола. Выбранное окно: координаты в рабочей области окна; перед запуском выберите и активируйте его."));
    textInputLabel_->setText(trText("Text to type", "Текст для ввода"));
    captureMouse_->setText(trText("Choose by clicking here...", "Выбрать нажатием мыши..."));
    captureKey_->setText(trText("Choose by pressing a key...", "Выбрать нажатием клавиши..."));
    textInput_->setToolTip(trText("Type words, sentences or multiline text. Stored in the saved plan; do not enter passwords. Unicode input may not work in games or privileged windows.",
        "Слова, предложения или многострочный текст. Сохраняется в сценарии — не вводите пароли. Unicode-ввод может не работать в играх или привилегированных окнах."));
    repetitionsLabel_->setText(trText("Repeat complete plan", "Повторить весь сценарий"));
    passDelayLabel_->setText(trText("Delay between passes (ms)", "Пауза между проходами (мс)"));
    keepAwake_->setText(trText("Keep Windows and screen awake during playback/waits",
        "Не давать Windows и экрану засыпать во время выполнения и ожидания"));
    keepAwake_->setToolTip(trText("Prevents automatic sleep and screen timeout while running. Does not block manual sleep, shutdown or closing a laptop lid.",
        "Предотвращает автоматический сон и отключение экрана во время сценария. Не отменяет ручной сон, выключение или закрытие крышки ноутбука."));
    savedGroup_->setTitle(trText("Saved sequences", "Сохранённые сценарии"));
    savePlan_->setText(trText("Save as…", "Сохранить как…"));
    loadPlan_->setText(trText("Load", "Загрузить"));
    deletePlan_->setText(trText("Delete", "Удалить"));
    savedPlans_->setToolTip(trText("Choose a named scenario. Your current draft has an independent autosave.",
        "Выберите именованный сценарий. Текущий черновик сохраняется отдельно автоматически."));
    scriptInput_->setToolTip(trText("Executes locally as the current Windows user. Not restricted to the selected window. Confirm before running; Stop terminates PowerShell.",
        "Код выполняется локально с правами пользователя Windows. Привязка к окну не ограничивает скрипт. Перед запуском запрашивается подтверждение."));
    updateStartLabel();
    stop_->setText(trText("Stop", "Стоп"));
    type_->setItemText(MouseClick, trText("Mouse click", "Клик мыши"));
    type_->setItemText(KeyTap, trText("Key press", "Нажать клавишу"));
    type_->setItemText(KeyDown, trText("Key down until key up", "Зажать клавишу до команды отпустить"));
    type_->setItemText(KeyUp, trText("Release key", "Отпустить клавишу"));
    type_->setItemText(MouseDown, trText("Mouse down until mouse up", "Зажать кнопку до команды отпустить"));
    type_->setItemText(MouseUp, trText("Release mouse button", "Отпустить кнопку мыши"));
    type_->setItemText(Scroll, trText("Scroll wheel", "Прокрутка колеса"));
    type_->setItemText(Wait, trText("Wait", "Ожидание"));
    type_->setItemText(MouseHoldTimed, trText("Hold mouse for a duration", "Зажать мышь на время"));
    type_->setItemText(KeyHoldTimed, trText("Hold key for a duration", "Зажать клавишу на время"));
    type_->setItemText(TypeText, trText("Type text", "Ввести текст"));
    type_->setItemText(PointerMove, trText("Move mouse cursor", "Переместить курсор"));
    type_->setItemText(PowerShell, trText("Local PowerShell script", "Локальный скрипт PowerShell"));
    type_->setItemText(WaitForTime, trText("Wait until clock time", "Ждать до времени"));
    type_->setToolTip(trText("Add Mouse down, Move cursor, Mouse up to drag. Each down must have a matching up in the same plan; Stop releases held inputs.",
        "Для перетаскивания: Зажать кнопку → Переместить курсор → Отпустить кнопку. Все зажатия нужно завершить в сценарии; Стоп освобождает кнопки."));
    const char* buttonsEn[] = {"Left", "Right", "Middle", "X1 (side)", "X2 (side)"};
    const char* buttonsRu[] = {"Левая", "Правая", "Средняя", "X1 (боковая)", "X2 (боковая)"};
    for (int i = 0; i < 5; ++i) mouseButton_->setItemText(i, trText(buttonsEn[i], buttonsRu[i]));
    wheelAxis_->setItemText(0, trText("Vertical", "Вертикально"));
    wheelAxis_->setItemText(1, trText("Horizontal", "Горизонтально"));
    table_->setHorizontalHeaderLabels({trText("Action", "Действие"),
                                       trText("Parameters", "Параметры"), QStringLiteral("⋯")});
    table_->horizontalHeaderItem(2)->setToolTip(trText(
        "All actions: add, paste, undo, redo or clear the list.",
        "Весь список: добавить, вставить, отменить, повторить или очистить."));
    run_->setToolTip(trText("Start this unified scenario using the button or configured Start hotkey. Playback waits 3 seconds and targets only the foreground window. Use Stop to cancel.",
        "Сворачивает ClickNova, ждёт 3 секунды и отправляет ввод в активное окно. Для отмены используйте назначенную клавишу Стоп."));
    stop_->setToolTip(trText("Cancel playback. Held buttons and keys are released.",
        "Остановить сценарий. Зажатые кнопки и клавиши освобождаются."));
    reportStatus(trText("Ready. Add and edit actions, then press Run.",
        "Готово. Добавьте действия, настройте их и нажмите Запуск."));
    refreshSavedPlans();
    updateFields();
    refreshTable(table_->currentRow());
}

bool InputLabWidget::isRunning() const noexcept { return running_ || countdown_ || recording_; }
void InputLabWidget::cancel() { if (recorder_) recorder_->finishForReview(); else stopPlayback(); }

std::int64_t InputLabWidget::waitMilliseconds() const {
    const auto parsed = clicknova::duration::parse(waitExpression_->text().toStdU16String());
    return parsed.value_or(kInvalidWideNumber);
}

std::int64_t InputLabWidget::passMilliseconds() const { return nonnegative(passDelay_); }

bool InputLabWidget::numericInputsValid() const {
    const auto kind = type_->currentIndex();
    if (kind == MouseClick && nonnegative(clickCount_) < 1) return false;
    if (kind == Wait && waitMilliseconds() < 0) return false;
    return true;
}

Action InputLabWidget::currentEditorAction() const {
    auto button = static_cast<Button>(mouseButton_->currentIndex());
    int key = virtualKey_->value();
    switch (type_->currentIndex()) {
    case KeyTap: return clicknova::input::KeyTap{key};
    case KeyDown: return clicknova::input::KeyDown{key};
    case KeyUp: return clicknova::input::KeyUp{key};
    case MouseDown: return clicknova::input::ButtonDown{button};
    case MouseUp: return clicknova::input::ButtonUp{button};
    case Scroll: return Wheel{static_cast<Axis>(wheelAxis_->currentIndex()), wheelDelta_->value()};
    case Wait: return clicknova::input::Wait{waitMilliseconds()};
    case WaitForTime: {
        const QTime t = waitClock_->time();
        return clicknova::input::WaitUntil{t.hour(), t.minute(), t.second()};
    }
    case MouseHoldTimed: return clicknova::input::MouseHold{button, holdDuration_->value()};
    case KeyHoldTimed: return clicknova::input::KeyHold{key, holdDuration_->value()};
    case TypeText: return clicknova::input::TypeText{textInput_->toPlainText().toStdU16String()};
    case PowerShell: return clicknova::input::RunScript{scriptInput_->toPlainText().toStdU16String()};
    case PointerMove: return clicknova::input::MovePointer{
        static_cast<MoveMode>(moveMode_->currentIndex()), moveX_->value(), moveY_->value(), moveDuration_->value()};
    default: return Click{button, nonnegative(clickCount_), clickInterval_->value()};
    }
}

void InputLabWidget::loadEditor(const Action& action) {
    updating_ = true;
    int actionType = MouseClick;
    if (std::holds_alternative<clicknova::input::KeyTap>(action)) actionType = KeyTap;
    else if (std::holds_alternative<clicknova::input::KeyDown>(action)) actionType = KeyDown;
    else if (std::holds_alternative<clicknova::input::KeyUp>(action)) actionType = KeyUp;
    else if (std::holds_alternative<clicknova::input::ButtonDown>(action)) actionType = MouseDown;
    else if (std::holds_alternative<clicknova::input::ButtonUp>(action)) actionType = MouseUp;
    else if (std::holds_alternative<clicknova::input::Wheel>(action)) actionType = Scroll;
    else if (std::holds_alternative<clicknova::input::Wait>(action)) actionType = Wait;
    else if (std::holds_alternative<clicknova::input::WaitUntil>(action)) actionType = WaitForTime;
    else if (std::holds_alternative<clicknova::input::MouseHold>(action)) actionType = MouseHoldTimed;
    else if (std::holds_alternative<clicknova::input::KeyHold>(action)) actionType = KeyHoldTimed;
    else if (std::holds_alternative<clicknova::input::TypeText>(action)) actionType = TypeText;
    else if (std::holds_alternative<clicknova::input::MovePointer>(action)) actionType = PointerMove;
    else if (std::holds_alternative<clicknova::input::RunScript>(action)) actionType = PowerShell;
    type_->setCurrentIndex(actionType);
    std::visit([this](const auto& a) {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::is_same_v<T, Click>) {
            mouseButton_->setCurrentIndex(static_cast<int>(a.button));
            clickCount_->setText(QString::number(a.count));
            clickInterval_->setValue(a.intervalMs);
        } else if constexpr (std::is_same_v<T, ButtonDown> || std::is_same_v<T, ButtonUp>) {
            mouseButton_->setCurrentIndex(static_cast<int>(a.button));
        } else if constexpr (std::is_same_v<T, clicknova::input::KeyTap> || std::is_same_v<T, clicknova::input::KeyDown> || std::is_same_v<T, clicknova::input::KeyUp>) {
            virtualKey_->setValue(a.virtualKey);
            int preset = keyPreset_->findData(a.virtualKey);
            keyPreset_->setCurrentIndex(preset >= 0 ? preset : 0);
        } else if constexpr (std::is_same_v<T, clicknova::input::MouseHold>) {
            mouseButton_->setCurrentIndex(static_cast<int>(a.button));
            holdDuration_->setValue(a.durationMs);
        } else if constexpr (std::is_same_v<T, clicknova::input::KeyHold>) {
            virtualKey_->setValue(a.virtualKey);
            holdDuration_->setValue(a.durationMs);
            int preset = keyPreset_->findData(a.virtualKey);
            keyPreset_->setCurrentIndex(preset >= 0 ? preset : 0);
        } else if constexpr (std::is_same_v<T, clicknova::input::MovePointer>) {
            moveMode_->setCurrentIndex(static_cast<int>(a.mode));
            moveX_->setValue(a.x);
            moveY_->setValue(a.y);
            moveDuration_->setValue(a.durationMs);
        } else if constexpr (std::is_same_v<T, clicknova::input::RunScript>) {
            scriptInput_->setPlainText(QString::fromStdU16String(a.code));
        } else if constexpr (std::is_same_v<T, clicknova::input::TypeText>) {
            textInput_->setPlainText(QString::fromStdU16String(a.text));
        } else if constexpr (std::is_same_v<T, Wheel>) {
            wheelAxis_->setCurrentIndex(static_cast<int>(a.axis));
            wheelDelta_->setValue(a.delta);
        } else if constexpr (std::is_same_v<T, clicknova::input::Wait>) {
            const QSignalBlocker blocker(waitExpression_);
            waitExpression_->setText(QString::fromStdU16String(
                clicknova::duration::format(a.durationMs, russian_)));
        } else if constexpr (std::is_same_v<T, clicknova::input::WaitUntil>) {
            waitClock_->setTime(QTime(a.hour, a.minute, a.second));
        }
    }, action);
    updating_ = false;
    updateFields();
}

void InputLabWidget::updateFields() {
    int type = type_->currentIndex();
    bool button = type == MouseClick || type == MouseDown || type == MouseUp || type == MouseHoldTimed;
    bool key = type == KeyTap || type == KeyDown || type == KeyUp || type == KeyHoldTimed;
    bool click = type == MouseClick;
    bool scroll = type == Scroll;
    bool wait = type == Wait;
    for (QWidget* widget : std::initializer_list<QWidget*>{mouseLabel_, mouseButton_, captureMouse_}) widget->setVisible(button);
    for (QWidget* widget : std::initializer_list<QWidget*>{keyPresetLabel_, keyPreset_, keyLabel_, virtualKey_, captureKey_}) widget->setVisible(key);
    for (QWidget* widget : std::initializer_list<QWidget*>{clickCountLabel_, clickCount_, clickIntervalLabel_, clickInterval_}) widget->setVisible(click);
    for (QWidget* widget : std::initializer_list<QWidget*>{wheelAxisLabel_, wheelAxis_, wheelDeltaLabel_, wheelDelta_}) widget->setVisible(scroll);
    for (QWidget* widget : std::initializer_list<QWidget*>{waitLabel_, waitExpression_, waitSyntaxHint_, waitNextAt_}) widget->setVisible(wait);
    for (QWidget* widget : std::initializer_list<QWidget*>{waitClockLabel_, waitClock_, waitClockPreview_})
        widget->setVisible(type == WaitForTime);
    refreshWaitPreview();
    for (QWidget* widget : std::initializer_list<QWidget*>{holdDurationLabel_, holdDuration_}) widget->setVisible(type == MouseHoldTimed || type == KeyHoldTimed);
    for (QWidget* widget : std::initializer_list<QWidget*>{textInputLabel_, textInput_}) widget->setVisible(type == TypeText);
    for (QWidget* widget : std::initializer_list<QWidget*>{scriptInputLabel_, scriptInput_}) widget->setVisible(type == PowerShell);
    for (QWidget* widget : std::initializer_list<QWidget*>{moveModeLabel_, moveMode_, moveXLabel_, moveX_,
           moveYLabel_, moveY_, moveDurationLabel_, moveDuration_}) widget->setVisible(type == PointerMove);
}

QString InputLabWidget::actionName(const Action& action) const {
    if (std::holds_alternative<Click>(action)) return type_->itemText(MouseClick);
    if (std::holds_alternative<clicknova::input::KeyTap>(action)) return type_->itemText(KeyTap);
    if (std::holds_alternative<clicknova::input::KeyDown>(action)) return type_->itemText(KeyDown);
    if (std::holds_alternative<clicknova::input::KeyUp>(action)) return type_->itemText(KeyUp);
    if (std::holds_alternative<clicknova::input::ButtonDown>(action)) return type_->itemText(MouseDown);
    if (std::holds_alternative<clicknova::input::ButtonUp>(action)) return type_->itemText(MouseUp);
    if (std::holds_alternative<clicknova::input::MouseHold>(action)) return type_->itemText(MouseHoldTimed);
    if (std::holds_alternative<clicknova::input::KeyHold>(action)) return type_->itemText(KeyHoldTimed);
    if (std::holds_alternative<clicknova::input::TypeText>(action)) return type_->itemText(TypeText);
    if (std::holds_alternative<clicknova::input::MovePointer>(action)) return type_->itemText(PointerMove);
    if (std::holds_alternative<clicknova::input::RunScript>(action)) return type_->itemText(PowerShell);
    if (std::holds_alternative<clicknova::input::WaitUntil>(action)) return type_->itemText(WaitForTime);
    if (std::holds_alternative<clicknova::input::Wheel>(action)) return type_->itemText(Scroll);
    return type_->itemText(Wait);
}

QString InputLabWidget::actionDetails(const Action& action) const {
    return std::visit([this](const auto& a) -> QString {
        using T = std::decay_t<decltype(a)>;
        if constexpr (std::is_same_v<T, Click>)
            return QStringLiteral("%1 × %2 · %3 ms").arg(mouseButton_->itemText(static_cast<int>(a.button)))
                .arg(a.count).arg(a.intervalMs);
        else if constexpr (std::is_same_v<T, ButtonDown> || std::is_same_v<T, ButtonUp>)
            return mouseButton_->itemText(static_cast<int>(a.button));
        else if constexpr (std::is_same_v<T, clicknova::input::KeyTap> || std::is_same_v<T, clicknova::input::KeyDown> || std::is_same_v<T, clicknova::input::KeyUp>) {
            int preset = keyPreset_->findData(a.virtualKey);
            return preset >= 1 ? keyPreset_->itemText(preset) : QStringLiteral("VK %1").arg(a.virtualKey);
        } else if constexpr (std::is_same_v<T, clicknova::input::MovePointer>)
            return QStringLiteral("%1 X:%2 Y:%3 · %4 ms")
                .arg(moveMode_->itemText(static_cast<int>(a.mode)))
                .arg(a.x).arg(a.y).arg(a.durationMs);
        else if constexpr (std::is_same_v<T, clicknova::input::TypeText>) {
            QString preview = QString::fromStdU16String(a.text).left(80).replace(QLatin1Char('\n'), QStringLiteral("↵"));
            if (a.text.size() > 80) preview += QStringLiteral("…");
            return QStringLiteral("\"%1\" · %2 %3").arg(preview).arg(a.text.size()).arg(trText("characters", "символов"));
        }
        else if constexpr (std::is_same_v<T, clicknova::input::RunScript>)
            return trText("PowerShell · %1 characters", "PowerShell · %1 символов").arg(a.code.size());
        else if constexpr (std::is_same_v<T, clicknova::input::WaitUntil>)
            return trText("Until %1 (local time)", "До %1 (по часам Windows)")
                .arg(QTime(a.hour, a.minute, a.second).toString(QStringLiteral("HH:mm:ss")));
        else if constexpr (std::is_same_v<T, Wheel>)
            return QStringLiteral("%1 %2").arg(a.axis == Axis::Vertical ? wheelAxis_->itemText(0) : wheelAxis_->itemText(1))
                .arg(a.delta);
        else return QString::fromStdU16String(
            clicknova::duration::format(a.durationMs, russian_));
    }, action);
}

namespace {
clicknova::gui::Icon actionPictogram(const Action& action) {
    using clicknova::gui::Icon;
    if (std::holds_alternative<clicknova::input::KeyTap>(action) ||
        std::holds_alternative<clicknova::input::KeyDown>(action) ||
        std::holds_alternative<clicknova::input::KeyUp>(action) ||
        std::holds_alternative<clicknova::input::KeyHold>(action)) return Icon::Keyboard;
    if (std::holds_alternative<clicknova::input::TypeText>(action) ||
        std::holds_alternative<clicknova::input::RunScript>(action)) return Icon::Text;
    if (std::holds_alternative<clicknova::input::Wait>(action) ||
        std::holds_alternative<clicknova::input::WaitUntil>(action)) return Icon::Wait;
    if (std::holds_alternative<clicknova::input::MovePointer>(action)) return Icon::Cursor;
    if (std::holds_alternative<clicknova::input::Wheel>(action)) return Icon::Mouse;
    return Icon::Click;
}
}

void InputLabWidget::refreshTable(int selectedRow) {
    normalizeStepWindows();
    QSignalBlocker blocker(table_);
    table_->setRowCount(static_cast<int>(actions_.size()));
    for (int row = 0; row < static_cast<int>(actions_.size()); ++row) {
        const auto& action = actions_[static_cast<std::size_t>(row)];
        auto* title = new QTableWidgetItem(QStringLiteral("%1. %2").arg(row + 1).arg(actionName(action)));
        title->setIcon(clicknova::gui::icon(actionPictogram(action)));
        table_->setItem(row, 0, title);
        QString details = actionDetails(action);
        if (static_cast<std::size_t>(row) < stepWindows_.size() &&
            !stepWindows_[static_cast<std::size_t>(row)].title.empty())
            details += QStringLiteral("  ·  ") + QString::fromStdString(stepWindows_[static_cast<std::size_t>(row)].title) +
                (stepWindows_[static_cast<std::size_t>(row)].enforce ? QString() :
                    trText(" (observed)", " (записано)"));
        table_->setItem(row, 1, new QTableWidgetItem(details));
        auto* more = new QToolButton(table_);
        more->setObjectName(QStringLiteral("inputLabRowMenu"));
        more->setIcon(clicknova::gui::icon(clicknova::gui::Icon::More));
        more->setToolTip(trText("Step actions: edit, duplicate, delete, move, insert, copy or paste.",
            "Действия с шагом: изменить, дублировать, удалить, переместить, вставить, скопировать."));
        more->setAutoRaise(true);
        more->setFocusPolicy(Qt::NoFocus);
        more->installEventFilter(this); // Right-click over ⋯ targets this row, too.
        table_->setCellWidget(row, 2, more);
        connect(more, &QToolButton::clicked, this, [this, row, more] {
            showActionMenu(row, more->mapToGlobal(QPoint(0, more->height())));
        });
    }
    if (selectedRow >= 0 && selectedRow < table_->rowCount()) {
        // A highlighted row must also be the current row used by shortcuts
        // and editor modifications (selectRow alone does not guarantee this).
        table_->setCurrentCell(selectedRow, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    } else {
        table_->selectionModel()->clearCurrentIndex();
        table_->clearSelection();
        selectedRow = -1;
    }
    selectAction(selectedRow);
    scheduleAutosave();
}

void InputLabWidget::selectAction(int index) {
    if (index >= 0 && index < static_cast<int>(actions_.size()) && selectedActionRows().size() == 1)
        loadEditor(actions_[static_cast<std::size_t>(index)]);
    editorGroup_->setEnabled(index >= 0 && selectedActionRows().size() == 1 && !isRunning());
    refreshStepWindowEditor(index);
    updateButtons();
}

void InputLabWidget::updateSelected() {
    if (updating_ || isRunning()) return;
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(actions_.size()) || selectedActionRows().size() != 1) return;
    if (!numericInputsValid()) return;
    Action edited = currentEditorAction();
    if (clicknova::storage::encodeInputAction(edited) ==
        clicknova::storage::encodeInputAction(actions_[static_cast<std::size_t>(row)])) return;
    checkpointActions();
    actions_[static_cast<std::size_t>(row)] = std::move(edited);
    // Preserve selection while replacing visible descriptions.
    QSignalBlocker blocker(table_);
    table_->item(row, 0)->setText(QStringLiteral("%1. %2").arg(row + 1).arg(actionName(actions_[static_cast<std::size_t>(row)])));
    table_->item(row, 0)->setIcon(clicknova::gui::icon(actionPictogram(actions_[static_cast<std::size_t>(row)])));
    QString details = actionDetails(actions_[static_cast<std::size_t>(row)]);
    if (!stepWindows_[static_cast<std::size_t>(row)].title.empty())
        details += QStringLiteral("  ·  ") + QString::fromStdString(stepWindows_[static_cast<std::size_t>(row)].title) +
            (stepWindows_[static_cast<std::size_t>(row)].enforce ? QString() :
                trText(" (observed)", " (записано)"));
    table_->item(row, 1)->setText(details);
    scheduleAutosave();
}

void InputLabWidget::addAction() {
    if (isRunning() || actions_.size() >= 512 || !numericInputsValid()) return;
    checkpointActions();
    actions_.push_back(currentEditorAction());
    stepWindows_.push_back({});
    refreshTable(static_cast<int>(actions_.size()) - 1);
}

void InputLabWidget::addActionOfType(int actionType) {
    insertActionAt(static_cast<int>(actions_.size()), actionType);
}

Action InputLabWidget::defaultActionOfType(int actionType) const {
    switch (actionType) {
    case MouseClick: return Click{Button::Left, 1, 200};
    case KeyTap: return clicknova::input::KeyTap{0x41};
    case KeyDown: return clicknova::input::KeyDown{0x41};
    case KeyUp: return clicknova::input::KeyUp{0x41};
    case MouseDown: return ButtonDown{Button::Left};
    case MouseUp: return ButtonUp{Button::Left};
    case Scroll: return Wheel{Axis::Vertical, 120};
    case Wait: return clicknova::input::Wait{500};
    case WaitForTime: {
        const QTime t = QTime::currentTime().addSecs(3600);
        return clicknova::input::WaitUntil{t.hour(), t.minute(), t.second()};
    }
    case MouseHoldTimed: return MouseHold{Button::Left, 500};
    case KeyHoldTimed: return clicknova::input::KeyHold{0x41, 500};
    case TypeText: return clicknova::input::TypeText{u""};
    case PowerShell: return clicknova::input::RunScript{u"# Script runs only after pressing Start\n"};
    case PointerMove: return MovePointer{MoveMode::Relative, 100, 100, 500};
    case -1: return currentEditorAction();
    default: return Click{Button::Left, 1, 200};
    }
}

void InputLabWidget::checkpointActions() {
    actionHistory_.record(actions_, stepWindows_, table_->currentRow());
}

void InputLabWidget::undoActionEdit() {
    if (isRunning()) return;
    int row = table_->currentRow();
    if (!actionHistory_.undo(actions_, stepWindows_, row)) return;
    refreshTable(row);
    reportStatus(trText("Action edit undone.", "Изменение действий отменено."));
}

void InputLabWidget::redoActionEdit() {
    if (isRunning()) return;
    int row = table_->currentRow();
    if (!actionHistory_.redo(actions_, stepWindows_, row)) return;
    refreshTable(row);
    reportStatus(trText("Action edit restored.", "Изменение действий повторено."));
}

void InputLabWidget::clearAllActions() {
    if (isRunning() || actions_.empty()) return;
    if (QMessageBox::question(this, QStringLiteral("ClickNova"),
        trText("Delete every action in the current list? You can undo this with Ctrl+Z.",
               "Удалить все действия в списке? Отменить удаление можно через Ctrl+Z."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    checkpointActions();
    actions_.clear();
    stepWindows_.clear();
    refreshTable(-1);
    reportStatus(trText("Action list cleared. Ctrl+Z to undo.",
                            "Список действий очищен. Ctrl+Z — отмена."));
}

namespace {
constexpr const char* kStepMime = "application/x-clicknova-action+json";
}

bool InputLabWidget::canPasteAction() const {
    const auto* mime = QApplication::clipboard()->mimeData();
    return mime && mime->hasFormat(QString::fromLatin1(kStepMime)) &&
        mime->data(QString::fromLatin1(kStepMime)).size() <= 1024 * 1024;
}

void InputLabWidget::copyRow(int row) {
    if (isRunning() || row < 0 || row >= static_cast<int>(actions_.size())) return;
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kStepMime), QJsonDocument(
        clicknova::storage::encodeBoundInputAction(actions_[static_cast<std::size_t>(row)],
            stepWindows_[static_cast<std::size_t>(row)]))
        .toJson(QJsonDocument::Compact));
    QApplication::clipboard()->setMimeData(mime);
    reportStatus(trText("Step copied. Paste in the list using Ctrl+V or the context menu.",
        "Шаг скопирован. Вставьте его через Ctrl+V или контекстное меню."));
}

void InputLabWidget::pasteActionAt(int row) {
    if (isRunning() || actions_.size() >= 512 || !canPasteAction()) return;
    const auto bytes = QApplication::clipboard()->mimeData()->data(QString::fromLatin1(kStepMime));
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject()) return;
    std::vector<Action> pasted;
    std::vector<StepWindow> pastedWindows;
    const auto obj = json.object();
    if (obj.contains(QStringLiteral("steps"))) {
        if (!obj.value(QStringLiteral("steps")).isArray()) return;
        for (const auto& value : obj.value(QStringLiteral("steps")).toArray()) {
            if (!value.isObject()) return;
            const auto action = clicknova::storage::decodeInputAction(value.toObject());
            if (!action) return;
            const auto binding = clicknova::storage::decodeStepWindow(value.toObject());
            if (!binding) return;
            pasted.push_back(*action);
            pastedWindows.push_back(*binding);
        }
    } else {
        const auto action = clicknova::storage::decodeInputAction(obj);
        const auto binding = clicknova::storage::decodeStepWindow(obj);
        if (action && binding) { pasted.push_back(*action); pastedWindows.push_back(*binding); }
    }
    if (pasted.empty() || pasted.size() + actions_.size() > 512) {
        reportStatus(trText("Clipboard is empty or the 512-step limit was reached.",
            "Буфер пуст или превышен предел в 512 шагов."));
        return;
    }
    const int index = std::clamp(row, 0, static_cast<int>(actions_.size()));
    checkpointActions();
    actions_.insert(actions_.begin() + index, pasted.begin(), pasted.end());
    stepWindows_.insert(stepWindows_.begin() + index, pastedWindows.begin(), pastedWindows.end());
    refreshTable(index);
    std::vector<int> selection;
    for (int i = 0; i < static_cast<int>(pasted.size()); ++i) selection.push_back(index + i);
    refreshSelectedRows(selection);
}

void InputLabWidget::insertActionAt(int row, int actionType) {
    if (isRunning() || actions_.size() >= 512) return;
    Action action = defaultActionOfType(actionType);
    const int index = std::clamp(row, 0, static_cast<int>(actions_.size()));
    checkpointActions();
    actions_.insert(actions_.begin() + index, std::move(action));
    stepWindows_.insert(stepWindows_.begin() + index, {});
    refreshTable(index);
}

void InputLabWidget::deleteRow(int row) {
    if (isRunning() || row < 0 || row >= static_cast<int>(actions_.size())) return;
    checkpointActions();
    actions_.erase(actions_.begin() + row);
    stepWindows_.erase(stepWindows_.begin() + row);
    refreshTable(std::min(row, static_cast<int>(actions_.size()) - 1));
}

void InputLabWidget::duplicateRow(int row) {
    if (isRunning() || row < 0 || row >= static_cast<int>(actions_.size()) || actions_.size() >= 512) return;
    Action duplicate = actions_[static_cast<std::size_t>(row)];
    const StepWindow duplicateWindow = stepWindows_[static_cast<std::size_t>(row)];
    checkpointActions();
    actions_.insert(actions_.begin() + row + 1, std::move(duplicate));
    stepWindows_.insert(stepWindows_.begin() + row + 1, duplicateWindow);
    refreshTable(row + 1);
}

void InputLabWidget::moveActionRow(int from, int to) {
    if (isRunning() || from < 0 || to < 0 ||
        from >= static_cast<int>(actions_.size()) || to >= static_cast<int>(actions_.size()) || from == to) return;
    const bool previouslyValid = clicknova::input::validate({actions_, 1, 0}).empty();
    auto reordered = actions_;
    if (!clicknova::input::moveAction(reordered, static_cast<std::size_t>(from), static_cast<std::size_t>(to))) return;
    if (previouslyValid && !clicknova::input::validate({reordered, 1, 0}).empty()) {
        reportStatus(trText("Cannot move: mouse or keyboard hold/release order would be broken.",
            "Нельзя переместить: нарушится порядок зажатия и отпускания кнопки или клавиши."));
        return;
    }
    checkpointActions();
    actions_ = std::move(reordered);
    // Move the binding in lockstep with its action.
    auto bound = std::move(stepWindows_[from]);
    stepWindows_.erase(stepWindows_.begin() + from);
    stepWindows_.insert(stepWindows_.begin() + to, std::move(bound));
    refreshTable(to);
}

std::vector<int> InputLabWidget::selectedActionRows() const {
    std::vector<int> rows;
    if (!table_ || !table_->selectionModel()) return rows;
    for (const QModelIndex& index : table_->selectionModel()->selectedRows()) {
        if (index.row() >= 0 && index.row() < static_cast<int>(actions_.size()))
            rows.push_back(index.row());
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void InputLabWidget::refreshSelectedRows(const std::vector<int>& rows) {
    if (!table_ || !table_->selectionModel() || rows.empty()) return;
    const QSignalBlocker blocker(table_);
    table_->clearSelection();
    for (const int row : rows) {
        if (row >= 0 && row < table_->rowCount())
            table_->selectionModel()->select(table_->model()->index(row, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    table_->setCurrentIndex(table_->model()->index(rows.front(), 0));
    selectAction(rows.size() == 1 ? rows.front() : -1);
}

void InputLabWidget::deleteSelectedRows() {
    if (isRunning()) return;
    auto rows = selectedActionRows();
    if (rows.empty() && table_->currentRow() >= 0) rows.push_back(table_->currentRow());
    if (rows.empty()) return;
    checkpointActions();
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        actions_.erase(actions_.begin() + *it);
        stepWindows_.erase(stepWindows_.begin() + *it);
    }
    refreshTable(std::min(rows.front(), static_cast<int>(actions_.size()) - 1));
}

void InputLabWidget::duplicateSelectedRows() {
    if (isRunning()) return;
    const auto rows = selectedActionRows();
    if (rows.empty() || actions_.size() + rows.size() > 512) return;
    checkpointActions();
    const auto original = actions_;
    std::vector<Action> result;
    std::vector<StepWindow> resultWindows;
    std::vector<int> selected;
    result.reserve(original.size() + rows.size());
    std::size_t next = 0;
    for (int i = 0; i < static_cast<int>(original.size()); ++i) {
        result.push_back(original[static_cast<std::size_t>(i)]);
        resultWindows.push_back(stepWindows_[static_cast<std::size_t>(i)]);
        if (next < rows.size() && rows[next] == i) {
            selected.push_back(static_cast<int>(result.size()));
            result.push_back(original[static_cast<std::size_t>(i)]);
            resultWindows.push_back(stepWindows_[static_cast<std::size_t>(i)]);
            ++next;
        }
    }
    actions_ = std::move(result);
    stepWindows_ = std::move(resultWindows);
    refreshTable(selected.front());
    refreshSelectedRows(selected);
}

void InputLabWidget::copySelectedRows() {
    if (isRunning()) return;
    const auto rows = selectedActionRows();
    if (rows.empty()) return;
    if (rows.size() == 1) { copyRow(rows.front()); return; }
    QJsonArray copiedSteps;
    for (const int row : rows)
        copiedSteps.append(clicknova::storage::encodeBoundInputAction(
            actions_[static_cast<std::size_t>(row)], stepWindows_[static_cast<std::size_t>(row)]));
    QJsonObject obj;
    obj.insert(QStringLiteral("steps"), copiedSteps);
    const auto bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    if (bytes.size() > 1024 * 1024) {
        reportStatus(trText("Selected steps are too large to copy.", "Выделенные шаги слишком велики для копирования."));
        return;
    }
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kStepMime), bytes);
    QApplication::clipboard()->setMimeData(mime);
}

void InputLabWidget::moveSelectedRows(int direction) {
    if (isRunning() || (direction != 1 && direction != -1)) return;
    auto rows = selectedActionRows();
    if (rows.empty()) return;
    auto candidate = actions_;
    auto candidateWindows = stepWindows_;
    // vector<bool> yields proxy references that MSVC cannot std::swap.
    // A byte mask also keeps grouped selections in sync with moved actions.
    std::vector<unsigned char> selected(candidate.size(), 0);
    for (int row : rows) selected[static_cast<std::size_t>(row)] = true;
    if (direction < 0) {
        for (std::size_t i = 1; i < selected.size(); ++i) {
            if (selected[i] && !selected[i - 1]) {
                std::swap(candidate[i], candidate[i - 1]);
                std::swap(candidateWindows[i], candidateWindows[i - 1]);
                std::swap(selected[i], selected[i - 1]);
            }
        }
    } else {
        for (std::size_t i = selected.size() - 1; i > 0; --i) {
            if (selected[i - 1] && !selected[i]) {
                std::swap(candidate[i], candidate[i - 1]);
                std::swap(candidateWindows[i], candidateWindows[i - 1]);
                std::swap(selected[i], selected[i - 1]);
            }
        }
    }
    if (std::equal(candidate.begin(), candidate.end(), actions_.begin(),
        [](const Action& a, const Action& b) {
            return clicknova::storage::encodeInputAction(a) == clicknova::storage::encodeInputAction(b);
        }) && candidateWindows == stepWindows_) return;
    if (clicknova::input::validate({actions_, 1, 0}).empty() &&
        !clicknova::input::validate({candidate, 1, 0}).empty()) {
        reportStatus(trText("Moving these steps would break a held input pair.",
            "Перемещение нарушит порядок зажатия и отпускания кнопки."));
        return;
    }
    checkpointActions();
    actions_ = std::move(candidate);
    stepWindows_ = std::move(candidateWindows);
    std::vector<int> newSelection;
    for (int i = 0; i < static_cast<int>(selected.size()); ++i)
        if (selected[static_cast<std::size_t>(i)]) newSelection.push_back(i);
    refreshTable(newSelection.front());
    refreshSelectedRows(newSelection);
}

void InputLabWidget::moveSelectedRowsTo(const std::vector<int>& rows, int insertion) {
    if (isRunning() || rows.empty()) return;
    const int n = static_cast<int>(actions_.size());
    if (insertion < 0 || insertion > n) return;
    std::vector<unsigned char> selected(actions_.size(), 0);
    for (int row : rows) {
        if (row < 0 || row >= n) return;
        selected[static_cast<std::size_t>(row)] = true;
    }
    std::vector<Action> extracted, remaining;
    std::vector<StepWindow> extractedWindows, remainingWindows;
    int before = 0;
    for (int i = 0; i < n; ++i) {
        if (selected[static_cast<std::size_t>(i)]) {
            extracted.push_back(actions_[i]); extractedWindows.push_back(stepWindows_[i]);
        } else {
            remaining.push_back(actions_[i]); remainingWindows.push_back(stepWindows_[i]);
            if (i < insertion) ++before;
        }
    }
    if (extracted.empty()) return;
    auto candidate = remaining;
    candidate.insert(candidate.begin() + before, extracted.begin(), extracted.end());
    remainingWindows.insert(remainingWindows.begin() + before,
        extractedWindows.begin(), extractedWindows.end());
    if (std::equal(candidate.begin(), candidate.end(), actions_.begin(),
        [](const Action& a, const Action& b) {
            return clicknova::storage::encodeInputAction(a) == clicknova::storage::encodeInputAction(b);
        }) && remainingWindows == stepWindows_) return;
    if (clicknova::input::validate({actions_, 1, 0}).empty() &&
        !clicknova::input::validate({candidate, 1, 0}).empty()) {
        reportStatus(trText("Moving these steps would break a held input pair.",
            "Перемещение нарушит порядок зажатия и отпускания кнопки."));
        return;
    }
    checkpointActions();
    actions_ = std::move(candidate);
    stepWindows_ = std::move(remainingWindows);
    std::vector<int> newSelection;
    for (int i = 0; i < static_cast<int>(extracted.size()); ++i) newSelection.push_back(before + i);
    refreshTable(before);
    refreshSelectedRows(newSelection);
}

void InputLabWidget::normalizeStepWindows() {
    if (stepWindows_.size() != actions_.size()) stepWindows_.resize(actions_.size());
}

void InputLabWidget::refreshStepWindowEditor(int row) {
    if (!stepWindowStatus_ || !stepEnforce_ || !stepActivate_ || !stepWindowWait_) return;
    normalizeStepWindows();
    const bool single = row >= 0 && row < static_cast<int>(actions_.size()) &&
        selectedActionRows().size() == 1;
    const QSignalBlocker blockEnforce(stepEnforce_);
    const QSignalBlocker blockActivate(stepActivate_);
    const QSignalBlocker blockWait(stepWindowWait_);
    if (!single) {
        stepWindowStatus_->setText(trText("Choose one step", "Выберите один шаг"));
        stepEnforce_->setChecked(false);
        stepActivate_->setChecked(false);
        stepWindowWait_->setText(QStringLiteral("0"));
    } else {
        const auto& binding = stepWindows_[static_cast<std::size_t>(row)];
        stepWindowStatus_->setText(binding.title.empty() ?
            trText("Uses default foreground window", "Окно по умолчанию") :
            QString::fromStdString(binding.title) + (binding.enforce ? QString() :
                trText(" (recorded only)", " (только из записи)")));
        stepEnforce_->setChecked(!binding.title.empty() && binding.enforce);
        stepActivate_->setChecked(binding.activate);
        stepWindowWait_->setText(QString::number(binding.waitMs));
    }
    for (auto* widget : {static_cast<QWidget*>(chooseStepWindow_), static_cast<QWidget*>(clearStepWindow_)})
        widget->setEnabled(single && !isRunning());
    const bool hasWindow = single && !stepWindows_[static_cast<std::size_t>(row)].title.empty();
    stepEnforce_->setEnabled(hasWindow && !isRunning());
    const bool requiresWindow = hasWindow && stepWindows_[static_cast<std::size_t>(row)].enforce;
    stepActivate_->setEnabled(requiresWindow && !isRunning());
    stepWindowWait_->setEnabled(requiresWindow && !isRunning());
}

void InputLabWidget::chooseStepWindow() {
    if (isRunning() || selectedActionRows().size() != 1) return;
    const auto selected = chooseTargetWindow(this, russian_ ? QStringLiteral("ru") : QStringLiteral("en"));
    if (!selected) return;
    const auto row = static_cast<std::size_t>(selectedActionRows().front());
    checkpointActions();
    stepWindows_[row].title = QString::fromStdWString(selected->title).toUtf8().toStdString();
    stepWindows_[row].process = QString::fromStdWString(selected->process).toUtf8().toStdString();
    stepWindows_[row].enforce = true;
    refreshTable(static_cast<int>(row));
}

void InputLabWidget::clearStepWindow() {
    if (isRunning() || selectedActionRows().size() != 1) return;
    const auto row = static_cast<std::size_t>(selectedActionRows().front());
    if (stepWindows_[row].title.empty()) return;
    checkpointActions();
    stepWindows_[row] = {};
    refreshTable(static_cast<int>(row));
}

void InputLabWidget::makeWindowBindingsInformational(bool selectedOnly) {
    if (isRunning()) return;
    auto rows = selectedOnly ? selectedActionRows() : std::vector<int>{};
    if (!selectedOnly) {
        for (int row = 0; row < static_cast<int>(actions_.size()); ++row)
            rows.push_back(row);
    }
    bool changed = false;
    for (int row : rows) {
        if (row < 0 || static_cast<std::size_t>(row) >= stepWindows_.size()) continue;
        const auto& binding = stepWindows_[static_cast<std::size_t>(row)];
        if (!binding.title.empty() && (binding.enforce || binding.activate || binding.waitMs)) {
            changed = true;
            break;
        }
    }
    if (!changed) return;
    checkpointActions();
    for (int row : rows) {
        if (row < 0 || static_cast<std::size_t>(row) >= stepWindows_.size()) continue;
        auto& binding = stepWindows_[static_cast<std::size_t>(row)];
        if (binding.title.empty()) continue;
        binding.enforce = false;
        binding.activate = false;
        binding.waitMs = 0;
    }
    const auto selection = selectedActionRows();
    refreshTable(selection.empty() ? -1 : selection.front());
    if (!selection.empty()) refreshSelectedRows(selection);
}

void InputLabWidget::editStepWindow() {
    if (isRunning() || selectedActionRows().size() != 1) return;
    const auto row = static_cast<std::size_t>(selectedActionRows().front());
    const auto ms = nonnegative(stepWindowWait_);
    if (ms < 0) return;
    auto binding = stepWindows_[row];
    if (binding.title.empty()) return;
    binding.enforce = stepEnforce_->isChecked();
    binding.activate = binding.enforce && stepActivate_->isChecked();
    binding.waitMs = binding.enforce ? ms : 0;
    if (stepWindows_[row] == binding) return;
    checkpointActions();
    stepWindows_[row] = std::move(binding);
    refreshStepWindowEditor(static_cast<int>(row));
    scheduleAutosave();
}

bool InputLabWidget::eventFilter(QObject* watched, QEvent* event) {
    if (table_ && event && event->type() == QEvent::ContextMenu) {
        const auto* context = static_cast<QContextMenuEvent*>(event);
        auto* header = table_->horizontalHeader();
        if (watched == header || watched == header->viewport()) {
            // A header right-click always works on the *whole* list.
            showActionMenu(-1, context->globalPos());
            event->accept();
            return true;
        }
        if (watched == table_ || watched == table_->viewport() ||
            (qobject_cast<QToolButton*>(watched) &&
             watched->objectName() == QStringLiteral("inputLabRowMenu"))) {
            // Use the global mouse position for every source. QAbstractScrollArea
            // sends customContextMenuRequested with viewport-relative positions,
            // whereas a cell widget uses its own local coordinates. Computing
            // the row from either signal's position was not reliable.
            const QPoint pos = table_->viewport()->mapFromGlobal(context->globalPos());
            const QModelIndex hit = table_->indexAt(pos);
            showActionMenu(hit.isValid() ? hit.row() : -1, context->globalPos());
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void InputLabWidget::showActionMenu(int row, const QPoint& globalPos) {
    if (isRunning()) return;
    const bool hasRow = row >= 0 && row < static_cast<int>(actions_.size());
    if (hasRow && !table_->selectionModel()->isRowSelected(row, QModelIndex())) {
        table_->setCurrentCell(row, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    // A menu closed using popup()/close() can remain alive until the deferred
    // delete event runs. In particular, processEvents() does not guarantee that
    // event has run. Do not leave a hidden, stale menu with the same object name:
    // the next right-click and the Windows GUI smoke test must find the new one.
    if (activeActionMenu_) {
        activeActionMenu_->setObjectName(QString());
        activeActionMenu_->close();
    }
    auto* menu = new QMenu(table_);
    menu->setObjectName(QStringLiteral("inputLabContextMenu"));
    activeActionMenu_ = menu;
    // Keep the cleanup connection scoped to the menu itself; it must not
    // dereference InputLabWidget while child widgets are being destroyed.
    connect(menu, &QMenu::aboutToHide, menu, [menu] {
        menu->setObjectName(QString());
        menu->deleteLater();
    });
    const auto item = [this, menu](const char* en, const char* ru,
                                   clicknova::gui::Icon picture, const char* id,
                                   const QKeySequence& shortcut = {}) {
        QAction* a = menu->addAction(clicknova::gui::icon(picture), trText(en, ru));
        a->setObjectName(QString::fromLatin1(id));
        if (!shortcut.isEmpty()) a->setShortcut(shortcut);
        return a;
    };
    const int selectionCount = static_cast<int>(selectedActionRows().size());
    const bool multiple = hasRow && selectionCount > 1;
    // Each operation is present only once: labels change with selection size.
    // Select All is useful only when at least one row is not selected.
    if (!actions_.empty() && selectionCount < static_cast<int>(actions_.size())) {
        auto* selectAll = item("Select all", "Выделить всё", clicknova::gui::Icon::Copy,
                               hasRow ? "multiSelectAll" : "listSelectAll",
                               QKeySequence(QKeySequence::SelectAll));
        connect(selectAll, &QAction::triggered, this, [this] { table_->selectAll(); });
        menu->addSeparator();
    }
    if (multiple) {
        auto* remove = item("Delete selected", "Удалить выделенные", clicknova::gui::Icon::Delete,
                            "multiDelete", QKeySequence(Qt::Key_Delete));
        connect(remove, &QAction::triggered, this, [this] { deleteSelectedRows(); });
        auto* dup = item("Duplicate selected", "Дублировать выделенные", clicknova::gui::Icon::Copy,
                         "multiDuplicate", QKeySequence(Qt::CTRL | Qt::Key_D));
        dup->setEnabled(actions_.size() + selectedActionRows().size() <= 512);
        connect(dup, &QAction::triggered, this, [this] { duplicateSelectedRows(); });
        auto* cp = item("Copy selected", "Копировать выделенные", clicknova::gui::Icon::Copy,
                        "multiCopy", QKeySequence(QKeySequence::Copy));
        connect(cp, &QAction::triggered, this, [this] { copySelectedRows(); });
        auto* up = item("Move selected up", "Выделенные вверх", clicknova::gui::Icon::Up,
                        "multiUp", QKeySequence(Qt::CTRL | Qt::Key_Up));
        connect(up, &QAction::triggered, this, [this] { moveSelectedRows(-1); });
        auto* down = item("Move selected down", "Выделенные вниз", clicknova::gui::Icon::Down,
                          "multiDown", QKeySequence(Qt::CTRL | Qt::Key_Down));
        connect(down, &QAction::triggered, this, [this] { moveSelectedRows(1); });
        menu->addSeparator();
    }
    // Existing recordings imported before this fix may contain mandatory
    // bindings to the Windows desktop. Preserve labels while releasing those
    // constraints, including via Ctrl+A for an entire recorded scenario.
    if (hasRow && !selectedActionRows().empty()) {
        auto* recordedOnly = item("Use selected window names as information only",
            "Окна выделенных шагов — только информация",
            clicknova::gui::Icon::Window, "stepInformationalWindows");
        connect(recordedOnly, &QAction::triggered, this,
                [this] { makeWindowBindingsInformational(true); });
        menu->addSeparator();
    } else if (!hasRow && !actions_.empty()) {
        auto* recordedOnly = item("Use all window names as information only",
            "Окна всех шагов — только информация",
            clicknova::gui::Icon::Window, "listInformationalWindows");
        connect(recordedOnly, &QAction::triggered, this,
                [this] { makeWindowBindingsInformational(false); });
        menu->addSeparator();
    }
    if (hasRow && !multiple) {
        auto* edit = item("Edit", "Редактировать", clicknova::gui::Icon::Edit, "stepEdit");
        connect(edit, &QAction::triggered, this, [this, row] {
            // A menu can remain open when playback starts through another control.
            if (isRunning()) return;
            table_->setCurrentCell(row, 0);
            type_->setFocus(Qt::OtherFocusReason);
        });
        auto* bindWindow = item("Select window for this step", "Выбрать окно для шага",
            clicknova::gui::Icon::Window, "stepChooseWindow");
        connect(bindWindow, &QAction::triggered, this, [this, row] {
            if (isRunning()) return;
            table_->setCurrentCell(row, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            chooseStepWindow();
        });
        auto* unbindWindow = item("Clear window for this step", "Убрать окно шага",
            clicknova::gui::Icon::Close, "stepClearWindow");
        unbindWindow->setEnabled(row < static_cast<int>(stepWindows_.size()) &&
            !stepWindows_[static_cast<std::size_t>(row)].title.empty());
        connect(unbindWindow, &QAction::triggered, this, [this, row] {
            if (isRunning()) return;
            table_->setCurrentCell(row, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            clearStepWindow();
        });
        auto* duplicate = item("Duplicate", "Дублировать", clicknova::gui::Icon::Copy,
                               "stepDuplicate", QKeySequence(Qt::CTRL | Qt::Key_D));
        duplicate->setEnabled(actions_.size() < 512);
        connect(duplicate, &QAction::triggered, this, [this, row] { duplicateRow(row); });
        auto* remove = item("Delete", "Удалить", clicknova::gui::Icon::Delete,
                            "stepDelete", QKeySequence(Qt::Key_Delete));
        connect(remove, &QAction::triggered, this, [this, row] { deleteRow(row); });
        menu->addSeparator();
        auto* up = item("Move up", "Переместить вверх", clicknova::gui::Icon::Up,
                        "stepUp", QKeySequence(Qt::CTRL | Qt::Key_Up));
        up->setEnabled(row > 0);
        connect(up, &QAction::triggered, this, [this, row] { moveActionRow(row, row - 1); });
        auto* down = item("Move down", "Переместить вниз", clicknova::gui::Icon::Down,
                          "stepDown", QKeySequence(Qt::CTRL | Qt::Key_Down));
        down->setEnabled(row + 1 < static_cast<int>(actions_.size()));
        connect(down, &QAction::triggered, this, [this, row] { moveActionRow(row, row + 1); });
        auto* first = item("Move to beginning", "В самое начало", clicknova::gui::Icon::Up,
                           "stepFirst");
        first->setEnabled(row > 0);
        connect(first, &QAction::triggered, this, [this, row] { moveActionRow(row, 0); });
        auto* last = item("Move to end", "В самый конец", clicknova::gui::Icon::Down,
                          "stepLast");
        last->setEnabled(row + 1 < static_cast<int>(actions_.size()));
        connect(last, &QAction::triggered, this, [this, row] {
            moveActionRow(row, static_cast<int>(actions_.size()) - 1);
        });
        menu->addSeparator();
    }

    // Explicit records avoid MSVC's mixed initializer_list/tuple deduction
    // failure (C3535/C2440/C3617/C2439 in the previous release).
    struct MenuStep { int type; const char* english; const char* russian; };
    constexpr std::array<MenuStep, 15> types{{
        {MouseClick, "Mouse click", "Клик мыши"},
        {MouseDown, "Mouse button down", "Зажать кнопку мыши"},
        {MouseUp, "Mouse button up", "Отпустить кнопку мыши"},
        {MouseHoldTimed, "Timed mouse hold", "Зажать мышь на время"},
        {PointerMove, "Move cursor", "Переместить курсор"},
        {Scroll, "Scroll wheel", "Прокрутить колесо"},
        {KeyTap, "Key press", "Нажать клавишу"},
        {KeyDown, "Key down", "Зажать клавишу"},
        {KeyUp, "Key up", "Отпустить клавишу"},
        {KeyHoldTimed, "Timed key hold", "Зажать клавишу на время"},
        {TypeText, "Type text", "Ввести текст"},
        {PowerShell, "Local PowerShell script", "Локальный скрипт PowerShell"},
        {Wait, "Wait", "Ожидание"},
        {WaitForTime, "Wait until clock time", "Ждать до времени"},
        {-1, "Use editor settings", "По настройкам редактора"}
    }};
    const auto insertMenu = [this, menu, &types](const char* en, const char* ru, int rowToInsert, const char* id) {
        QMenu* submenu = menu->addMenu(trText(en, ru));
        submenu->setObjectName(QString::fromLatin1(id));
        for (const auto& type : types) {
            if (type.type == KeyTap || type.type == Wait || type.type == -1) submenu->addSeparator();
            QAction* a = submenu->addAction(trText(type.english, type.russian));
            a->setEnabled(actions_.size() < 512);
            connect(a, &QAction::triggered, this, [this, rowToInsert, kind = type.type] {
                insertActionAt(rowToInsert, kind);
            });
        }
    };
    if (hasRow) {
        const auto rows = selectedActionRows();
        const int first = multiple ? *std::min_element(rows.begin(), rows.end()) : row;
        const int last = multiple ? *std::max_element(rows.begin(), rows.end()) : row;
        insertMenu(multiple ? "Insert before selection" : "Insert before",
                   multiple ? "Добавить перед выделенными" : "Добавить перед", first, "stepInsertBefore");
        insertMenu(multiple ? "Insert after selection" : "Insert after",
                   multiple ? "Добавить после выделенных" : "Добавить после", last + 1, "stepInsertAfter");
    } else {
        insertMenu("Add action", "Добавить действие", static_cast<int>(actions_.size()), "listAddAction");
    }
    menu->addSeparator();
    if (hasRow && !multiple) {
        auto* copy = item("Copy", "Скопировать", clicknova::gui::Icon::Copy,
                          "stepCopy", QKeySequence(QKeySequence::Copy));
        connect(copy, &QAction::triggered, this, [this, row] { if (selectedActionRows().size() > 1) copySelectedRows(); else copyRow(row); });
        auto* pasteBefore = item("Paste before", "Вставить перед", clicknova::gui::Icon::Copy,
                                 "stepPasteBefore", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
        pasteBefore->setEnabled(actions_.size() < 512 && canPasteAction());
        connect(pasteBefore, &QAction::triggered, this, [this, row] { pasteActionAt(row); });
    }
    if (multiple) {
        auto* before = item("Paste before selection", "Вставить перед выделенными",
            clicknova::gui::Icon::Copy, "stepPasteBefore", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
        before->setEnabled(actions_.size() < 512 && canPasteAction());
        const auto rows = selectedActionRows();
        const int first = *std::min_element(rows.begin(), rows.end());
        connect(before, &QAction::triggered, this, [this, first] { pasteActionAt(first); });
    }
    auto* paste = item(!hasRow ? "Paste at end" : multiple ? "Paste after selection" : "Paste after",
                       !hasRow ? "Вставить в конец" : multiple ? "Вставить после выделенных" : "Вставить после",
                       clicknova::gui::Icon::Copy, "stepPaste", QKeySequence(QKeySequence::Paste));
    paste->setEnabled(actions_.size() < 512 && canPasteAction());
    const auto rows = selectedActionRows();
    const int at = !hasRow ? static_cast<int>(actions_.size()) :
        multiple ? *std::max_element(rows.begin(), rows.end()) + 1 : row + 1;
    connect(paste, &QAction::triggered, this, [this, at] { pasteActionAt(at); });
    menu->addSeparator();
    auto* undo = item("Undo", "Отменить", clicknova::gui::Icon::Up,
                      "listUndo", QKeySequence(QKeySequence::Undo));
    undo->setEnabled(actionHistory_.canUndo());
    connect(undo, &QAction::triggered, this, [this] { undoActionEdit(); });
    auto* redo = item("Redo", "Повторить", clicknova::gui::Icon::Down,
                      "listRedo", QKeySequence(Qt::CTRL | Qt::Key_Y));
    redo->setEnabled(actionHistory_.canRedo());
    connect(redo, &QAction::triggered, this, [this] { redoActionEdit(); });
    if (!hasRow) {
        menu->addSeparator();
        auto* clear = item("Clear all actions…", "Удалить все действия…", clicknova::gui::Icon::Delete,
                           "listClearAll");
        clear->setEnabled(!actions_.empty());
        connect(clear, &QAction::triggered, this, [this] { clearAllActions(); });
    }
    menu->popup(globalPos);
}

void InputLabWidget::reportStatus(const QString& message) {
    if (status_) status_->setText(message);
    if (statusSink_) statusSink_(message);
}

void InputLabWidget::refreshRecoveryAvailability() {
    hasRecovery_ = !clicknova::gui::recording_checkpoint::list().empty();
    if (recoverRecording_)
        recoverRecording_->setVisible(hasRecovery_ && !recording_);
}

void InputLabWidget::updateButtons() {
    int row = table_->currentRow();
    bool selected = row >= 0 && row < static_cast<int>(actions_.size());
    bool idle = !isRunning();
    record_->setEnabled(idle || recording_);
    recoverRecording_->setEnabled(idle && !recording_ && hasRecovery_);
    recoverRecording_->setVisible(hasRecovery_ && !recording_);
    if (recording_) record_->setText(trText("Show recorder", "Показать запись"));
    else record_->setText(trText("Record actions", "Записать действия"));
    chooseWindow_->setEnabled(idle);
    clearWindow_->setEnabled(idle && targetWindow_.has_value());
    for (auto* button : {quickClick_, quickKey_, quickText_, quickWait_, quickWaitUntil_, quickCode_})
        button->setEnabled(idle && actions_.size() < 512);
    selectAll_->setEnabled(idle && !actions_.empty());
    table_->setDragEnabled(idle);
    table_->setAcceptDrops(idle);
    const int selectedCount = static_cast<int>(selectedActionRows().size());
    editorGroup_->setTitle(selectedCount > 1
        ? trText("%1 steps selected — use context menu for group actions",
                 "Выбрано шагов: %1 — групповые действия по ПКМ").arg(selectedCount)
        : trText("Selected action — changes are immediate",
                 "Выбранное действие — изменения применяются сразу"));
    editorGroup_->setEnabled(idle && selected && selectedCount == 1);
    captureMouse_->setEnabled(idle && selected);
    captureKey_->setEnabled(idle && selected);
    textInput_->setReadOnly(!idle);
    scriptInput_->setReadOnly(!idle);
    repetitions_->setEnabled(idle);
    passDelay_->setEnabled(idle);
    keepAwake_->setEnabled(idle);
    run_->setEnabled(idle && !actions_.empty());
    savePlan_->setEnabled(idle && !actions_.empty());
    savedPlans_->setEnabled(idle);
    loadPlan_->setEnabled(idle && savedPlans_->currentIndex() > 0);
    deletePlan_->setEnabled(idle && savedPlans_->currentIndex() > 0 &&
        savedPlans_->currentData(Qt::UserRole).toInt() == static_cast<int>(clicknova::storage::Kind::InputLab));
    if (importCurrentLegacy_) importCurrentLegacy_->setEnabled(idle);
    stop_->setEnabled(running_ || countdown_);
}

void InputLabWidget::beginRecording() {
    if (isRunning()) return;
    if (canBegin_ && !canBegin_()) {
        reportStatus(trText("Stop the other automation or point capture first.",
                                "Сначала остановите другую автоматизацию или захват точки."));
        return;
    }
#ifndef _WIN32
    reportStatus(trText("Recording is available on Windows.", "Запись доступна на Windows."));
#else
    recording_ = true;
    auto* overlay = new InputRecorderDialog(russian_, [this](std::vector<clicknova::recording::Sample> samples,
                                                         bool cancelled, std::uint64_t durationMs) {
        const QString file = recorder_ ? recorder_->checkpointFile() : QString{};
        reviewCapturedRecording(std::move(samples), cancelled, durationMs, file);
    });
    recorder_ = overlay;
    // Keep the main window available: Windows may hide owned tool windows
    // while minimizing their application, leaving recording controls lost.
    // The recorder remains on top; users may minimize the editor manually.
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    reportStatus(trText("Move the floating panel, then press Start on it.",
                            "Переместите плавающую панель, затем нажмите на ней Начать."));
    updateButtons();
#endif
}

void InputLabWidget::recoverRecording() {
    if (isRunning() || recording_) return;
    const auto sessions = clicknova::gui::recording_checkpoint::list();
    if (sessions.empty()) {
        refreshRecoveryAvailability();
        QMessageBox::information(this, QStringLiteral("ClickNova"),
            trText("No saved recordings found.", "Сохранённых записей не найдено."));
        return;
    }
    QStringList choices;
    for (const auto& session : sessions) {
        choices << QStringLiteral("%1  ·  %2: %3  ·  %4: %5")
            .arg(session.created.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")))
            .arg(trText("updated", "обновлено"), session.updated.toLocalTime().toString(QStringLiteral("HH:mm:ss")))
            .arg(trText("events", "событий"))
            .arg(static_cast<qlonglong>(session.samples.size()));
    }
    bool confirmed = false;
    const QString chosen = QInputDialog::getItem(this, QStringLiteral("ClickNova"),
        trText("Select a recording by date and time:", "Выберите запись по дате и времени:"),
        choices, 0, false, &confirmed);
    if (!confirmed || chosen.isEmpty()) return;
    const int index = choices.indexOf(chosen);
    if (index < 0 || index >= static_cast<int>(sessions.size())) return;
    auto session = sessions[static_cast<std::size_t>(index)];
    QMessageBox choice(this);
    choice.setWindowTitle(QStringLiteral("ClickNova"));
    choice.setText(trText("A recovery copy may contain typed text. Review or explicitly delete this recording.",
                          "Резервная запись может содержать введённый текст. Просмотрите или удалите выбранную запись."));
    auto* view = choice.addButton(trText("Review", "Просмотр"), QMessageBox::AcceptRole);
    auto* erase = choice.addButton(trText("Delete saved recording", "Удалить сохранённую запись"), QMessageBox::DestructiveRole);
    choice.addButton(QMessageBox::Cancel);
    choice.exec();
    if (choice.clickedButton() == erase) {
        if (QMessageBox::question(this, QStringLiteral("ClickNova"),
                trText("Permanently delete this recovery recording?", "Удалить эту резервную запись без возможности восстановления?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            clicknova::gui::recording_checkpoint::remove(session.path);
        refreshRecoveryAvailability();
        return;
    }
    if (choice.clickedButton() != view) return;
    reviewCapturedRecording(std::move(session.samples), false, session.durationMs, session.path);
}

void InputLabWidget::reviewCapturedRecording(std::vector<clicknova::recording::Sample> samples,
                                            bool cancelled, std::uint64_t durationMs,
                                            const QString& checkpointFile) {
        recording_ = false;
        recorder_ = nullptr;
        refreshRecoveryAvailability();
        window()->showNormal();
        window()->raise();
        window()->activateWindow();
        if (cancelled) {
            reportStatus(trText("Recording cancelled. Actions unchanged.", "Запись отменена. Действия не изменены."));
            updateButtons();
            return;
        }
        if (samples.empty()) {
            reportStatus(trText("No input events were captured. Actions unchanged.",
                                    "События ввода не записаны. Действия не изменены."));
            updateButtons();
            return;
        }
        const auto converted = clicknova::recording::convert(samples);
        if (!converted.error.empty()) {
            QMessageBox::warning(this, QStringLiteral("ClickNova"),
                trText("Cannot import recording: ", "Не удалось импортировать запись: ") + QString::fromStdString(converted.error));
            updateButtons();
            return;
        }
        RecordingReviewDialog review(converted.plan.actions, samples.size(),
                                     durationMs, russian_, this, converted.windows);
        if (review.exec() != QDialog::Accepted || review.decision() == RecordingReviewDialog::Decision::Cancel) {
            reportStatus(trText("Recording not imported. Recovery copy retained.",
                                    "Запись не импортирована. Резервная копия сохранена."));
            updateButtons();
            return;
        }
        std::vector<clicknova::input::Action> updated;
        std::vector<StepWindow> updatedWindows;
        auto importedWindows = std::vector<StepWindow>{};
        for (const auto& origin : review.windows()) {
            StepWindow binding;
            binding.title = QString::fromStdU16String(origin.title).toUtf8().toStdString();
            if (!binding.title.empty())
                binding.process = QString::fromStdU16String(origin.process).toUtf8().toStdString();
            // A hovered window (including the desktop) is not necessarily the
            // foreground window. Retain its identity for review/editing without
            // making it a mandatory playback target. Manually chosen windows
            // and older required bindings remain enforced.
            if (!binding.title.empty()) binding.enforce = false;
            importedWindows.push_back(std::move(binding));
        }
        if (importedWindows.size() != review.actions().size())
            importedWindows.resize(review.actions().size());
        if (review.decision() == RecordingReviewDialog::Decision::Replace) {
            updated = review.actions();
            updatedWindows = importedWindows;
        } else {
            updated = actions_;
            updatedWindows = stepWindows_;
            updatedWindows.insert(updatedWindows.end(), importedWindows.begin(), importedWindows.end());
            updated.insert(updated.end(), review.actions().begin(), review.actions().end());
        }
        if (passMilliseconds() < 0) {
            QMessageBox::warning(this, QStringLiteral("ClickNova"),
                trText("Invalid pause between passes.", "Неверная пауза между проходами."));
            updateButtons();
            return;
        }
        clicknova::input::Plan plan{updated, repetitions_->value(), passMilliseconds(),
            requireWindowBinding_, updatedWindows};
        const auto problem = clicknova::input::validate(plan);
        if (!problem.empty()) {
            QMessageBox::warning(this, QStringLiteral("ClickNova"),
                trText("Cannot add recording: ", "Нельзя добавить запись: ") + QString::fromStdString(problem));
            updateButtons();
            return;
        }
        checkpointActions();
        actions_ = std::move(updated);
        stepWindows_ = std::move(updatedWindows);
        refreshTable(0);
        reportStatus(trText("Imported recorded actions: ", "Записанные действия добавлены: ") +
                         QString::number(review.actions().size()));
        updateButtons();
        // Import is committed to the draft before deleting its recovery copy.
        const bool saved = autosaveEnabled_ && writeAutosave();
        if (review.saveRequested()) savePlan();
        if (saved) clicknova::gui::recording_checkpoint::remove(checkpointFile);
        else reportStatus(trText("Actions imported, but autosave failed or is disabled. Recording recovery retained.",
                                 "Шаги импортированы, но автосохранение не выполнено. Резервная запись сохранена."));
        refreshRecoveryAvailability();
 }

void InputLabWidget::replaceActions(const clicknova::input::Plan& plan) {
    if (isRunning() || !clicknova::input::validate(plan).empty()) return;
    // Assign only after full validation; no disk write is performed.
    // Imported screen actions must not silently inherit a different target.
    targetWindow_.reset();
    requireWindowBinding_ = plan.requireTargetWindow;
    windowStatus_->setText(requireWindowBinding_ ?
        trText("Choose the required window before running", "Выберите необходимое окно перед запуском") :
        trText("Any foreground application", "Любое активное приложение"));
    actionHistory_.clear();
    actions_ = plan.actions;
    stepWindows_ = plan.stepWindows;
    normalizeStepWindows();
    repetitions_->setValue(plan.repetitions);
    passDelay_->setText(QString::number(plan.passDelayMs));
    refreshTable(actions_.empty() ? -1 : 0);
    reportStatus(trText("Imported actions. Autosave is enabled.",
                            "Действия перенесены. Автосохранение включено."));
}

void InputLabWidget::refreshWaitPreview() {
    if (!waitNextAt_) return;
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime at(now.date(), waitClock_->time());
    if (at <= now) at = QDateTime(now.date().addDays(1), waitClock_->time());
    waitClockPreview_->setText(trText("Next step will start: ", "Следующий шаг начнётся: ") +
                               at.toString(QStringLiteral("dd.MM.yyyy HH:mm:ss")));
    const auto ms = waitMilliseconds();
    waitNextAt_->setText(ms < 0 ?
        trText("Invalid duration expression or 64-bit overflow.", "Неверное выражение длительности или переполнение 64 бит.") :
        trText("If waiting begins now, the next step starts at: ",
               "Если ожидание начнётся сейчас, следующий шаг начнётся: ") +
            predictedLocalTime(ms, russian_));
}

void InputLabWidget::clearNextStepTime() {
    nextStepPending_ = false;
    waitingForFirst_ = false;
    if (nextStepTimer_) nextStepTimer_->stop();
    refreshNextStepTime();
}

void InputLabWidget::onPlaybackProgress(const clicknova::input::PlaybackProgress& progress) {
    if (progress.kind == clicknova::input::PlaybackProgress::Kind::ActionStarted) {
        nextStepPending_ = false;
        upcomingStep_ = static_cast<int>(progress.stepIndex) + 1;
        upcomingPass_ = progress.passNumber;
    } else {
        nextStepPending_ = true;
        nextStepDelayMs_ = progress.waitMs;
        nextStepStarted_ = progress.startedAt;
        upcomingStep_ = static_cast<int>(progress.stepIndex) + 1;
        upcomingPass_ = progress.passNumber;
    }
    refreshNextStepTime();
}

void InputLabWidget::refreshNextStepTime() {
    if (!nextStepTime_) return;
    if (!nextStepPending_) {
        if (running_) nextStepTime_->setText(
            trText("Executing step %1 · pass %2. The next start time will appear when a wait begins.",
                   "Выполняется шаг %1 · проход %2. Время следующего шага появится при ожидании.")
                .arg(upcomingStep_).arg(upcomingPass_));
        else nextStepTime_->setText(trText(
            "Next step: no active wait. During playback the countdown and local clock time appear here.",
            "Следующий шаг: ожидания нет. При выполнении здесь будут обратный отсчёт и время по часам Windows."));
        return;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - nextStepStarted_).count();
    const auto remaining = nextStepDelayMs_ - std::min(nextStepDelayMs_, std::max<std::int64_t>(0, elapsed));
    const QString target = waitingForFirst_ ? trText("First step", "Первый шаг") :
        upcomingPass_ > repetitions_->value() ? trText("Scenario completion", "Завершение сценария") :
        trText("Step %1 · pass %2", "Шаг %1 · проход %2").arg(upcomingStep_).arg(upcomingPass_);
    nextStepTime_->setText(trText("%1 starts at %2 · remaining %3",
        "%1 начнётся в %2 · осталось %3")
            .arg(target, predictedLocalTime(remaining, russian_), readableRemaining(remaining, russian_)));
}

void InputLabWidget::beginPlayback() {
    if (isRunning()) return;
    if (canBegin_ && !canBegin_()) {
        reportStatus(trText("Stop the other automation or point capture first.",
                                "Сначала остановите другую автоматизацию или захват точки."));
        return;
    }
    if (!numericInputsValid() || passMilliseconds() < 0) {
        reportStatus(trText("Invalid count or duration (64-bit overflow).",
            "Неверное количество или длительность (переполнение 64-битного числа)."));
        return;
    }
    Plan plan{actions_, repetitions_->value(), passMilliseconds(), requireWindowBinding_, stepWindows_};
    const std::string error = validate(plan);
    if (!error.empty()) {
        reportStatus(trText("Cannot run: ", "Нельзя запустить: ") + QString::fromStdString(error));
        return;
    }
    if (std::any_of(actions_.begin(), actions_.end(), [](const Action& a) {
            return std::holds_alternative<RunScript>(a);
        }) && QMessageBox::question(this, QStringLiteral("ClickNova"),
            trText("This scenario contains LOCAL PowerShell code with your Windows user permissions. Run it now? Only run scripts you trust.",
                   "Сценарий содержит ЛОКАЛЬНЫЙ код PowerShell с правами вашей учётной записи Windows. Выполнить? Запускайте только доверенный код."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    if (!targetWindow_ && requireWindowBinding_) {
        reportStatus(trText("This scenario requires a target window. Select it again before playback.",
                                "Для этого сценария нужно окно. Выберите его заново перед запуском."));
        return;
    }
    if (!targetWindow_ && [&] {
            for (std::size_t i = 0; i < actions_.size(); ++i) {
                const auto* move = std::get_if<MovePointer>(&actions_[i]);
                if (move && move->mode == MoveMode::WindowClient &&
                    (i >= stepWindows_.size() || stepWindows_[i].title.empty())) return true;
            }
            return false;
        }()) {
        reportStatus(trText("Choose the target window before running window-client coordinates.",
                                "Сначала выберите окно для шагов с координатами внутри окна."));
        return;
    }
#ifndef _WIN32
    reportStatus(trText("Input playback is currently available only on Windows.", "Воспроизведение ввода пока доступно только на Windows."));
    return;
#else
    if (!stopHotkeyController_ || !stopHotkeyController_(true)) {
        reportStatus(trText("Cannot reserve the Stop shortcut. Choose a free Stop key in Settings.",
            "Не удалось зарегистрировать клавишу остановки. Выберите свободную клавишу в настройках."));
        return;
    }
    if (autosaveTimer_->isActive()) writeAutosave();
    stopHotkeyActive_ = true;
    ++playbackGeneration_;
    countdown_ = true;
    waitingForFirst_ = true;
    nextStepPending_ = true;
    nextStepDelayMs_ = 3000;
    nextStepStarted_ = std::chrono::steady_clock::now();
    upcomingStep_ = 1;
    upcomingPass_ = 1;
    nextStepTimer_->start();
    refreshNextStepTime();
    const QString stopKey = stopHotkeyLabel_ ? stopHotkeyLabel_() : QStringLiteral("Stop");
    reportStatus(trText("Starting in 3 seconds. Switch to the target app. Press %1 to stop.",
                            "Старт через 3 секунды. Переключитесь в нужное приложение. Для остановки нажмите %1.").arg(stopKey));
    updateButtons();
    // The global stop hotkey remains registered throughout the countdown and playback.
    // The window is minimized so the app itself cannot intercept the injected keys.
    window()->showMinimized();
    const auto generation = playbackGeneration_;
    QTimer::singleShot(3000, this, [this, generation] {
        if (countdown_ && playbackGeneration_ == generation) startPlayback();
    });
#endif
}

void InputLabWidget::startPlayback() {
#ifdef _WIN32
    if (targetWindow_ && clicknova::platform::checkTarget(*targetWindow_) != clicknova::platform::TargetCheck::Ready) {
        countdown_ = false;
        clearNextStepTime();
        if (stopHotkeyActive_ && stopHotkeyController_) stopHotkeyController_(false);
        stopHotkeyActive_ = false;
        window()->showNormal();
        reportStatus(trText("Selected window is no longer active or available. Activate it and try again.",
                                "Выбранное окно неактивно или недоступно. Активируйте его и повторите попытку."));
        updateButtons();
        return;
    }
#endif
    countdown_ = false;
    running_ = true;
    waitingForFirst_ = false;
    nextStepPending_ = false;
    refreshNextStepTime();
    updateButtons();
    const Plan plan{actions_, repetitions_->value(), passMilliseconds(), requireWindowBinding_, stepWindows_};
    if (worker_.joinable()) worker_.join();
    const auto transientTarget = targetWindow_;
    const bool keepWindowsAwake = keepAwake_->isChecked();
    const auto generation = playbackGeneration_;
    worker_ = std::jthread([this, plan, transientTarget, keepWindowsAwake, generation](std::stop_token token) {
#ifdef _WIN32
        ScriptBackend backend(transientTarget);
        clicknova::platform::WindowsAwakeGuard awake(keepWindowsAwake);
        SteadyWaiter waiter;
        const Result result = play(plan, backend, waiter, token,
            [this, generation](const clicknova::input::PlaybackProgress& event) {
                QMetaObject::invokeMethod(this, [this, generation, event] {
                    if (generation == playbackGeneration_ && running_)
                        onPlaybackProgress(event);
                }, Qt::QueuedConnection);
            });
#else
        const Result result{};
#endif
        QMetaObject::invokeMethod(this, [this, result] { finishPlayback(result); }, Qt::QueuedConnection);
    });
}

void InputLabWidget::stopPlayback() {
    if (countdown_) {
        countdown_ = false;
        ++playbackGeneration_; // invalidate the old three-second singleShot
        clearNextStepTime();
        if (stopHotkeyActive_ && stopHotkeyController_) stopHotkeyController_(false);
        stopHotkeyActive_ = false;
        window()->showNormal();
        reportStatus(trText("Cancelled before starting.", "Отменено до запуска."));
    }
    if (running_) {
        ++playbackGeneration_; // queued progress from the worker must not resurrect the ETA
        clearNextStepTime();
        worker_.request_stop();
        reportStatus(trText("Stopping…", "Остановка…"));
    }
    updateButtons();
}

void InputLabWidget::finishPlayback(const Result& result) {
    running_ = false;
    clearNextStepTime();
    if (worker_.joinable()) worker_.join();
    window()->showNormal();
    window()->activateWindow();
    if (stopHotkeyActive_ && stopHotkeyController_) stopHotkeyController_(false);
    stopHotkeyActive_ = false;
    QString outcome;
    switch (result.status) {
    case Status::Completed: outcome = trText("Completed", "Завершено"); break;
    case Status::Cancelled: outcome = trText("Stopped", "Остановлено"); break;
    case Status::InvalidPlan: outcome = trText("Invalid action list", "Неверный список действий"); break;
    case Status::DispatchFailed:
        outcome = targetWindow_
            ? trText("Target window lost focus, the point is blocked, or input failed",
                     "Окно потеряло фокус, точка перекрыта или ввод не удался")
            : trText("Input dispatch failed", "Ошибка отправки ввода");
        break;
    }
    if (result.status == Status::DispatchFailed &&
        result.message.find("step window") != std::string::npos) {
        outcome = trText("Step window missing, inactive or activation was denied",
            "Окно шага не найдено, неактивно или Windows запретила переключение");
    }
    if (result.status == Status::DispatchFailed &&
        result.message.find("PowerShell") != std::string::npos) {
        outcome = trText("PowerShell step failed", "Ошибка выполнения шага PowerShell");
    }
    reportStatus(QStringLiteral("%1 · %2: %3 · %4: %5")
        .arg(outcome, trText("Actions", "Действий"))
        .arg(result.actionsCompleted)
        .arg(trText("Events", "Событий"))
        .arg(result.eventsSent));
    updateButtons();
}

namespace {
QString autosavePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
        QStringLiteral("/clicknova-steps-autosave.json");
}
}

void InputLabWidget::scheduleAutosave() {
    if (autosaveEnabled_ && !restoringDraft_ && autosaveTimer_ && !isRunning()) autosaveTimer_->start();
}

bool InputLabWidget::writeAutosave() {
    if (!autosaveEnabled_ || restoringDraft_ || isRunning() || !autosaveTimer_) return false;
    autosaveTimer_->stop();
    const auto delay = passMilliseconds();
    // Preserve even incomplete/unbalanced actions; playback validates separately.
    QJsonObject draft = QJsonObject::fromVariantMap(clicknova::storage::encodeInputPlan(
        Plan{actions_, repetitions_->value(), std::max<std::int64_t>(0, delay), requireWindowBinding_, stepWindows_}));
    draft[QStringLiteral("draftVersion")] = 1;
    draft[QStringLiteral("passDelayInput")] = passDelay_->text();
    const QByteArray bytes = QJsonDocument(draft).toJson(QJsonDocument::Compact);
    const QString path = autosavePath();
    if (bytes.size() > 1024 * 1024 || !QDir().mkpath(QFileInfo(path).absolutePath())) {
        autosaveStatus_->setText(trText("Autosave failed: draft is too large.",
            "Автосохранение: ошибка, черновик слишком большой."));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        autosaveStatus_->setText(trText("Autosave failed: cannot write file.",
            "Автосохранение: не удалось записать файл."));
        return false;
    }
    autosaveStatus_->setText(trText("Autosave: saved locally", "Автосохранение: сохранено локально"));
    return true;
}

void InputLabWidget::restoreAutosave() {
    if (!autosaveEnabled_) return;
    QFile file(autosavePath());
    if (!file.exists() || file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly)) return;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return;
    const auto draft = doc.object();
    if (draft.value(QStringLiteral("draftVersion")).toInt() != 1 ||
        !draft.value(QStringLiteral("actions")).isArray()) return;
    const auto array = draft.value(QStringLiteral("actions")).toArray();
    if (array.size() > 512) return;
    std::vector<Action> restored;
    std::vector<StepWindow> restoredWindows;
    for (const auto& entry : array) {
        if (!entry.isObject()) return;
        const auto action = clicknova::storage::decodeInputAction(entry.toObject());
        const auto binding = clicknova::storage::decodeStepWindow(entry.toObject());
        if (!action || !binding) return;
        restored.push_back(*action);
        restoredWindows.push_back(*binding);
    }
    const int repetitions = draft.value(QStringLiteral("repetitions")).toInt();
    if (repetitions < 1) return;
    const auto delay = draft.value(QStringLiteral("passDelayInput")).toString();
    restoringDraft_ = true;
    actions_ = std::move(restored);
    stepWindows_ = std::move(restoredWindows);
    requireWindowBinding_ = draft.value(QStringLiteral("requireTargetWindow")).toBool();
    targetWindow_.reset(); // Never reuse stale HWND from a previous run.
    repetitions_->setValue(repetitions);
    passDelay_->setText(delay);
    refreshTable(actions_.empty() ? -1 : 0);
    actionHistory_.clear();
    restoringDraft_ = false;
    autosaveTimer_->stop();
    autosaveStatus_->setText(trText("Autosave: previous draft restored", "Автосохранение: черновик восстановлен"));
    reportStatus(trText("Previous draft restored. Review actions before running.",
        "Предыдущий черновик восстановлен. Проверьте шаги перед запуском."));
}

void InputLabWidget::refreshSavedPlans(const QString& selected) {
    const QString previous = selected.isEmpty() ? savedPlans_->currentData(Qt::UserRole + 1).toString() : selected;
    const int previousKind = savedPlans_->currentData(Qt::UserRole).toInt();
    QSignalBlocker blocker(savedPlans_);
    savedPlans_->clear();
    savedPlans_->addItem(trText("Choose a saved scenario…", "Выберите сохранённый сценарий…"));
    // All supported formats appear in ONE picker without changing or deleting
    // the original legacy files. A converted scenario is saved as InputLab.
    for (const auto kind : {clicknova::storage::Kind::InputLab}) {
        const QString prefix = kind == clicknova::storage::Kind::InputLab
            ? trText("Steps", "Шаги") : kind == clicknova::storage::Kind::Sequence
            ? trText("Old clicks", "Старые клики") : trText("Auto clicker", "Автокликер");
        for (const auto& name : scenarioStore_.names(kind)) {
            savedPlans_->addItem(QStringLiteral("[%1] %2").arg(prefix, name));
            savedPlans_->setItemData(savedPlans_->count() - 1, static_cast<int>(kind));
            savedPlans_->setItemData(savedPlans_->count() - 1, name, Qt::UserRole + 1);
        }
    }
    for (int i = 1; i < savedPlans_->count(); ++i) {
        if (savedPlans_->itemData(i, Qt::UserRole + 1).toString() == previous &&
            (selected.isEmpty() ? savedPlans_->itemData(i).toInt() == previousKind :
                savedPlans_->itemData(i).toInt() == static_cast<int>(clicknova::storage::Kind::InputLab))) {
            savedPlans_->setCurrentIndex(i);
            break;
        }
    }
    updateButtons();
}

void InputLabWidget::savePlan() {
    if (isRunning() || actions_.empty()) return;
    if (!numericInputsValid() || passMilliseconds() < 0) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"),
            trText("Invalid click count or duration.", "Неверное количество кликов или длительность."));
        return;
    }
    const Plan plan{actions_, repetitions_->value(), passMilliseconds(), requireWindowBinding_, stepWindows_};
    const auto problem = validate(plan);
    if (!problem.empty()) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"),
            trText("Cannot save invalid plan: ", "Нельзя сохранить некорректный сценарий: ") +
            QString::fromStdString(problem));
        return;
    }
    const QString suggestion = savedPlans_->currentIndex() > 0 &&
        savedPlans_->currentData(Qt::UserRole).toInt() == static_cast<int>(clicknova::storage::Kind::InputLab)
        ? savedPlans_->currentData(Qt::UserRole + 1).toString() : QString();
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("ClickNova"),
        trText("Scenario name", "Название сценария"), QLineEdit::Normal,
        suggestion, &accepted).trimmed();
    if (!accepted) return;
    if (!clicknova::storage::ScenarioStore::validName(name)) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"), trText(
            "Use 1–40 letters, numbers, spaces, underscores or hyphens. Start with a letter or number.",
            "Введите 1–40 символов: буквы, цифры, пробелы, _ или -. Начните с буквы или цифры."));
        return;
    }
    if (scenarioStore_.names(clicknova::storage::Kind::InputLab).contains(name) &&
        QMessageBox::question(this, QStringLiteral("ClickNova"),
            trText("Replace the saved plan?", "Заменить сохранённый сценарий?")) != QMessageBox::Yes) return;
    QString error;
    if (!scenarioStore_.save(clicknova::storage::Kind::InputLab, name,
                             clicknova::storage::encodeInputPlan(plan), &error)) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"), error);
        return;
    }
    refreshSavedPlans(name);
    reportStatus(trText("Saved plan: ", "Сценарий сохранён: ") + name);
}

void InputLabWidget::loadPlan() {
    if (isRunning() || savedPlans_->currentIndex() <= 0) return;
    const QString name = savedPlans_->currentData(Qt::UserRole + 1).toString();
    const auto kind = static_cast<clicknova::storage::Kind>(savedPlans_->currentData(Qt::UserRole).toInt());
    if (kind != clicknova::storage::Kind::InputLab) {
        loadLegacyPlan(name, kind);
        return;
    }
    QString error;
    const auto bytes = scenarioStore_.load(kind, name, &error);
    if (!bytes) { QMessageBox::warning(this, QStringLiteral("ClickNova"), error); return; }
    const auto restored = clicknova::storage::decodeInputPlan(*bytes, &error);
    if (!restored) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"),
            trText("Cannot load scenario: ", "Нельзя загрузить сценарий: ") + error);
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("ClickNova"),
        trText("Replace the current action list with this saved scenario?",
               "Заменить текущие действия сохранённым сценарием?")) != QMessageBox::Yes) return;
    replaceActions(*restored);
    reportStatus(trText("Loaded scenario: ", "Сценарий загружен: ") + name);
}

void InputLabWidget::loadLegacyPlan(const QString& name, clicknova::storage::Kind kind) {
    QString error;
    const auto bytes = scenarioStore_.load(kind, name, &error);
    if (!bytes) { QMessageBox::warning(this, QStringLiteral("ClickNova"), error); return; }
    Plan plan;
    if (kind == clicknova::storage::Kind::Sequence) {
        clicknova::storage::SequenceScenario old;
        if (!clicknova::storage::decodeScenario(*bytes, old, &error)) {
            QMessageBox::warning(this, QStringLiteral("ClickNova"), error); return;
        }
        int count = old.repeatCount;
        if (old.unlimited) {
            bool ok = false;
            count = QInputDialog::getInt(this, QStringLiteral("ClickNova"),
                trText("This old scenario repeats indefinitely. Choose a finite repeat count:",
                       "Старый сценарий повторялся бесконечно. Выберите конечное число повторов:"),
                1, 1, std::numeric_limits<int>::max(), 1, &ok);
            if (!ok) return;
        }
        clicknova::sequence::Plan oldPlan{old.steps, count, false, old.passDelayMs};
        std::string reason;
        const auto result = clicknova::sequence::toInputPlan(oldPlan, &reason);
        if (!result) { QMessageBox::warning(this, QStringLiteral("ClickNova"), QString::fromStdString(reason)); return; }
        plan = *result;
    } else if (kind == clicknova::storage::Kind::AutoClicker) {
        clicknova::storage::AutoClickerScenario old;
        if (!clicknova::storage::decodeScenario(*bytes, old, &error)) {
            QMessageBox::warning(this, QStringLiteral("ClickNova"), error); return;
        }
        if (old.actionMode == 3) {
            QMessageBox::warning(this, QStringLiteral("ClickNova"), trText(
                "Hold-until-Stop has no finite equivalent. Add a Mouse Hold action manually.",
                "У удержания до команды Стоп нет конечного аналога. Добавьте действие «Удержание мыши» вручную."));
            return;
        }
        int count = old.count;
        if (old.unlimited) {
            bool ok = false;
            count = QInputDialog::getInt(this, QStringLiteral("ClickNova"),
                trText("Select a finite repeat count for this auto-clicker scenario:",
                       "Выберите конечное число повторов автокликера:"), 1, 1, std::numeric_limits<int>::max(), 1, &ok);
            if (!ok) return;
        }
        plan.repetitions = count;
        plan.passDelayMs = old.intervalMs;
        plan.requireTargetWindow = old.bound;
        const auto button = static_cast<Button>(old.button);
        const auto mode = old.bound ? MoveMode::WindowClient : MoveMode::Absolute;
        const QPoint point = old.bound ? old.client : old.screen;
        if (old.fixed) plan.actions.emplace_back(MovePointer{mode, point.x(), point.y(), 0});
        if (old.actionMode == 0) {
            if (old.holdDurationMs > 0) plan.actions.emplace_back(MouseHold{button, old.holdDurationMs});
            else plan.actions.emplace_back(Click{button, 1, 0});
        } else {
            if (!old.destinationConfigured) {
                QMessageBox::warning(this, QStringLiteral("ClickNova"), trText(
                    "A destination must be set before importing this motion.",
                    "Перед переносом перемещения требуется конечная точка."));
                return;
            }
            if (old.actionMode == 2) plan.actions.emplace_back(ButtonDown{button});
            plan.actions.emplace_back(MovePointer{mode, old.destinationX, old.destinationY, old.movementMs});
            if (old.actionMode == 2) plan.actions.emplace_back(ButtonUp{button});
        }
    } else return;
    const auto problem = validate(plan);
    if (!problem.empty()) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"),
            trText("Cannot import safely: ", "Нельзя безопасно перенести: ") + QString::fromStdString(problem));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("ClickNova"), trText(
        "Import this old scenario into the common action list? The original saved file will remain unchanged.",
        "Перенести старый сценарий в общий список действий? Исходный сохранённый файл останется без изменений.")) != QMessageBox::Yes) return;
    replaceActions(plan);
    reportStatus(trText("Old scenario imported. Review positions, then select its active window if required.",
        "Старый сценарий перенесён. Проверьте координаты и при необходимости выберите активное окно."));
}

void InputLabWidget::deletePlan() {
    if (isRunning() || savedPlans_->currentIndex() <= 0) return;
    const QString name = savedPlans_->currentData(Qt::UserRole + 1).toString();
    if (savedPlans_->currentData(Qt::UserRole).toInt() != static_cast<int>(clicknova::storage::Kind::InputLab)) return;
    if (QMessageBox::question(this, QStringLiteral("ClickNova"),
        trText("Delete the saved plan? Current actions will not change.",
               "Удалить сохранённый сценарий? Текущие действия останутся без изменений.")) != QMessageBox::Yes) return;
    if (!scenarioStore_.remove(clicknova::storage::Kind::InputLab, name)) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"),
            trText("Could not delete this plan.", "Не удалось удалить сценарий."));
        return;
    }
    refreshSavedPlans();
    reportStatus(trText("Deleted plan: ", "Сценарий удалён: ") + name);
}
