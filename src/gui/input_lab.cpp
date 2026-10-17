// SPDX-License-Identifier: Apache-2.0
#include "input_lab.hpp"
#include "browser_discovery.hpp"
#include "browser_extension_bridge.hpp"
#include "action_table_widget.hpp"
#include "clicknova/action_list_edit.hpp"
#include "clicknova/action_step_groups.hpp"
#include "clicknova/duration_expression.hpp"
#include "input_capture_dialog.hpp"
#include "input_recorder_dialog.hpp"
#include "recording_checkpoint.hpp"
#include "recording_review_dialog.hpp"
#include "window_chooser.hpp"
#include "wrap_layout.hpp"
#include "ui_icons.hpp"
#include "portable_storage.hpp"
#include <QCryptographicHash>
#include "clicknova/input_recording.hpp"
#include "clicknova/ui_element_search.hpp"
#include "clicknova/visual_recognition.hpp"
#include "../storage/input_plan_codec.hpp"
#include "../storage/scenario_codec.hpp"
#include "clicknova/sequence_to_input.hpp"
#include "clicknova/windows_awake.hpp"
#ifdef _WIN32
#include "clicknova/windows_input_backend.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <QComboBox>
#include <QCursor>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QBuffer>
#include <QImageReader>
#include <QHash>
#include <QSet>
#include <QQueue>
#include <QCheckBox>
#include <QRegularExpressionValidator>
#include <QRegularExpression>
#include <QSettings>
#include <QDateTime>
#include <QUuid>
#include <QTimeEdit>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QStandardItemModel>
#include <QSaveFile>
#include <QDir>
#include <QStandardPaths>
#include "portable_storage.hpp"
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <limits>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QThread>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QSplitter>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
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
#include <memory>
#include <functional>

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
            auto found = clicknova::platform::findWindow(title, process);
            // A browser window title often changes with the active tab. If an
            // explicit tab was saved, allow a unique process-name match only.
            // Several windows with the same executable remain ambiguous: fail.
            if (!found && !step.elementTab.empty() &&
                !process.empty()) {
                std::optional<clicknova::platform::WindowTarget> unique;
                bool ambiguous = false;
                for (const auto& candidate : clicknova::platform::listWindows()) {
                    if (QString::compare(QString::fromStdWString(candidate.process),
                        QString::fromStdWString(process), Qt::CaseInsensitive) != 0) continue;
                    if (unique) { ambiguous = true; break; }
                    unique = candidate;
                }
                if (!ambiguous) found = std::move(unique);
            }
            if (found) {
                // Only explicit native-provider element steps may run without
                // activating their window. Raw input and visual OCR/OpenCV
                // always go through the foreground input/visibility guard.
                const bool background = clicknova::input::backgroundElementAction(step);
                if ((background && clicknova::platform::checkAccessibleTarget(*found) ==
                         clicknova::platform::TargetCheck::Ready) ||
                    (!background && !step.activate &&
                         clicknova::platform::checkTarget(*found) == clicknova::platform::TargetCheck::Ready) ||
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
    bool invokeElement(const clicknova::input::StepWindow& binding, std::stop_token stop) override {
        if (stop.stop_requested() || !native_.selectedWindow()) return false;
        if (!binding.elementTab.empty() &&
            !clicknova::accessibility::selectBrowserTab(*native_.selectedWindow(),
                QString::fromStdString(binding.elementTab).toStdWString())) return false;
        const bool invoked = clicknova::accessibility::perform(*native_.selectedWindow(),
            QString::fromStdString(binding.elementName).toStdWString(),
            QString::fromStdString(binding.elementAutomationId).toStdWString(),
            QString::fromStdString(binding.elementClass).toStdWString(),
            static_cast<clicknova::accessibility::Role>(binding.elementRole), binding.elementBrowser,
            static_cast<clicknova::accessibility::Operation>(binding.elementOperation),
            QString::fromStdString(binding.elementValue).toStdWString(),
            static_cast<clicknova::accessibility::Provider>(binding.elementProvider),
            clicknova::input::backgroundElementAction(binding));
        return invoked && !stop.stop_requested();
    }
    bool doubleClickElement(const clicknova::input::StepWindow& binding,
                            int intervalMs, std::stop_token stop) override {
        if (stop.stop_requested() || !native_.selectedWindow() || !binding.activate ||
            !binding.elementTab.empty()) return false;
        const auto ok = clicknova::accessibility::pointerGesture(*native_.selectedWindow(),
            QString::fromStdString(binding.elementName).toStdWString(),
            QString::fromStdString(binding.elementAutomationId).toStdWString(),
            QString::fromStdString(binding.elementClass).toStdWString(),
            static_cast<clicknova::accessibility::Role>(binding.elementRole),
            static_cast<clicknova::accessibility::Provider>(binding.elementProvider),
            clicknova::accessibility::PointerGesture::DoubleClick, false, 0,
            intervalMs, stop);
        return ok && !stop.stop_requested();
    }
    bool elementPresent(const clicknova::input::StepWindow& binding, std::stop_token stop) override {
        if (stop.stop_requested() || !native_.selectedWindow()) return false;
        if (!binding.elementTab.empty() &&
            !clicknova::accessibility::selectBrowserTab(*native_.selectedWindow(),
                QString::fromStdString(binding.elementTab).toStdWString())) return false;
        return clicknova::accessibility::present(*native_.selectedWindow(),
            QString::fromStdString(binding.elementName).toStdWString(),
            QString::fromStdString(binding.elementAutomationId).toStdWString(),
            QString::fromStdString(binding.elementClass).toStdWString(),
            static_cast<clicknova::accessibility::Role>(binding.elementRole), binding.elementBrowser,
            static_cast<clicknova::accessibility::Provider>(binding.elementProvider),
            clicknova::input::backgroundElementAction(binding)) && !stop.stop_requested();
    }
    bool scrollElement(const clicknova::input::StepWindow& binding,
                       clicknova::input::Axis axis, int delta, std::stop_token stop) override {
        if (stop.stop_requested() || !native_.selectedWindow()) return false;
        if (binding.elementProvider != 0) {
            if (!binding.activate || !binding.elementTab.empty()) return false;
            return clicknova::accessibility::pointerGesture(*native_.selectedWindow(),
                QString::fromStdString(binding.elementName).toStdWString(),
                QString::fromStdString(binding.elementAutomationId).toStdWString(),
                QString::fromStdString(binding.elementClass).toStdWString(),
                static_cast<clicknova::accessibility::Role>(binding.elementRole),
                static_cast<clicknova::accessibility::Provider>(binding.elementProvider),
                clicknova::accessibility::PointerGesture::Wheel,
                axis == clicknova::input::Axis::Horizontal, delta, 0, stop);
        }
        const QString amount = QStringLiteral("%1:%2")
            .arg(axis == clicknova::input::Axis::Vertical ? QStringLiteral("v") : QStringLiteral("h"))
            .arg(delta);
        const auto& target = *native_.selectedWindow();
        const auto name = QString::fromStdString(binding.elementName).toStdWString();
        const auto id = QString::fromStdString(binding.elementAutomationId).toStdWString();
        const auto cls = QString::fromStdString(binding.elementClass).toStdWString();
        const auto role = static_cast<clicknova::accessibility::Role>(binding.elementRole);
        if (clicknova::accessibility::perform(target, name, id, cls, role,
            binding.elementBrowser, clicknova::accessibility::Operation::Scroll,
            amount.toStdWString(), clicknova::accessibility::Provider::Uia,
            clicknova::input::backgroundElementAction(binding))) return !stop.stop_requested();
        // Explicit Scroll step in a foreground window: if the element has no
        // ScrollPattern, use its re-resolved visible rectangle for a wheel.
        // Never use the desktop cursor position or do this in background.
        return !stop.stop_requested() && binding.activate && binding.elementTab.empty() &&
            clicknova::accessibility::pointerGesture(target, name, id, cls, role,
                clicknova::accessibility::Provider::Uia,
                clicknova::accessibility::PointerGesture::Wheel,
                axis == clicknova::input::Axis::Horizontal, delta, 0, stop);
    }
    bool setElementText(const clicknova::input::StepWindow& binding,
                        const std::u16string& value, std::stop_token stop) override {
        if (stop.stop_requested() || !native_.selectedWindow()) return false;
        if (binding.elementProvider >= 3) {
            if (!binding.activate || !binding.elementTab.empty() || value.empty()) return false;
            // User explicitly selected visual text entry. Click the UNIQUE fresh
            // visual match before typing; this is foreground keyboard input,
            // never a claim that OCR exposes a native editable field.
            if (!clicknova::accessibility::perform(*native_.selectedWindow(),
                QString::fromStdString(binding.elementName).toStdWString(),
                QString::fromStdString(binding.elementAutomationId).toStdWString(),
                QString::fromStdString(binding.elementClass).toStdWString(),
                static_cast<clicknova::accessibility::Role>(binding.elementRole),
                false, clicknova::accessibility::Operation::Invoke, L"",
                static_cast<clicknova::accessibility::Provider>(binding.elementProvider), false)) return false;
            for (char16_t unit : value) {
                if (stop.stop_requested() || clicknova::platform::checkTarget(*native_.selectedWindow()) !=
                    clicknova::platform::TargetCheck::Ready) return false;
                clicknova::input::Event ev{};
                ev.kind = clicknova::input::EventKind::UnicodePress;
                ev.unicodeUnit = unit;
                if (!native_.dispatch(ev)) return false;
                ev.kind = clicknova::input::EventKind::UnicodeRelease;
                if (!native_.dispatch(ev)) return false;
            }
            return true;
        }
        if (!binding.elementTab.empty() &&
            !clicknova::accessibility::selectBrowserTab(*native_.selectedWindow(),
                QString::fromStdString(binding.elementTab).toStdWString())) return false;
        return clicknova::accessibility::perform(*native_.selectedWindow(),
            QString::fromStdString(binding.elementName).toStdWString(),
            QString::fromStdString(binding.elementAutomationId).toStdWString(),
            QString::fromStdString(binding.elementClass).toStdWString(),
            static_cast<clicknova::accessibility::Role>(binding.elementRole), binding.elementBrowser,
            clicknova::accessibility::Operation::SetValue,
            QString::fromStdU16String(value).toStdWString(),
            static_cast<clicknova::accessibility::Provider>(binding.elementProvider),
            clicknova::input::backgroundElementAction(binding)) && !stop.stop_requested();
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
// Executable icons come from Windows itself. Site entries use a neutral web
// icon: UIA exposes tab names, not reliable favicons or URLs.
QIcon searchWindowIcon(const clicknova::platform::WindowTarget& window) {
#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, window.processId);
    if (process) {
        wchar_t path[32768]{};
        DWORD size = 32768;
        const bool found = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
        CloseHandle(process);
        if (found) {
            const QIcon icon = QFileIconProvider{}.icon(QFileInfo(QString::fromWCharArray(path,
                static_cast<qsizetype>(size))));
            if (!icon.isNull()) return icon;
        }
    }
#else
    (void)window;
#endif
    return clicknova::gui::icon(clicknova::gui::Icon::Window);
}
QIcon browserSiteIcon() {
    QPixmap canvas(20, 20);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(QStringLiteral("#78b9fa")), 1.6));
    painter.drawEllipse(QRectF(2, 2, 16, 16));
    painter.drawLine(QPointF(2, 10), QPointF(18, 10));
    painter.drawEllipse(QRectF(6, 2, 8, 16));
    return QIcon(canvas);
}
struct UiSearchTarget {
    clicknova::platform::WindowTarget window;
    std::wstring tab; // Empty: the complete application's current accessible tree.
    bool browser{false};
    QString pageUrl; // DevTools URL is display-only; never used as an unsafe UIA window selector.
    bool devToolsOnly{false};
};
// Never display UIA SetValue contents in step summaries or activity logs.
QString uiOperationLabel(int operation, bool russian) {
    constexpr std::array<const char*, 10> en = {
        "Invoke", "Set value", "Check", "Uncheck", "Select", "Expand", "Collapse",
        "Set number", "Focus", "Scroll to"};
    constexpr std::array<const char*, 10> ru = {
        "Нажать", "Ввести", "Включить", "Выключить", "Выбрать", "Раскрыть", "Свернуть",
        "Задать число", "Фокус", "Прокрутить"};
    return operation >= 0 && operation < 10 ?
        QString::fromUtf8(russian ? ru[static_cast<std::size_t>(operation)] :
                                    en[static_cast<std::size_t>(operation)]) : QStringLiteral("?");
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
    // Alternating rows used to look like preselected actions in the dark theme.
    // A row is highlighted only in response to an actual selection.
    table_->setAlternatingRowColors(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(2, 46);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setMinimumHeight(180);
    // The ordered action model stays in the table: grouping never modifies the
    // saved or executed step order, so all existing shortcuts and undo work.
    stepsRootLayout_ = layout;
    stepsPanel_ = new QWidget(this);
    stepsPanel_->setObjectName(QStringLiteral("inputLabStepPanel"));
    auto* panelLayout = new QVBoxLayout(stepsPanel_);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(6);
    stepsHeading_ = new QLabel;
    stepsHeading_->setObjectName(QStringLiteral("inputLabStepsHeading"));
    panelLayout->addWidget(stepsHeading_);
    auto* tools = new QHBoxLayout;
    stepSearch_ = new QLineEdit;
    stepSearch_->setObjectName(QStringLiteral("inputLabStepSearch"));
    stepSearch_->setClearButtonEnabled(true);
    stepFilter_ = new QComboBox;
    stepFilter_->setObjectName(QStringLiteral("inputLabStepFilter"));
    for (int i = 0; i < 7; ++i) stepFilter_->addItem(QString(), i);
    expandGroups_ = new QPushButton;
    expandGroups_->setObjectName(QStringLiteral("inputLabExpandGroups"));
    collapseGroups_ = new QPushButton;
    collapseGroups_->setObjectName(QStringLiteral("inputLabCollapseGroups"));
    shrinkSteps_ = new QPushButton(QStringLiteral("−"));
    shrinkSteps_->setObjectName(QStringLiteral("inputLabShrinkSteps"));
    enlargeSteps_ = new QPushButton(QStringLiteral("+"));
    enlargeSteps_->setObjectName(QStringLiteral("inputLabEnlargeSteps"));
    detachSteps_ = new QPushButton;
    detachSteps_->setObjectName(QStringLiteral("inputLabDetachSteps"));
    tools->addWidget(stepSearch_, 2);
    tools->addWidget(stepFilter_, 1);
    for (auto* button : {expandGroups_, collapseGroups_, shrinkSteps_, enlargeSteps_, detachSteps_})
        tools->addWidget(button);
    panelLayout->addLayout(tools);
    table_->setFixedHeight(attachedHeight_);
    panelLayout->addWidget(table_);
    stepCount_ = new QLabel;
    stepCount_->setObjectName(QStringLiteral("inputLabStepCount"));
    auto* footer = new QHBoxLayout;
    footer->addWidget(stepCount_, 1);
    auto* grip = new QLabel(QStringLiteral("↕"));
    grip->setObjectName(QStringLiteral("inputLabStepResizeGrip"));
    grip->setCursor(Qt::SizeVerCursor);
    grip->setToolTip(QStringLiteral("Drag to resize the steps list"));
    grip->setMinimumWidth(65);
    grip->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grip->installEventFilter(this);
    stepResizeGrip_ = grip;
    footer->addWidget(grip);
    panelLayout->addLayout(footer);
    layout->addWidget(stepsPanel_);
    connect(stepSearch_, &QLineEdit::textChanged, this, [this] { applyStepVisibility(); });
    connect(stepFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { applyStepVisibility(); });
    connect(expandGroups_, &QPushButton::clicked, this, [this] {
        collapsedStepGroups_.clear(); collapsedManualGroups_.clear(); applyStepVisibility();
    });
    connect(collapseGroups_, &QPushButton::clicked, this, [this] {
        collapsedStepGroups_.clear();
        collapsedManualGroups_.clear();
        for (int i = 0; i < static_cast<int>(groupEndByStart_.size()); ++i)
            if (groupEndByStart_[i] > i + 1) collapsedStepGroups_.insert(i);
        for (int i = 0; i < static_cast<int>(manualEndByStart_.size()); ++i)
            if (manualEndByStart_[i] > i)
                collapsedManualGroups_.insert(QString::fromStdString(stepWindows_[i].manualGroupId));
        applyStepVisibility();
    });
    connect(enlargeSteps_, &QPushButton::clicked, this, [this] { resizeStepList(100); });
    connect(shrinkSteps_, &QPushButton::clicked, this, [this] { resizeStepList(-100); });
    connect(detachSteps_, &QPushButton::clicked, this, [this] { detachStepList(); });
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int column) {
        if (column != 0 || row >= static_cast<int>(groupEndByStart_.size()) ||
            (groupEndByStart_[row] <= row + 1 && manualEndByStart_[row] <= row)) return;
        const QPoint local = table_->viewport()->mapFromGlobal(QCursor::pos());
        const QRect cell = table_->visualItemRect(table_->item(row, 0));
        if (local.x() >= cell.left() && local.x() < cell.left() + 30)
            toggleStepGroup(row);
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == 0) toggleStepGroup(row);
    });

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
    // List management belongs above its contents; additions and editing below.
    quickHeading_ = new QLabel;
    quickHeading_->setObjectName(QStringLiteral("inputLabQuickHeading"));
    layout->insertWidget(layout->indexOf(stepsPanel_) + 1, quickHeading_);
    layout->insertLayout(layout->indexOf(quickHeading_) + 1, quickActions);
    auto* editButtons = new QHBoxLayout;
    createGroup_ = new QPushButton;
    createGroup_->setObjectName(QStringLiteral("inputLabCreateManualGroup"));
    renameGroup_ = new QPushButton;
    renameGroup_->setObjectName(QStringLiteral("inputLabRenameManualGroup"));
    ungroupSteps_ = new QPushButton;
    ungroupSteps_->setObjectName(QStringLiteral("inputLabUngroupManualSteps"));
    duplicateStep_ = new QPushButton;
    duplicateStep_->setObjectName(QStringLiteral("inputLabDuplicateStep"));
    deleteStep_ = new QPushButton;
    deleteStep_->setObjectName(QStringLiteral("inputLabDeleteStep"));
    moveStepUp_ = new QPushButton;
    moveStepUp_->setObjectName(QStringLiteral("inputLabMoveStepUp"));
    moveStepDown_ = new QPushButton;
    moveStepDown_->setObjectName(QStringLiteral("inputLabMoveStepDown"));
    undoStep_ = new QPushButton;
    undoStep_->setObjectName(QStringLiteral("inputLabUndoStep"));
    redoStep_ = new QPushButton;
    redoStep_->setObjectName(QStringLiteral("inputLabRedoStep"));
    for (auto* button : {createGroup_, renameGroup_, ungroupSteps_, duplicateStep_, deleteStep_,
                         moveStepUp_, moveStepDown_, undoStep_, redoStep_})
        editButtons->addWidget(button);
    layout->insertLayout(layout->indexOf(quickHeading_) + 2, editButtons);
    connect(createGroup_, &QPushButton::clicked, this, [this] { createManualGroup(); });
    connect(renameGroup_, &QPushButton::clicked, this, [this] { renameManualGroup(); });
    connect(ungroupSteps_, &QPushButton::clicked, this, [this] { ungroupManualSteps(); });
    connect(duplicateStep_, &QPushButton::clicked, this, [this] { duplicateSelectedRows(); });
    connect(deleteStep_, &QPushButton::clicked, this, [this] { deleteSelectedRows(); });
    connect(moveStepUp_, &QPushButton::clicked, this, [this] { moveSelectedRows(-1); });
    connect(moveStepDown_, &QPushButton::clicked, this, [this] { moveSelectedRows(1); });
    connect(undoStep_, &QPushButton::clicked, this, [this] { undoActionEdit(); });
    connect(redoStep_, &QPushButton::clicked, this, [this] { redoActionEdit(); });
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
    searchStepUi_ = new QPushButton;
    searchStepUi_->setObjectName(QStringLiteral("inputLabSearchUiElements"));
    clearStepUi_ = new QPushButton;
    clearStepUi_->setObjectName(QStringLiteral("inputLabClearUiElement"));
    clearStepWindow_->setObjectName(QStringLiteral("inputLabStepClearWindow"));
    chooseStepWindow_->setMinimumHeight(38);
    clearStepWindow_->setMinimumHeight(38);
    stepWindowStatus_ = new QLabel;
    stepWindowStatus_->setObjectName(QStringLiteral("inputLabStepWindowStatus"));
    stepWindowStatus_->setWordWrap(true);
    stepWindowRow->addWidget(chooseStepWindow_);
    stepWindowRow->addWidget(clearStepWindow_);
    stepWindowRow->addWidget(searchStepUi_);
    stepWindowRow->addWidget(clearStepUi_);
    stepWindowRow->addWidget(stepWindowStatus_, 1);
    form->addRow(stepWindowRow);
    stepEnforce_ = new QCheckBox;
    stepEnforce_->setObjectName(QStringLiteral("inputLabStepEnforceWindow"));
    form->addRow(stepEnforce_);
    stepActivate_ = new QCheckBox;
    stepActivate_->setObjectName(QStringLiteral("inputLabStepActivateWindow"));
    form->addRow(stepActivate_);
    stepWindowWait_ = largeIntegerEdit("inputLabStepWindowWaitMs", QStringLiteral("0"));
    stepWindowTimeoutLabel_ = new QLabel;
    form->addRow(stepWindowTimeoutLabel_, stepWindowWait_);
    connect(chooseStepWindow_, &QPushButton::clicked, this, [this] { chooseStepWindow(); });
    connect(clearStepWindow_, &QPushButton::clicked, this, [this] { clearStepWindow(); });
    connect(searchStepUi_, &QPushButton::clicked, this, [this] { searchStepUiElements(); });
    connect(clearStepUi_, &QPushButton::clicked, this, [this] { clearStepUiElement(); });
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
    setButtonIcon(quickWaitUntil_, Icon::ClockUntil);
    setButtonIcon(quickCode_, Icon::Code);
    setButtonIcon(selectAll_, Icon::SelectAll);
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
#ifdef CLICKNOVA_WITH_X11
    if (x11Stop_) x11Stop_->stop();
#endif
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

void InputLabWidget::hideEmbeddedSavedLibrary() {
    // The connected controls now live in the common toolbar, not twice in the editor.
    savedToggle_->hide();
    savedGroup_->hide();
}


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
    if (quickHeading_) quickHeading_->setText(trText("ADD STEP", "ДОБАВИТЬ ШАГ"));
    if (stepsHeading_) stepsHeading_->setText(trText("STEP LIST", "СПИСОК ШАГОВ"));
    stepSearch_->setPlaceholderText(trText("Search steps by name, parameters or window…", "Поиск по шагам, параметрам и окнам…"));
    stepFilter_->setItemText(0, trText("All steps", "Все шаги"));
    stepFilter_->setItemText(1, trText("Mouse clicks", "Клики мыши"));
    stepFilter_->setItemText(2, trText("Keyboard", "Клавиатура"));
    stepFilter_->setItemText(3, trText("Text and scripts", "Текст и код"));
    stepFilter_->setItemText(4, trText("Waits", "Ожидания"));
    stepFilter_->setItemText(5, trText("Wheel and movement", "Прокрутка и движение"));
    stepFilter_->setItemText(6, trText("Element actions", "Действия с элементами"));
    expandGroups_->setText(trText("Expand", "Развернуть"));
    collapseGroups_->setText(trText("Collapse", "Свернуть"));
    detachSteps_->setText(detachedSteps_ ? trText("Restore", "Вернуть") : trText("Detach", "Отделить"));
    enlargeSteps_->setToolTip(trText("Increase list height", "Увеличить высоту списка"));
    shrinkSteps_->setToolTip(trText("Decrease list height", "Уменьшить высоту списка"));
    stepResizeGrip_->setToolTip(trText("Drag to resize list height", "Потяните, чтобы изменить высоту списка"));
    createGroup_->setText(trText("New group", "Группа"));
    createGroup_->setToolTip(trText("Name consecutive selected steps as a logical section.",
        "Создать именованную группу из выделенных подряд шагов."));
    renameGroup_->setText(trText("Rename group", "Имя группы"));
    ungroupSteps_->setText(trText("Ungroup", "Разгруппировать"));
    duplicateStep_->setText(trText("Duplicate", "Дублировать"));
    deleteStep_->setText(trText("Delete", "Удалить"));
    moveStepUp_->setText(trText("Move up", "Вверх"));
    moveStepDown_->setText(trText("Move down", "Вниз"));
    undoStep_->setText(trText("Undo", "Отменить"));
    redoStep_->setText(trText("Redo", "Повторить"));
    help_->setText(trText(
        "Record, edit, multi-select and reorder actions or add local PowerShell code. Drafts are autosaved. Gamepads are not supported.",
        "Записывайте, редактируйте и выделяйте несколько шагов; добавляйте локальный код PowerShell. Черновик сохраняется автоматически."));
    chooseStepWindow_->setText(trText("Choose step window", "Окно шага"));
    clearStepWindow_->setText(trText("Clear", "Сброс"));
    searchStepUi_->setText(trText("Find UI element", "Найти элемент"));
    clearStepUi_->setText(trText("Clear element", "Сброс элемента"));
    clearStepUi_->setToolTip(trText("Remove the accessibility binding from this click step.",
        "Удалить привязку элемента для этого шага клика."));
    searchStepUi_->setToolTip(trText("Browse open Windows applications and browser tabs directly here. No prior window selection needed. UIA is not the browser DOM.",
        "Открыть поиск по приложениям Windows и вкладкам браузера. Сначала выбирать окно шага не нужно. UIA — не DOM браузера."));
    stepEnforce_->setText(trText("Require this window for playback (recorded window is informational by default)",
                                    "Требовать это окно при запуске (после записи — только информация)"));
    stepActivate_->setText(trText("Activate this window before the step", "Активировать окно перед шагом"));
    stepActivate_->setToolTip(trText(
        "Background mode works only with a native UIA/MSAA/Java element action, without tab switching or Focus. "
        "OpenCV/OCR, mouse, keyboard and wheel input require a foreground window. Providers may still reject a background action.",
        "Фоновый режим доступен только для действий UIA/MSAA/Java без смены вкладки и Focus. "
        "OpenCV/OCR, мышь, клавиатура и колесо требуют активного окна. Приложение может не поддерживать фоновое действие."));
    stepWindowTimeoutLabel_->setText(trText("Window timeout (ms)", "Ожидание окна (мс)"));
    stepWindowWait_->setToolTip(trText("Wait up to this many milliseconds for the selected window; 0 = check immediately.",
        "Сколько миллисекунд ожидать выбранное окно; 0 = проверить сразу."));
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
    const auto selection = selectedActionRows();
    refreshTable(selection.size() == 1 ? selection.front() : -1);
    if (selection.size() > 1) refreshSelectedRows(selection);
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
    if (const auto* e = std::get_if<ElementTask>(&action)) {
        const QStringList labels = russian_ ? QStringList{
            QString::fromUtf8("Элемент: действие"), QString::fromUtf8("Элемент: двойное нажатие"),
            QString::fromUtf8("Элемент: ввод текста"), QString::fromUtf8("Элемент: прокрутка"),
            QString::fromUtf8("Элемент: ожидание"), QString::fromUtf8("Элемент: проверка")} :
            QStringList{QStringLiteral("Element: action"), QStringLiteral("Element: double activation"),
                        QStringLiteral("Element: set text"), QStringLiteral("Element: scroll"),
                        QStringLiteral("Element: wait until visible"), QStringLiteral("Element: assert present")};
        return labels.at(static_cast<int>(e->command));
    }
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
        else if constexpr (std::is_same_v<T, ElementTask>) {
            switch (a.command) {
            case ElementCommand::SetText: return trText("Enter %1 characters at the resolved target", "Ввести %1 символов в найденную цель")
                .arg(a.text.size());
            case ElementCommand::Scroll: return QStringLiteral("%1: %2")
                .arg(a.axis == Axis::Vertical ? trText("Vertical", "Вертикально") :
                     trText("Horizontal", "Горизонтально")).arg(a.delta);
            case ElementCommand::WaitVisible: return trText("Timeout: %1 ms · poll: %2 ms", "Тайм-аут: %1 мс · опрос: %2 мс")
                .arg(a.timeoutMs).arg(a.pollMs);
            case ElementCommand::AssertVisible: return trText("Check now, fail if absent", "Проверить сейчас, ошибка при отсутствии");
            case ElementCommand::DoubleActivate: return trText("Resolve each of two activations", "Повторный поиск перед каждым нажатием");
            case ElementCommand::Activate: return trText("Re-resolve before operation", "Повторный поиск перед действием");
            }
            return QStringLiteral("Element step");
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
    if (const auto* task = std::get_if<ElementTask>(&action)) {
        if (task->command == ElementCommand::WaitVisible ||
            task->command == ElementCommand::AssertVisible) return Icon::Wait;
        if (task->command == ElementCommand::SetText) return Icon::Text;
        if (task->command == ElementCommand::Scroll) return Icon::Mouse;
        return Icon::Click;
    }
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
    const int count = static_cast<int>(actions_.size());
    groupEndByStart_.assign(count, -1);
    groupStartByRow_.assign(count, -1);
    manualEndByStart_.assign(count, -1);
    manualStartByRow_.assign(count, -1);
    for (const auto& group : clicknova::input::namedStepRuns(stepWindows_)) {
        const int start = static_cast<int>(group.first);
        const int end = static_cast<int>(group.end);
        manualEndByStart_[start] = end;
        for (int row = start; row < end; ++row) manualStartByRow_[row] = start;
    }
    for (const auto& group : clicknova::input::visibleAutomaticRuns(actions_, stepWindows_)) {
        const int start = static_cast<int>(group.first);
        const int end = static_cast<int>(group.end);
        groupEndByStart_[start] = end;
        for (int row = start; row < end; ++row) groupStartByRow_[row] = start;
    }
    for (int row = 0; row < count; ++row) {
        const auto& action = actions_[static_cast<std::size_t>(row)];
        QString titleText = QStringLiteral("%1. %2").arg(row + 1).arg(actionName(action));
        if (manualEndByStart_[row] > row) {
            titleText = QStringLiteral("%1  %2–%3. 📁 %4 (%5)  ·  %6")
                .arg(collapsedManualGroups_.contains(QString::fromStdString(stepWindows_[row].manualGroupId)) ?
                     QStringLiteral("▸") : QStringLiteral("▾"))
                .arg(row + 1).arg(manualEndByStart_[row])
                .arg(QString::fromStdString(stepWindows_[row].manualGroupTitle))
                .arg(manualEndByStart_[row] - row)
                .arg(actionName(action));
        } else if (groupEndByStart_[row] > row + 1) {
            titleText = QStringLiteral("%1  %2–%3. %4  ×%5")
                .arg(collapsedStepGroups_.contains(row) ? QStringLiteral("▸") : QStringLiteral("▾"))
                .arg(row + 1).arg(groupEndByStart_[row]).arg(actionName(action))
                .arg(groupEndByStart_[row] - row);
        } else if (groupStartByRow_[row] >= 0 ||
                   (manualStartByRow_[row] >= 0 && manualStartByRow_[row] != row)) {
            titleText.prepend(QStringLiteral("    ↳  "));
        }
        auto* title = new QTableWidgetItem(titleText);
        if (manualEndByStart_[row] > row) title->setToolTip(trText(
            "Manual group: click arrow to collapse, right-click to rename or ungroup.",
            "Ручная группа: нажмите стрелку, ПКМ — переименовать или разгруппировать."));
        else if (groupEndByStart_[row] > row + 1) title->setToolTip(trText(
            "Click the arrow (or double-click) to expand/collapse consecutive actions. All remain executable.",
            "Нажмите стрелку (или дважды строку), чтобы свернуть повторяющиеся действия. Все шаги продолжают выполняться."));
        title->setIcon(clicknova::gui::icon(actionPictogram(action)));
        // The title is presentation-only: automatic/manual groups add prefixes,
        // counts and indentation which change as actions move or are inserted.
        // Keep a canonical, independent action value for GUI regression checks
        // and for clients that need to distinguish action order from grouping.
        title->setData(Qt::UserRole,
            QJsonDocument(clicknova::storage::encodeInputAction(action))
                .toJson(QJsonDocument::Compact));
        table_->setItem(row, 0, title);
        QString details = actionDetails(action);
        if (static_cast<std::size_t>(row) < stepWindows_.size() &&
            !stepWindows_[static_cast<std::size_t>(row)].title.empty()) {
            const auto& target = stepWindows_[static_cast<std::size_t>(row)];
            details += QStringLiteral("  ·  ") + QString::fromStdString(target.title) +
                (target.enforce ? QString() : trText(" (observed)", " (записано)"));
            if (!target.elementAutomationId.empty() || !target.elementName.empty())
                details += QStringLiteral("  · %1 %2: ").arg(
                    QString::fromLatin1(clicknova::accessibility::providerName(
                        static_cast<clicknova::accessibility::Provider>(target.elementProvider))),
                    uiOperationLabel(target.elementOperation, russian_)) +
                    QString::fromStdString(target.elementAutomationId.empty() ?
                        target.elementName : target.elementAutomationId);
        }
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
    applyStepVisibility();
    selectAction(selectedRow);
    scheduleAutosave();
}

int InputLabWidget::stepCategory(int row) const {
    if (row < 0 || row >= static_cast<int>(actions_.size())) return -1;
    const auto& a = actions_[static_cast<std::size_t>(row)];
    if (std::holds_alternative<clicknova::input::Click>(a) || std::holds_alternative<clicknova::input::ButtonDown>(a) ||
        std::holds_alternative<clicknova::input::ButtonUp>(a) || std::holds_alternative<clicknova::input::MouseHold>(a)) return 1;
    if (std::holds_alternative<clicknova::input::KeyTap>(a) || std::holds_alternative<clicknova::input::KeyDown>(a) ||
        std::holds_alternative<clicknova::input::KeyUp>(a) || std::holds_alternative<clicknova::input::KeyHold>(a)) return 2;
    if (std::holds_alternative<clicknova::input::TypeText>(a) || std::holds_alternative<clicknova::input::RunScript>(a)) return 3;
    if (std::holds_alternative<clicknova::input::Wait>(a) || std::holds_alternative<clicknova::input::WaitUntil>(a)) return 4;
    if (std::holds_alternative<clicknova::input::Wheel>(a) || std::holds_alternative<clicknova::input::MovePointer>(a)) return 5;
    if (std::holds_alternative<clicknova::input::ElementTask>(a)) return 6;
    return 0;
}

void InputLabWidget::applyStepVisibility() {
    if (!table_ || !stepCount_) return;
    const QString needle = stepSearch_->text().trimmed();
    const int category = stepFilter_->currentData().toInt();
    const bool filtering = !needle.isEmpty() || category != 0;
    const int count = static_cast<int>(actions_.size());
    std::vector<bool> matches(static_cast<std::size_t>(count), false);
    for (int row = 0; row < count; ++row) {
        const bool typeMatches = category == 0 || stepCategory(row) == category;
        const QString title = actionName(actions_[static_cast<std::size_t>(row)]);
        const QString details = table_->item(row, 1) ? table_->item(row, 1)->text() : QString();
        matches[static_cast<std::size_t>(row)] = typeMatches &&
            (needle.isEmpty() || title.contains(needle, Qt::CaseInsensitive) ||
             details.contains(needle, Qt::CaseInsensitive) ||
             QString::number(row + 1).contains(needle));
    }
    int visible = 0;
    for (int row = 0; row < count; ++row) {
        const int parent = row < static_cast<int>(groupStartByRow_.size()) ? groupStartByRow_[row] : -1;
        bool show = matches[static_cast<std::size_t>(row)];
        const int manualHead = manualStartByRow_[row];
        if (filtering && parent == row && groupEndByStart_[row] > row + 1) {
            for (int child = row + 1; child < groupEndByStart_[row]; ++child)
                show = show || matches[static_cast<std::size_t>(child)];
        }
        if (manualHead == row && manualEndByStart_[row] > row) {
            if (!needle.isEmpty() && QString::fromStdString(stepWindows_[row].manualGroupTitle)
                    .contains(needle, Qt::CaseInsensitive) && (category == 0 ||
                        std::any_of(matches.begin() + row, matches.begin() + manualEndByStart_[row],
                                    [](bool matched) { return matched; }))) show = true;
            if (filtering) for (int child = row + 1; child < manualEndByStart_[row]; ++child)
                show = show || matches[static_cast<std::size_t>(child)];
        }
        if (!filtering && manualHead >= 0 && manualHead != row &&
            collapsedManualGroups_.contains(QString::fromStdString(stepWindows_[row].manualGroupId))) show = false;
        if (!filtering && parent >= 0 && parent != row && collapsedStepGroups_.contains(parent)) show = false;
        table_->setRowHidden(row, !show);
        if (show) ++visible;
        if (manualHead == row && manualEndByStart_[row] > row) {
            auto* title = table_->item(row, 0);
            if (title) title->setText(QStringLiteral("%1  %2–%3. 📁 %4 (%5)  ·  %6")
                .arg(collapsedManualGroups_.contains(QString::fromStdString(stepWindows_[row].manualGroupId)) && !filtering ?
                    QStringLiteral("▸") : QStringLiteral("▾"))
                .arg(row + 1).arg(manualEndByStart_[row])
                .arg(QString::fromStdString(stepWindows_[row].manualGroupTitle))
                .arg(manualEndByStart_[row] - row)
                .arg(actionName(actions_[static_cast<std::size_t>(row)])));
        }
        if (parent == row && groupEndByStart_[row] > row + 1) {
            auto* title = table_->item(row, 0);
            if (title) {
                title->setText(QStringLiteral("%1  %2–%3. %4  ×%5")
                    .arg(collapsedStepGroups_.contains(row) && !filtering ? QStringLiteral("▸") : QStringLiteral("▾"))
                    .arg(row + 1).arg(groupEndByStart_[row])
                    .arg(actionName(actions_[static_cast<std::size_t>(row)]))
                    .arg(groupEndByStart_[row] - row));
            }
        }
    }
    // Never allow edits to continue targeting a row hidden by a filter/group.
    const int current = table_->currentRow();
    if (current >= 0 && current < count && table_->isRowHidden(current)) {
        int next = -1;
        if (!filtering && current < static_cast<int>(groupStartByRow_.size())) {
            const int parent = groupStartByRow_[current];
            if (parent >= 0 && !table_->isRowHidden(parent)) next = parent;
            if (next < 0 && manualStartByRow_[current] >= 0 &&
                !table_->isRowHidden(manualStartByRow_[current]))
                next = manualStartByRow_[current];
        }
        if (next < 0) {
            for (int row = 0; row < count; ++row) {
                if (!table_->isRowHidden(row)) { next = row; break; }
            }
        }
        if (next >= 0)
            table_->setCurrentCell(next, 0, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        else {
            table_->clearSelection();
            table_->selectionModel()->clearCurrentIndex();
            selectAction(-1);
        }
    }
    stepCount_->setText(trText("Visible: %1 / %2 steps (all execute)",
        "Показано: %1 из %2 шагов (выполняются все)").arg(visible).arg(count));
}

void InputLabWidget::toggleStepGroup(int row) {
    if (row < 0 || row >= static_cast<int>(groupEndByStart_.size())) return;
    if (manualEndByStart_[row] > row) {
        const QString id = QString::fromStdString(stepWindows_[row].manualGroupId);
        if (collapsedManualGroups_.contains(id)) collapsedManualGroups_.remove(id);
        else collapsedManualGroups_.insert(id);
        applyStepVisibility();
        return;
    }
    if (groupEndByStart_[row] <= row + 1) return;
    if (collapsedStepGroups_.contains(row)) collapsedStepGroups_.remove(row);
    else collapsedStepGroups_.insert(row);
    applyStepVisibility();
}

bool InputLabWidget::selectedManualGroup(int& first, int& end) const {
    first = end = -1;
    const auto selection = selectedActionRows();
    if (selection.empty()) return false;
    const int row = selection.front();
    if (row < 0 || row >= static_cast<int>(manualStartByRow_.size())) return false;
    first = manualStartByRow_[row];
    if (first < 0 || first >= static_cast<int>(manualEndByStart_.size())) return false;
    end = manualEndByStart_[first];
    if (end <= first) return false;
    // The selection must refer to a single named group.
    for (const int other : selection)
        if (other >= static_cast<int>(manualStartByRow_.size()) || manualStartByRow_[other] != first) return false;
    return true;
}

void InputLabWidget::createManualGroup() {
    if (isRunning()) return;
    const auto selection = selectedActionRows();
    if (selection.empty() || selection.back() - selection.front() + 1 != static_cast<int>(selection.size())) {
        reportStatus(trText("Select consecutive steps to create a group.",
            "Выделите идущие подряд шаги для создания группы."));
        return;
    }
    bool ok = false;
    const QString title = QInputDialog::getText(this, QStringLiteral("ClickNova"),
        trText("Name the group", "Название группы"), QLineEdit::Normal,
        trText("New section", "Новая группа"), &ok).trimmed();
    if (!ok || title.isEmpty()) return;
    if (title.size() > 80 || title.contains(QChar(0)) ||
        std::any_of(title.begin(), title.end(), [](QChar ch) { return ch.category() == QChar::Other_Control; })) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"), trText(
            "Group name must be 1–80 characters.", "Название группы: от 1 до 80 символов."));
        return;
    }
    checkpointActions();
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    for (int row : selection) {
        stepWindows_[row].manualGroupId = id;
        stepWindows_[row].manualGroupTitle = title.toStdString();
    }
    const int start = selection.front();
    refreshTable(start);
    refreshSelectedRows(selection);
    reportStatus(trText("Named group created.", "Именованная группа создана."));
}

void InputLabWidget::renameManualGroup() {
    if (isRunning()) return;
    int first = -1, end = -1;
    if (!selectedManualGroup(first, end)) return;
    const auto id = stepWindows_[first].manualGroupId;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("ClickNova"),
        trText("Rename group", "Переименовать группу"), QLineEdit::Normal,
        QString::fromStdString(stepWindows_[first].manualGroupTitle), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (name.size() > 80 || name.contains(QChar(0)) ||
        std::any_of(name.begin(), name.end(), [](QChar ch) { return ch.category() == QChar::Other_Control; })) {
        QMessageBox::warning(this, QStringLiteral("ClickNova"), trText(
            "Group name must be 1–80 characters.", "Название группы: от 1 до 80 символов."));
        return;
    }
    checkpointActions();
    // Rename every span of the same logical group, including spans that were
    // split by moving a step; step order remains untouched.
    for (auto& step : stepWindows_)
        if (step.manualGroupId == id) step.manualGroupTitle = name.toStdString();
    refreshTable(first);
}

void InputLabWidget::ungroupManualSteps() {
    if (isRunning()) return;
    int first = -1, end = -1;
    if (!selectedManualGroup(first, end)) return;
    const auto id = stepWindows_[first].manualGroupId;
    checkpointActions();
    // Remove the logical group completely, rather than leaving orphaned
    // labels on spans previously split by reorder operations.
    for (auto& step : stepWindows_)
        if (step.manualGroupId == id) {
            step.manualGroupId.clear(); step.manualGroupTitle.clear();
        }
    collapsedManualGroups_.remove(QString::fromStdString(id));
    refreshTable(first);
}

void InputLabWidget::resizeStepList(int pixels) {
    if (!table_) return;
    const int height = std::clamp(table_->height() + pixels, 180, 1150);
    table_->setFixedHeight(height);
    if (!detachedSteps_) attachedHeight_ = height;
}

void InputLabWidget::detachStepList() {
    if (detachedSteps_) { detachedSteps_->raise(); detachedSteps_->activateWindow(); return; }
    if (!stepsPanel_ || !stepsRootLayout_) return;
    attachedHeight_ = table_->height();
    const int originalIndex = stepsRootLayout_->indexOf(stepsPanel_);
    auto* dialog = new QDialog(this, Qt::Window);
    detachedSteps_ = dialog;
    dialog->setObjectName(QStringLiteral("inputLabDetachedSteps"));
    dialog->setWindowTitle(trText("ClickNova — Steps", "ClickNova — Шаги"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->resize(1000, 620);
    auto* floatingLayout = new QVBoxLayout(dialog);
    stepsRootLayout_->removeWidget(stepsPanel_);
    floatingLayout->addWidget(stepsPanel_);
    table_->setMinimumHeight(180);
    table_->setMaximumHeight(QWIDGETSIZE_MAX);
    table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    detachSteps_->setText(trText("Restore", "Вернуть"));
    connect(dialog, &QDialog::finished, this, [this, dialog, originalIndex] {
        if (stepsPanel_ && stepsRootLayout_) {
            dialog->layout()->removeWidget(stepsPanel_);
            stepsRootLayout_->insertWidget(std::max(0, originalIndex), stepsPanel_);
            table_->setFixedHeight(attachedHeight_);
        }
        detachedSteps_.clear();
        detachSteps_->setText(trText("Detach", "Отделить"));
    });
    connect(detachSteps_, &QPushButton::clicked, dialog, &QDialog::close);
    dialog->show();
}

void InputLabWidget::selectAction(int index) {
    if (index >= 0 && index < static_cast<int>(actions_.size()) && selectedActionRows().size() == 1)
        loadEditor(actions_[static_cast<std::size_t>(index)]);
    editorGroup_->setEnabled(index >= 0 && selectedActionRows().size() == 1 && !isRunning() &&
        !std::holds_alternative<ElementTask>(actions_[static_cast<std::size_t>(index)]));
    refreshStepWindowEditor(index);
    updateButtons();
}

void InputLabWidget::updateSelected() {
    if (updating_ || isRunning()) return;
    int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(actions_.size()) || selectedActionRows().size() != 1) return;
    if (std::holds_alternative<ElementTask>(actions_[static_cast<std::size_t>(row)])) return;
    if (!numericInputsValid()) return;
    Action edited = currentEditorAction();
    if (clicknova::storage::encodeInputAction(edited) ==
        clicknova::storage::encodeInputAction(actions_[static_cast<std::size_t>(row)])) return;
    const bool changedType = actions_[static_cast<std::size_t>(row)].index() != edited.index();
    checkpointActions();
    actions_[static_cast<std::size_t>(row)] = std::move(edited);
    auto& target = stepWindows_[static_cast<std::size_t>(row)];
    if ((!target.elementName.empty() || !target.elementAutomationId.empty()) &&
        (!std::holds_alternative<Click>(actions_[static_cast<std::size_t>(row)]) ||
         (std::get<Click>(actions_[static_cast<std::size_t>(row)]).button != Button::Left ||
          (target.elementOperation != 0 &&
           std::get<Click>(actions_[static_cast<std::size_t>(row)]).count != 1)))) {
        target.elementName.clear();
        target.elementAutomationId.clear();
        target.elementClass.clear();
        target.elementRole = 0;
        target.elementProvider = 0;
        target.elementBrowser = false;
        target.elementTab.clear();
        target.elementOperation = 0;
        target.elementValue.clear();
        refreshStepWindowEditor(row);
    }
    // Preserve selection while replacing visible descriptions.
    QSignalBlocker blocker(table_);
    table_->item(row, 0)->setText(QStringLiteral("%1. %2").arg(row + 1).arg(actionName(actions_[static_cast<std::size_t>(row)])));
    table_->item(row, 0)->setIcon(clicknova::gui::icon(actionPictogram(actions_[static_cast<std::size_t>(row)])));
    table_->item(row, 0)->setData(Qt::UserRole,
        QJsonDocument(clicknova::storage::encodeInputAction(actions_[static_cast<std::size_t>(row)]))
            .toJson(QJsonDocument::Compact));
    QString details = actionDetails(actions_[static_cast<std::size_t>(row)]);
    if (!stepWindows_[static_cast<std::size_t>(row)].title.empty()) {
        // Reuse the binding reference above, avoiding MSVC C4456 shadowing.
        details += QStringLiteral("  ·  ") + QString::fromStdString(target.title) +
            (target.enforce ? QString() : trText(" (observed)", " (записано)"));
        if (!target.elementAutomationId.empty() || !target.elementName.empty())
            details += QStringLiteral("  · %1 %2: ").arg(
                    QString::fromLatin1(clicknova::accessibility::providerName(
                        static_cast<clicknova::accessibility::Provider>(target.elementProvider))),
                    uiOperationLabel(target.elementOperation, russian_)) +
                QString::fromStdString(target.elementAutomationId.empty() ?
                    target.elementName : target.elementAutomationId);
    }
    table_->item(row, 1)->setText(details);
    if (changedType) refreshTable(row); // A changed type can split/merge groups.
    else { applyStepVisibility(); scheduleAutosave(); } // Do not reset cursor while entering text.
}

void InputLabWidget::addAction() {
    if (isRunning() || actions_.size() >= 512 || !numericInputsValid()) return;
    checkpointActions();
    actions_.push_back(currentEditorAction());
    stepWindows_.push_back({});
    refreshTable(static_cast<int>(actions_.size()) - 1);
    reportActionChange(QStringLiteral("Added steps: %1 · Total: %2"),
        QStringLiteral("Добавлено шагов: %1 · Всего: %2"), 1);
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
    // Clipboard steps are independent inserts, never an accidental continuation
    // of a group with a matching UUID in another scenario.
    for (auto& binding : pastedWindows) {
        binding.manualGroupId.clear();
        binding.manualGroupTitle.clear();
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
    reportActionChange(QStringLiteral("Pasted steps: %1 · Total: %2"),
        QStringLiteral("Вставлено шагов: %1 · Всего: %2"), static_cast<int>(pasted.size()));
}

void InputLabWidget::insertActionAt(int row, int actionType) {
    if (isRunning() || actions_.size() >= 512) return;
    Action action = defaultActionOfType(actionType);
    const int index = std::clamp(row, 0, static_cast<int>(actions_.size()));
    checkpointActions();
    actions_.insert(actions_.begin() + index, std::move(action));
    StepWindow newBinding;
    // An insertion strictly between members of a named group belongs to it.
    // Inserting at a boundary creates an independent, ungrouped step.
    if (index > 0 && index < static_cast<int>(stepWindows_.size()) &&
        !stepWindows_[index - 1].manualGroupId.empty() &&
        stepWindows_[index - 1].manualGroupId == stepWindows_[index].manualGroupId) {
        newBinding.manualGroupId = stepWindows_[index - 1].manualGroupId;
        newBinding.manualGroupTitle = stepWindows_[index - 1].manualGroupTitle;
    }
    stepWindows_.insert(stepWindows_.begin() + index, std::move(newBinding));
    refreshTable(index);
    reportActionChange(QStringLiteral("Added steps: %1 · Total: %2"),
        QStringLiteral("Добавлено шагов: %1 · Всего: %2"), 1);
}

void InputLabWidget::deleteRow(int row) {
    if (isRunning() || row < 0 || row >= static_cast<int>(actions_.size())) return;
    checkpointActions();
    actions_.erase(actions_.begin() + row);
    stepWindows_.erase(stepWindows_.begin() + row);
    refreshTable(std::min(row, static_cast<int>(actions_.size()) - 1));
    reportActionChange(QStringLiteral("Deleted steps: %1 · Total: %2"),
        QStringLiteral("Удалено шагов: %1 · Всего: %2"), 1);
}

void InputLabWidget::duplicateRow(int row) {
    if (isRunning() || row < 0 || row >= static_cast<int>(actions_.size()) || actions_.size() >= 512) return;
    Action duplicate = actions_[static_cast<std::size_t>(row)];
    const StepWindow duplicateWindow = stepWindows_[static_cast<std::size_t>(row)];
    checkpointActions();
    actions_.insert(actions_.begin() + row + 1, std::move(duplicate));
    stepWindows_.insert(stepWindows_.begin() + row + 1, duplicateWindow);
    refreshTable(row + 1);
    reportActionChange(QStringLiteral("Duplicated steps: %1 · Total: %2"),
        QStringLiteral("Дублировано шагов: %1 · Всего: %2"), 1);
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
    reportActionChange(QStringLiteral("Moved steps: %1 · Total: %2"),
        QStringLiteral("Перемещено шагов: %1 · Всего: %2"), 1);
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
    reportActionChange(QStringLiteral("Deleted steps: %1 · Total: %2"),
        QStringLiteral("Удалено шагов: %1 · Всего: %2"), static_cast<int>(rows.size()));
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
    reportActionChange(QStringLiteral("Duplicated steps: %1 · Total: %2"),
        QStringLiteral("Дублировано шагов: %1 · Всего: %2"), static_cast<int>(rows.size()));
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
    reportActionChange(QStringLiteral("Copied steps: %1 · Total: %2"),
        QStringLiteral("Скопировано шагов: %1 · Всего: %2"), static_cast<int>(rows.size()));
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
    reportActionChange(QStringLiteral("Moved steps: %1 · Total: %2"),
        QStringLiteral("Перемещено шагов: %1 · Всего: %2"), static_cast<int>(rows.size()));
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
    reportActionChange(QStringLiteral("Moved steps: %1 · Total: %2"),
        QStringLiteral("Перемещено шагов: %1 · Всего: %2"), static_cast<int>(extracted.size()));
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
        const QString elementText = !binding.elementAutomationId.empty() ?
            QString::fromStdString(binding.elementAutomationId) :
            QString::fromStdString(binding.elementName);
        const QString actionName = uiOperationLabel(binding.elementOperation, russian_);
        stepWindowStatus_->setText((binding.title.empty() ?
            trText("Uses default foreground window", "Окно по умолчанию") :
            QString::fromStdString(binding.title) + (binding.enforce ?
                (clicknova::input::backgroundElementAction(binding) ?
                    trText(" (native background action)", " (фоновое нативное действие)") : QString()) :
                trText(" (recorded only)", " (только из записи)"))) +
            (elementText.isEmpty() ? QString() :
                QStringLiteral(" · %1 %2: ").arg(
                    QString::fromLatin1(clicknova::accessibility::providerName(
                        static_cast<clicknova::accessibility::Provider>(binding.elementProvider))),
                    actionName) + elementText +
                (binding.elementTab.empty() ? QString() :
                    trText(" · Tab: ", " · Вкладка: ") + QString::fromStdString(binding.elementTab))));
        stepEnforce_->setChecked(!binding.title.empty() && binding.enforce);
        stepActivate_->setChecked(binding.activate);
        stepWindowWait_->setText(QString::number(binding.waitMs));
    }
    for (auto* widget : {static_cast<QWidget*>(chooseStepWindow_), static_cast<QWidget*>(clearStepWindow_)})
        widget->setEnabled(single && !isRunning());
    // Discovery is independent of selecting a step; binding requires a compatible action.
    searchStepUi_->setEnabled(!isRunning());
    clearStepUi_->setEnabled(single && !isRunning() &&
        (!stepWindows_[static_cast<std::size_t>(row)].elementAutomationId.empty() ||
         !stepWindows_[static_cast<std::size_t>(row)].elementName.empty()));
    const bool hasWindow = single && !stepWindows_[static_cast<std::size_t>(row)].title.empty();
    stepEnforce_->setEnabled(hasWindow && !isRunning());
    const bool requiresWindow = hasWindow && stepWindows_[static_cast<std::size_t>(row)].enforce;
    const StepWindow* selectedBinding = single ? &stepWindows_[static_cast<std::size_t>(row)] : nullptr;
    const bool foregroundOnly = selectedBinding &&
        (!selectedBinding->elementAutomationId.empty() || !selectedBinding->elementName.empty()) &&
        (selectedBinding->elementProvider >= 3 || selectedBinding->elementOperation == 8 ||
         selectedBinding->elementBrowser || !selectedBinding->elementTab.empty());
    stepActivate_->setEnabled(requiresWindow && !isRunning() && !foregroundOnly);
    stepWindowWait_->setEnabled(requiresWindow && !isRunning());
}

void InputLabWidget::chooseStepWindow() {
    if (isRunning() || selectedActionRows().size() != 1) return;
    const auto selected = chooseTargetWindow(this, russian_ ? QStringLiteral("ru") : QStringLiteral("en"));
    if (!selected) return;
    const auto row = static_cast<std::size_t>(selectedActionRows().front());
    checkpointActions();
    stepWindows_[row].elementName.clear();
    stepWindows_[row].elementAutomationId.clear();
    stepWindows_[row].elementClass.clear();
    stepWindows_[row].elementRole = 0;
    stepWindows_[row].elementProvider = 0;
    stepWindows_[row].elementBrowser = false;
    stepWindows_[row].elementTab.clear();
    stepWindows_[row].elementOperation = 0;
    stepWindows_[row].elementValue.clear();
    if (std::holds_alternative<ElementTask>(actions_[row]))
        actions_[row] = clicknova::input::Wait{0};
    stepWindows_[row].title = QString::fromStdWString(selected->title).toUtf8().toStdString();
    stepWindows_[row].process = QString::fromStdWString(selected->process).toUtf8().toStdString();
    stepWindows_[row].enforce = true;
    refreshTable(static_cast<int>(row));
}

void InputLabWidget::searchStepUiElements() {
    if (isRunning()) return;
    std::optional<clicknova::platform::WindowTarget> preferred;
    const auto selectedRows = selectedActionRows();
    if (selectedRows.size() == 1) {
        const auto& binding = stepWindows_[static_cast<std::size_t>(selectedRows.front())];
        if (!binding.title.empty())
            preferred = clicknova::platform::findWindow(
                QString::fromStdString(binding.title).toStdWString(),
                QString::fromStdString(binding.process).toStdWString());
    }
    if (!preferred) preferred = targetWindow_;

    // Search is read-only; a separate explicit action is bound to a single
    // left-click step, never executed while browsing the accessibility tree.
    QDialog dialog(this);
    dialog.setWindowTitle(trText("Find applications, tabs and elements",
                                "Поиск приложений, вкладок и элементов"));
    dialog.resize(1150, 740);
    auto* layout = new QVBoxLayout(&dialog);
    auto* target = new QLabel(trText("Choose any open application or browser tab below; no step window is required.",
        "Выберите приложение или вкладку прямо здесь — предварительно задавать окно шага не нужно."), &dialog);
    target->setObjectName(QStringLiteral("uiaSearchTarget"));
    target->setWordWrap(true);
    layout->addWidget(target);
    auto* notice = new QLabel(trText(
        "Auto-detect UIA, MSAA and Java Access Bridge (not browser DOM). Search is read-only. Available actions depend on the provider; selectors are rechecked during playback.",
        "Автопоиск через UIA, MSAA и Java Access Bridge (не DOM браузера). Поиск ничего не нажимает. Доступные действия зависят от источника; при выполнении элемент проверяется заново."), &dialog);
    notice->setObjectName(QStringLiteral("uiaSearchExplanation"));
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto* filterRow = new QHBoxLayout;
    auto* query = new QLineEdit(&dialog);
    query->setPlaceholderText(trText("Name or selector (empty = all)",
                                     "Имя или ID (пусто = все)"));
    query->setToolTip(trText("Search by accessibility name or selector, case-insensitively. Empty search shows meaningful controls.",
        "Поиск по имени или идентификатору элемента без учёта регистра. Пустой запрос показывает содержательные элементы."));
    auto* role = new QComboBox(&dialog);
    const std::array<const char*, 13> english = {
        "All", "Button", "Edit", "Checkbox", "Combobox", "Link", "Text", "Menu item",
        "Radio button", "List item", "Tree item", "Slider", "Spinner"};
    const std::array<const char*, 13> russian = {
        "Все", "Кнопка", "Поле ввода", "Флажок", "Список", "Ссылка", "Текст", "Пункт меню",
        "Радиокнопка", "Элемент списка", "Элемент дерева", "Ползунок", "Счётчик"};
    for (int i = 0; i < 13; ++i) role->addItem(QString::fromUtf8(russian_ ? russian[i] : english[i]), i);
    role->setToolTip(trText("Filter by actual accessibility control type. 'All' means no type filter.",
        "Фильтр по реальному типу элемента. «Все» означает отсутствие фильтра."));
    // The hidden selector preserves the existing selection/binding path.
    // Users interact with the hierarchical tree below, never with engine lists.
    auto* scope = new QComboBox(&dialog);
    scope->setObjectName(QStringLiteral("uiaSearchSource"));
    scope->setMinimumWidth(305);
    scope->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    scope->setMinimumContentsLength(27);
    scope->setToolTip(trText("Auto-detected browser tabs from UI Automation, all other open applications, and paired browser extensions. DevTools is optional.",
        "Автоматически найденные вкладки браузеров через UIA, остальные приложения и вкладки расширений. DevTools необязателен."));
    auto* refreshSources = new QPushButton(trText("Refresh targets", "Обновить окна"), &dialog);
    refreshSources->setObjectName(QStringLiteral("uiaRefreshSources"));
    auto* find = new QPushButton(trText("Search", "Поиск"), &dialog);
    filterRow->addWidget(query, 1);
    filterRow->addWidget(role);
    scope->hide(); // Internal index shared by native/visual search and binding.
    filterRow->addWidget(refreshSources);
    filterRow->addWidget(find);
    layout->addLayout(filterRow);
    // Native image matching and OCR only operate on a visible foreground
    // client area. These controls are disabled when optional native libraries
    // were not available at CMake configuration time.
    auto* visualRow = new QHBoxLayout;
    auto* useImage = new QCheckBox(trText("Find image with OpenCV", "Поиск изображения OpenCV"), &dialog);
    useImage->setObjectName(QStringLiteral("uiaVisualImageEnabled"));
    useImage->setEnabled(clicknova::accessibility::visual::imageAvailable());
    auto* templateFile = new QLineEdit(&dialog);
    templateFile->setObjectName(QStringLiteral("uiaVisualTemplatePath"));
    templateFile->setReadOnly(true);
    templateFile->setPlaceholderText(trText("Select button image (PNG/JPEG/BMP)",
        "Выберите образец кнопки (PNG/JPEG/BMP)"));
    auto* pickTemplate = new QPushButton(trText("Browse image…", "Выбрать образец…"), &dialog);
    pickTemplate->setObjectName(QStringLiteral("uiaVisualBrowseImage"));
    pickTemplate->setEnabled(useImage->isEnabled());
    connect(pickTemplate, &QPushButton::clicked, &dialog, [=, &dialog] {
        const QString path = QFileDialog::getOpenFileName(&dialog,
            trText("Choose image template", "Выберите образец изображения"), QString(),
            QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp)"));
        if (!path.isEmpty()) { templateFile->setText(path); useImage->setChecked(true); }
    });
    auto* useOcr = new QCheckBox(trText("Find text with OCR", "Найти текст через OCR"), &dialog);
    useOcr->setObjectName(QStringLiteral("uiaVisualOcrEnabled"));
    useOcr->setEnabled(clicknova::accessibility::visual::ocrAvailable());
    auto* visualState = new QLabel(&dialog);
    visualState->setObjectName(QStringLiteral("uiaVisualAvailability"));
    visualState->setText(!useImage->isEnabled() ?
        trText("OpenCV not installed in this build", "В этой сборке OpenCV не подключён") :
        !useOcr->isEnabled() ? trText("OCR needs Tesseract", "Для OCR нужен Tesseract") :
        trText("Visible foreground window only", "Только видимое активное окно"));
    visualRow->addWidget(useImage);
    visualRow->addWidget(templateFile, 1);
    visualRow->addWidget(pickTemplate);
    visualRow->addWidget(useOcr);
    visualRow->addWidget(visualState);
    layout->addLayout(visualRow);
    auto* recognitionSettings = new QHBoxLayout;
    auto* imageScore = new QSpinBox(&dialog);
    imageScore->setObjectName(QStringLiteral("uiaVisualImageConfidence"));
    imageScore->setRange(50, 99);
    imageScore->setValue(90);
    imageScore->setSuffix(QStringLiteral("%"));
    imageScore->setToolTip(trText("Minimum image match confidence", "Минимальная точность совпадения изображения"));
    auto* ocrScore = new QSpinBox(&dialog);
    ocrScore->setObjectName(QStringLiteral("uiaVisualOcrConfidence"));
    ocrScore->setRange(30, 99);
    ocrScore->setValue(60);
    ocrScore->setSuffix(QStringLiteral("%"));
    ocrScore->setToolTip(trText("Minimum OCR line confidence", "Минимальная точность распознавания текста"));
    auto* searchRadius = new QSpinBox(&dialog);
    searchRadius->setObjectName(QStringLiteral("uiaVisualSearchRadius"));
    searchRadius->setRange(1, 30);
    searchRadius->setValue(8);
    searchRadius->setSuffix(QStringLiteral("%"));
    searchRadius->setToolTip(trText("Maximum distance from the originally chosen position during playback, as percent of the window size", "Максимальное смещение от исходного положения при выполнении в процентах от размера окна"));
    recognitionSettings->addWidget(new QLabel(trText("Image sensitivity:", "Точность OpenCV:"), &dialog));
    recognitionSettings->addWidget(imageScore);
    recognitionSettings->addWidget(new QLabel(trText("OCR confidence:", "Точность OCR:"), &dialog));
    recognitionSettings->addWidget(ocrScore);
    recognitionSettings->addWidget(new QLabel(trText("Re-search radius:", "Радиус повторного поиска:"), &dialog));
    recognitionSettings->addWidget(searchRadius);
    recognitionSettings->addStretch();
    layout->addLayout(recognitionSettings);
    // Chromium's opt-in localhost DevTools endpoint provides real page URLs;
    // it is NOT a privilege to bind a page to an unrelated native window.
    auto* devToolsRow = new QHBoxLayout;
    auto* devToolsEnabled = new QCheckBox(trText("Additional browser tabs via local DevTools", "Вкладки через локальный DevTools"), &dialog);
    devToolsEnabled->setObjectName(QStringLiteral("uiaDevToolsEnabled"));
    devToolsEnabled->setToolTip(trText("Requires Chromium launched by the user with --remote-debugging-port and a dedicated profile. Only 127.0.0.1 is contacted.",
        "Нужно запустить Chromium с --remote-debugging-port и отдельным профилем. Подключение только к 127.0.0.1."));
    auto* devToolsPort = new QSpinBox(&dialog);
    devToolsPort->setObjectName(QStringLiteral("uiaDevToolsPort"));
    devToolsPort->setRange(1024, 65535);
    devToolsPort->setValue(9222);
    devToolsPort->setPrefix(QStringLiteral("localhost:"));
    devToolsPort->setEnabled(false);
    auto* fetchFavicons = new QCheckBox(trText("Fetch first-party site icons", "Загружать иконки сайтов"), &dialog);
    fetchFavicons->setObjectName(QStringLiteral("uiaFetchFavicons"));
    fetchFavicons->setChecked(true);
    fetchFavicons->setToolTip(trText("Requests /favicon.ico or the first-party favicon provided by the browser, directly from each website. Uncheck to avoid these network requests.",
        "ClickNova запрашивает /favicon.ico или значок с того же сайта. Снимите флажок, чтобы не отправлять эти запросы."));
    auto* devToolsState = new QLabel(trText("DevTools is optional: UIA tabs remain available without it.",
        "DevTools необязателен: вкладки UIA доступны и без него."), &dialog);
    devToolsState->setObjectName(QStringLiteral("uiaDevToolsState"));
    devToolsRow->addWidget(devToolsEnabled);
    devToolsRow->addWidget(devToolsPort);
    devToolsRow->addWidget(fetchFavicons);
    auto* devToolsHelp = new QPushButton(QStringLiteral("?"), &dialog);
    devToolsHelp->setObjectName(QStringLiteral("uiaDevToolsHelp"));
    devToolsHelp->setFixedWidth(30);
    connect(devToolsHelp, &QPushButton::clicked, &dialog, [this, &dialog] {
        QMessageBox::information(&dialog, QStringLiteral("ClickNova — DevTools"), trText(
            "Optional for Chrome/Edge/Brave and compatible Chromium browsers. Start a SEPARATE browser profile with:\n\n"
            "chrome.exe --remote-debugging-port=9222 --user-data-dir=\"%TEMP%\\ClickNova-CDP\"\n\n"
            "Then enable DevTools and refresh the list. This local debugging port grants other local processes extensive control over that browser profile. Avoid sensitive accounts; close the debug browser after use. UIA-only tabs still work without DevTools. Tabs not safely matched to a Windows window are for discovery only.",
            "Для Chrome/Edge/Brave и совместимых браузеров. Запустите браузер с ОТДЕЛЬНЫМ профилем:\n\n"
            "chrome.exe --remote-debugging-port=9222 --user-data-dir=\"%TEMP%\\ClickNova-CDP\"\n\n"
            "Затем включите DevTools и обновите список. Этот локальный порт предоставляет другим процессам расширенный доступ к отладочному профилю: не используйте в нём важные учётные записи и закрывайте после работы. Вкладки UIA доступны и без DevTools. Вкладки без надёжной связи с окном Windows можно только просматривать."));
    });
    devToolsRow->addWidget(devToolsHelp);
    devToolsRow->addWidget(devToolsState, 1);
    layout->addLayout(devToolsRow);
    // Read-only WebExtensions inventory: normal Chrome/Edge/Brave/Opera/Vivaldi
    // and Firefox tabs, without DevTools. The extension needs explicit pairing.
    auto* extensionBridge = new clicknova::gui::BrowserExtensionBridge(&dialog);
    const bool extensionReady = extensionBridge->start();
    auto* extensionRow = new QHBoxLayout;
    auto* extensionStatus = new QLabel(extensionReady ?
        trText("Browser extensions: waiting for paired browsers", "Расширения: ожидание подключённых браузеров") :
        trText("Extension bridge unavailable (port 48231 in use)", "Мост расширений недоступен (порт 48231 занят)"), &dialog);
    extensionStatus->setObjectName(QStringLiteral("uiaExtensionBridgeStatus"));
    auto* pairingCode = new QLineEdit(extensionBridge->pairingKey(), &dialog);
    pairingCode->setObjectName(QStringLiteral("uiaExtensionPairingKey"));
    pairingCode->setReadOnly(true);
    pairingCode->setMinimumWidth(145);
    pairingCode->setToolTip(trText("Private pairing key. Paste it into the ClickNova Tabs Bridge browser extension. Do not publish it.",
        "Секретный код сопряжения: вставьте в расширение ClickNova Tabs Bridge и не публикуйте."));
    auto* copyPairing = new QPushButton(trText("Copy key", "Копировать код"), &dialog);
    copyPairing->setObjectName(QStringLiteral("uiaCopyExtensionKey"));
    connect(copyPairing, &QPushButton::clicked, &dialog, [pairingCode] {
        if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(pairingCode->text());
    });
    auto* extensionHelp = new QPushButton(QStringLiteral("?"), &dialog);
    extensionHelp->setObjectName(QStringLiteral("uiaExtensionHelp"));
    extensionHelp->setFixedWidth(30);
    connect(extensionHelp, &QPushButton::clicked, &dialog, [this, &dialog] {
        QMessageBox::information(&dialog, QStringLiteral("ClickNova — Browser Extension"), trText(
            "ClickNova automatically recognizes supported browsers and unknown browsers whose tab strip and address bar are accessible through Windows UI Automation. To also list background tabs hidden from UIA, install ClickNova Tabs Bridge once per browser: extensions/chromium for WebExtensions-compatible Chromium browsers; extensions/firefox for Firefox-compatible browsers. Enter the key above only once. The paired extension reconnects when this dialog opens; allow up to 30 seconds for its periodic sync. Not every browser supports WebExtensions or exposes its tabs to Windows; access to absolutely every browser cannot be guaranteed. Hidden/private tabs and browser internals are excluded. Metadata-only tabs are not native click targets.",
            "ClickNova автоматически определяет известные браузеры и неизвестные браузеры, показывающие вкладки и адресную строку через Windows UI Automation. Чтобы видеть также фоновые вкладки, скрытые от UIA, установите ClickNova Tabs Bridge один раз в каждый браузер: extensions/chromium для совместимых Chromium-браузеров или extensions/firefox для Firefox-совместимых. Код выше вводится один раз. После этого расширение подключается при открытии окна поиска; периодическое обновление может занять до 30 секунд. Не все браузеры поддерживают WebExtensions или раскрывают вкладки Windows, поэтому гарантировать абсолютно любой браузер нельзя. Приватные и внутренние вкладки исключены. Вкладки «только список» не используются как цели нажатий."));
    });
    extensionRow->addWidget(extensionStatus);
    extensionRow->addWidget(pairingCode, 1);
    extensionRow->addWidget(copyPairing);
    extensionRow->addWidget(extensionHelp);
    layout->addLayout(extensionRow);
    auto* network = new QNetworkAccessManager(&dialog);
    auto* showTechnical = new QCheckBox(trText("Include unnamed technical containers",
        "Показать безымянные технические контейнеры"), &dialog);
    showTechnical->setObjectName(QStringLiteral("uiaIncludeAnonymous"));
    showTechnical->setChecked(false);
    showTechnical->setToolTip(trText("Usually these are layout panes without an accessible name or ID. Hide them to find buttons and fields more quickly.",
        "Чаще всего это панели компоновки без имени и ID. Скройте их, чтобы быстрее находить кнопки и поля."));
    layout->addWidget(showTechnical);
    auto* results = new QTableWidget(&dialog);
    results->setObjectName(QStringLiteral("uiaSearchResults"));
    results->setColumnCount(5);
    results->setHorizontalHeaderLabels({trText("Name", "Имя"), QStringLiteral("AutomationId"),
        trText("Actual type", "Тип элемента"), trText("Class", "Класс"),
        trText("Enabled", "Включён")});
    results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results->setSelectionBehavior(QAbstractItemView::SelectRows);
    results->setSelectionMode(QAbstractItemView::SingleSelection);
    results->verticalHeader()->hide();
    results->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    results->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    results->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    results->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    results->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    results->setColumnWidth(1, 170);
    results->setColumnWidth(2, 145);
    results->setColumnWidth(3, 130);
    results->setColumnWidth(4, 95);
    results->horizontalHeaderItem(0)->setToolTip(trText("Accessible Name is provided by the application. It may be empty even for visible controls.",
        "Доступное имя передаёт приложение. Оно бывает пустым даже у видимых элементов."));
    results->horizontalHeaderItem(1)->setToolTip(trText("AutomationId is an optional developer-defined identifier, NOT a Windows window handle.",
        "AutomationId — необязательный идентификатор разработчика, НЕ дескриптор окна Windows."));
    results->horizontalHeaderItem(2)->setToolTip(trText("Control type and discovery provider; 'All' is only a search filter.",
        "Тип элемента и источник поиска. «Все» — только фильтр поиска."));
    results->horizontalHeaderItem(3)->setToolTip(trText("Underlying Windows accessibility class; often empty for web elements.",
        "Класс элемента Windows. У веб-элементов он нередко отсутствует."));
    results->horizontalHeaderItem(4)->setToolTip(trText("Whether the OS reports the element enabled; not a promise that ClickNova can act on it.",
        "Сообщает ли ОС, что элемент включён. Это не гарантия доступности действия ClickNova."));
    // One workspace: target hierarchy on the left, live accessible elements on
    // the right. Browser tabs are children of their actual Windows host, not
    // guessed from a favicon, URL or untrusted extension label.
    auto* workspace = new QSplitter(Qt::Horizontal, &dialog);
    workspace->setObjectName(QStringLiteral("unifiedSearchWorkspace"));
    auto* leftPanel = new QWidget(workspace);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 4, 0);
    auto* targetFilter = new QLineEdit(leftPanel);
    targetFilter->setObjectName(QStringLiteral("unifiedTargetFilter"));
    targetFilter->setPlaceholderText(trText("Filter applications and tabs…", "Фильтр программ и вкладок…"));
    leftLayout->addWidget(targetFilter);
    auto* sourceTree = new QTreeWidget(leftPanel);
    sourceTree->setObjectName(QStringLiteral("unifiedTargetTree"));
    sourceTree->setHeaderHidden(true);
    sourceTree->setUniformRowHeights(true);
    sourceTree->setSelectionMode(QAbstractItemView::SingleSelection);
    sourceTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sourceTree->setToolTip(trText("Select an application or a UIA browser tab. Disconnected websites are shown as metadata only.",
        "Выберите программу или UIA-вкладку. Сайты без связи с окном показаны только для просмотра."));
    leftLayout->addWidget(sourceTree, 1);
    workspace->addWidget(leftPanel);
    workspace->addWidget(results);
    workspace->setStretchFactor(0, 1);
    workspace->setStretchFactor(1, 2);
    workspace->setSizes({340, 700});
    layout->addWidget(workspace, 1);
    auto* actionRow = new QHBoxLayout;
    auto* operation = new QComboBox(&dialog);
    operation->setObjectName(QStringLiteral("uiaBindingOperation"));
    const std::array<const char*, 10> operationEn = {
        "Press (Invoke)", "Set text (Value)", "Check", "Uncheck", "Select item",
        "Expand", "Collapse", "Set numeric value", "Focus", "Scroll into view"};
    const std::array<const char*, 10> operationRu = {
        "Нажать (Invoke)", "Ввести текст (Value)", "Установить флажок", "Снять флажок", "Выбрать элемент",
        "Раскрыть", "Свернуть", "Задать число", "Фокус", "Прокрутить к элементу"};
    for (int i = 0; i < 10; ++i)
        operation->addItem(QString::fromUtf8(russian_ ? operationRu[i] : operationEn[i]), i);
    operation->setToolTip(trText("Only operations supported by the selected element and its backend can be bound.",
        "Доступны только действия, поддерживаемые элементом и его источником."));
    auto* actionValue = new QLineEdit(&dialog);
    actionValue->setObjectName(QStringLiteral("uiaBindingValue"));
    actionValue->setMaxLength(2048);
    actionValue->setPlaceholderText(trText("Text or decimal number for selected operation",
        "Текст или десятичное число для выбранной операции"));
    actionValue->setToolTip(trText("ValuePattern replaces the field content; empty text clears it. Numeric values use a dot (e.g. 2.5). Never used for passwords.",
        "ValuePattern заменяет содержимое поля; пустая строка очищает его. Число через точку (например, 2.5). Пароли запрещены."));
    actionRow->addWidget(new QLabel(trText("Operation", "Действие"), &dialog));
    actionRow->addWidget(operation);
    actionRow->addWidget(actionValue, 1);
    layout->addLayout(actionRow);
    auto* taskRow = new QHBoxLayout;
    auto* taskKind = new QComboBox(&dialog);
    taskKind->setObjectName(QStringLiteral("elementTaskKind"));
    const char* namesEn[] = {"Perform operation", "Double activate", "Set text", "Scroll element",
                             "Wait for element", "Check element exists"};
    const char* namesRu[] = {"Выполнить действие", "Двойное нажатие", "Ввести текст", "Прокрутить элемент",
                             "Дождаться элемента", "Проверить наличие"};
    for (int i = 0; i < 6; ++i) taskKind->addItem(QString::fromUtf8(russian_ ? namesRu[i] : namesEn[i]), i);
    auto* scrollAxis = new QComboBox(&dialog);
    scrollAxis->addItems({trText("Vertical", "Вертикально"), trText("Horizontal", "Горизонтально")});
    scrollAxis->setObjectName(QStringLiteral("elementScrollAxis"));
    auto* scrollAmount = new QSpinBox(&dialog);
    scrollAmount->setRange(-1200, 1200);
    scrollAmount->setValue(-120);
    scrollAmount->setObjectName(QStringLiteral("elementScrollAmount"));
    auto* timeout = new QSpinBox(&dialog);
    timeout->setRange(0, 600000);
    timeout->setSuffix(QStringLiteral(" ms"));
    timeout->setValue(5000);
    timeout->setObjectName(QStringLiteral("elementWaitTimeout"));
    auto* polling = new QSpinBox(&dialog);
    polling->setRange(20, 5000);
    polling->setSuffix(QStringLiteral(" ms"));
    polling->setValue(100);
    polling->setObjectName(QStringLiteral("elementWaitPoll"));
    auto* doubleInterval = new QSpinBox(&dialog);
    doubleInterval->setRange(10, std::min(1000, static_cast<int>(GetDoubleClickTime())));
    doubleInterval->setSuffix(QStringLiteral(" ms"));
    doubleInterval->setValue(100);
    doubleInterval->setObjectName(QStringLiteral("elementDoubleInterval"));
    taskRow->addWidget(new QLabel(trText("Step", "Шаг"), &dialog));
    taskRow->addWidget(taskKind, 2);
    taskRow->addWidget(scrollAxis);
    taskRow->addWidget(scrollAmount);
    taskRow->addWidget(timeout);
    taskRow->addWidget(polling);
    taskRow->addWidget(doubleInterval);
    layout->addLayout(taskRow);
    if (selectedActionRows().size() == 1) {
        if (const auto* previous = std::get_if<ElementTask>(
                &actions_[static_cast<std::size_t>(selectedActionRows().front())])) {
            taskKind->setCurrentIndex(static_cast<int>(previous->command));
            if (previous->command == ElementCommand::SetText) actionValue->setText(QString::fromStdU16String(previous->text));
            scrollAxis->setCurrentIndex(static_cast<int>(previous->axis));
            scrollAmount->setValue(previous->delta);
            timeout->setValue(previous->timeoutMs);
            polling->setValue(previous->pollMs);
            doubleInterval->setValue(previous->doubleIntervalMs);
        }
    }
    auto* state = new QLabel(trText("Choose an element and bind a universal step. Text is saved in plain text: never use passwords.",
        "Выберите элемент и тип действия. Текст сохраняется в сценарии открытым текстом: не используйте пароли."), &dialog);
    state->setWordWrap(true);
    layout->addWidget(state);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto* bind = close->addButton(trText("Bind element action", "Привязать действие элемента"), QDialogButtonBox::ActionRole);
    bind->setObjectName(QStringLiteral("uiaBindToStep"));
    bind->setEnabled(false);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(close);
    auto lastResults = std::make_shared<clicknova::accessibility::Result>();
    auto lastTarget = std::make_shared<std::optional<UiSearchTarget>>();
    auto choices = std::make_shared<std::vector<UiSearchTarget>>();
    auto browserTabs = std::make_shared<std::vector<UiSearchTarget>>();
    // A browser is not defined by its .exe name. UIA-detected unknown
    // executables are classified as browsers when selectable web tabs exist.
    auto detectedBrowserWindows = std::make_shared<QSet<quintptr>>();
    auto devToolsPages = std::make_shared<QVector<clicknova::gui::BrowserPage>>();
    auto extensionPages = std::make_shared<QVector<clicknova::gui::BrowserPage>>();
    auto attemptedExtensionIcons = std::make_shared<QSet<QString>>();
    auto siteIcons = std::make_shared<QHash<QString, QIcon>>();
    auto windows = std::make_shared<std::vector<clicknova::platform::WindowTarget>>();
    auto generation = std::make_shared<int>(0);
    auto tabLoading = std::make_shared<bool>(false);

    const auto populateSources = [=, this] {
        const int previous = scope->currentData().toInt();
        std::optional<UiSearchTarget> previousTarget;
        if (previous >= 0 && previous < static_cast<int>(choices->size()))
            previousTarget = (*choices)[static_cast<std::size_t>(previous)];
        QSignalBlocker scopeBlock(scope);
        QSignalBlocker treeBlock(sourceTree);
        scope->clear();
        sourceTree->clear();
        choices->clear();
        std::vector<QTreeWidgetItem*> choiceNodes;
        QTreeWidgetItem* group = nullptr;
        const auto heading = [&](const QString& text) {
            group = new QTreeWidgetItem(sourceTree, QStringList{text});
            group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
            group->setIcon(0, clicknova::gui::icon(clicknova::gui::Icon::Folder));
            group->setExpanded(true);
        };
        const auto addTarget = [&](const UiSearchTarget& item, const QString& name,
                                   const QIcon& icon, QTreeWidgetItem* host = nullptr) -> QTreeWidgetItem* {
            const int index = static_cast<int>(choices->size());
            choices->push_back(item);
            scope->addItem(icon, name, index);
            scope->setItemData(scope->count() - 1, name, Qt::ToolTipRole);
            auto* node = new QTreeWidgetItem(host ? host : group, QStringList{name});
            node->setData(0, Qt::UserRole, index);
            node->setIcon(0, icon);
            node->setToolTip(0, name);
            if (item.devToolsOnly) {
                node->setForeground(0, QColor(QStringLiteral("#98a9bd")));
                node->setToolTip(0, name + trText("\nInformation only: no secure native target",
                    "\nТолько сведения: нет надёжной привязки к окну"));
            }
            choiceNodes.push_back(node);
            return node;
        };
        const auto isBrowserWindow = [&](const clicknova::platform::WindowTarget& window) {
            return clicknova::accessibility::browserExecutable(window.process) ||
                detectedBrowserWindows->contains(window.handle);
        };
        heading(trText("Windows applications", "Приложения Windows"));
        for (const auto& window : *windows) {
            if (isBrowserWindow(window)) continue;
            const QString name = QString::fromStdWString(window.title);
            addTarget({window, {}, false}, name + QStringLiteral(" · ") +
                QString::fromStdWString(window.process), searchWindowIcon(window));
        }
        heading(trText("Browsers and websites (UIA, automatically detected)",
            "Браузеры и сайты (UIA, автоопределение)"));
        for (const auto& window : *windows) {
            if (!isBrowserWindow(window)) continue;
            auto* browserNode = addTarget({window, {}, true}, QString::fromStdWString(window.title) +
                QStringLiteral(" · ") + QString::fromStdWString(window.process), searchWindowIcon(window));
            browserNode->setExpanded(true);
            for (const auto& tab : *browserTabs) {
                if (tab.window.handle != window.handle || tab.window.processId != window.processId) continue;
                UiSearchTarget choice = tab;
                const QString tabTitle = QString::fromStdWString(tab.tab);
                const int matched = clicknova::gui::uniquePageForTitle(*devToolsPages, tabTitle);
                const int extensionMatch = clicknova::gui::uniqueExtensionPageForTab(
                    *extensionPages, tabTitle, QString::fromStdWString(tab.window.process));
                const int uiaMatches = static_cast<int>(std::count_if(browserTabs->begin(), browserTabs->end(),
                    [&](const UiSearchTarget& other) {
                        return QString::fromStdWString(other.tab).compare(tabTitle, Qt::CaseInsensitive) == 0;
                    }));
                // Metadata can decorate an existing UIA tab (favicon + URL),
                // but cannot designate a native target by itself. Refuse
                // ambiguous title matches and mismatched browser processes.
                const QString process = QString::fromStdWString(tab.window.process).toLower();
                if (uiaMatches == 1 && extensionMatch >= 0)
                    choice.pageUrl = (*extensionPages)[extensionMatch].url.toString();
                else if (matched >= 0 && uiaMatches == 1 &&
                         clicknova::accessibility::browserExecutable(tab.window.process) &&
                         !process.contains(QStringLiteral("firefox")))
                    choice.pageUrl = (*devToolsPages)[matched].url.toString();
                const QString text = QString::fromStdWString(tab.tab) +
                    (choice.pageUrl.isEmpty() ? QString() : QStringLiteral(" · ") + choice.pageUrl);
                addTarget(choice, text, siteIcons->value(choice.pageUrl, browserSiteIcon()), browserNode);
            }
        }
        if (!devToolsPages->empty()) {
            heading(trText("Additional websites via DevTools (view only when not in UIA)",
                "Дополнительные сайты DevTools (без UIA — только просмотр)"));
            for (const auto& page : *devToolsPages) {
                const QString pageUrl = page.url.toString();
                const int matchingPage = clicknova::gui::uniquePageForTitle(*devToolsPages, page.title);
                const int matchedWindows = static_cast<int>(std::count_if(browserTabs->begin(), browserTabs->end(),
                    [&](const UiSearchTarget& tab) {
                        return QString::fromStdWString(tab.tab).compare(page.title, Qt::CaseInsensitive) == 0 &&
                            clicknova::accessibility::browserExecutable(tab.window.process) &&
                            !QString::fromStdWString(tab.window.process).contains(QStringLiteral("firefox"), Qt::CaseInsensitive);
                    }));
                const bool matchedUiA = matchingPage >= 0 && matchedWindows == 1;
                if (matchedUiA) continue;
                UiSearchTarget candidate;
                candidate.tab = page.title.toStdWString();
                candidate.browser = true;
                candidate.pageUrl = pageUrl;
                candidate.devToolsOnly = true;
                addTarget(candidate, page.title + QStringLiteral(" · ") + pageUrl +
                    trText(" (metadata only)", " (только список)"),
                    siteIcons->value(pageUrl, browserSiteIcon()));
            }
        }
        if (!extensionPages->isEmpty()) {
            heading(trText("Websites from paired browser extensions (metadata only)",
                "Сайты подключённых расширений (только список)"));
            for (const auto& page : *extensionPages) {
                // A page that already decorates a unique native UIA tab
                // belongs there; do not display it again as metadata only.
                const int matchedTabs = static_cast<int>(std::count_if(browserTabs->begin(), browserTabs->end(),
                    [&](const UiSearchTarget& tab) {
                        return QString::fromStdWString(tab.tab).compare(page.title, Qt::CaseInsensitive) == 0 &&
                            clicknova::gui::uniqueExtensionPageForTab(*extensionPages, page.title,
                                QString::fromStdWString(tab.window.process)) >= 0;
                    }));
                if (matchedTabs == 1) continue;
                UiSearchTarget candidate;
                candidate.tab = page.title.toStdWString();
                candidate.browser = true;
                candidate.pageUrl = page.url.toString();
                candidate.devToolsOnly = true; // No unsafe window guessing.
                addTarget(candidate,
                    page.browser + QStringLiteral(" · ") +
                    page.title + QStringLiteral(" · ") + candidate.pageUrl +
                    trText(" (metadata only)", " (только список)"),
                    siteIcons->value(candidate.pageUrl, browserSiteIcon()));
            }
        }
        if (choices->empty())
            heading(trText("No open applications found", "Открытые приложения не найдены"));
        int selected = -1;
        for (int i = 0; i < static_cast<int>(choices->size()); ++i) {
            const auto& item = (*choices)[static_cast<std::size_t>(i)];
            const bool same = previousTarget &&
                item.window.handle == previousTarget->window.handle &&
                item.window.processId == previousTarget->window.processId && item.tab == previousTarget->tab &&
                // URL/favicon changes must not reset a real UIA tab selection.
                (item.window.handle != 0 || item.pageUrl == previousTarget->pageUrl);
            const bool preferredMatch = !previousTarget && preferred &&
                item.window.handle == preferred->handle && item.tab.empty();
            if (same || preferredMatch) { selected = i; break; }
        }
        if (selected < 0 && !choices->empty()) selected = 0;
        const int entry = selected >= 0 ? scope->findData(selected) : -1;
        scope->setCurrentIndex(entry);
        if (selected >= 0 && selected < static_cast<int>(choiceNodes.size())) {
            auto* node = choiceNodes[static_cast<std::size_t>(selected)];
            sourceTree->setCurrentItem(node);
        }
        // Preserve the user's filter across live window/favicon updates.
        const auto needle = targetFilter->text().trimmed();
        std::function<bool(QTreeWidgetItem*, bool)> filterTree =
            [&](QTreeWidgetItem* node, bool inheritedMatch) {
                const bool selfMatch = !needle.isEmpty() &&
                    node->text(0).contains(needle, Qt::CaseInsensitive);
                bool anyChild = false;
                for (int i = 0; i < node->childCount(); ++i)
                    anyChild = filterTree(node->child(i), inheritedMatch || selfMatch) || anyChild;
                const bool visible = needle.isEmpty() || inheritedMatch || selfMatch || anyChild;
                node->setHidden(!visible);
                if (visible && anyChild) node->setExpanded(true);
                return visible;
            };
        for (int i = 0; i < sourceTree->topLevelItemCount(); ++i)
            filterTree(sourceTree->topLevelItem(i), false);
        find->setEnabled(selected >= 0 && !(*choices)[static_cast<std::size_t>(selected)].devToolsOnly);
        if (selected >= 0 && selected < static_cast<int>(choices->size())) {
            const auto& current = (*choices)[static_cast<std::size_t>(selected)];
            if (current.devToolsOnly) {
                state->setText(trText("Website discovered via browser metadata: it cannot be safely mapped to a Windows UIA tab.",
                    "Сайт найден через браузер: вкладка не имеет однозначной связи с окном UIA."));
            }
            target->setText(trText("Target: %1 · %2", "Цель: %1 · %2").arg(
                current.devToolsOnly ? current.pageUrl : QString::fromStdWString(current.window.title),
                current.tab.empty() ? QString::fromStdWString(current.window.process) :
                    QString::fromStdWString(current.tab)));
        }
    };
    // Extension-reported favicon URLs are passed through browserIconUrl() so
    // icon downloads never contact a third-party host for a visited URL.
    auto requestExtensionIcons = [=, &dialog] {
        if (!fetchFavicons->isChecked()) return;
        int issued = 0;
        QVector<clicknova::gui::BrowserPage> pages = *extensionPages;
        // Without an extension UIA cannot list the URLs of background tabs.
        // The active address bar is reliable enough to request its first-party
        // favicon without activating any additional tabs.
        for (const auto& tab : *browserTabs) {
            if (tab.pageUrl.isEmpty()) continue;
            clicknova::gui::BrowserPage page;
            page.url = QUrl(tab.pageUrl);
            pages.push_back(std::move(page));
        }
        for (const auto& page : pages) {
            const QUrl url = clicknova::gui::browserIconUrl(page);
            const QString pageUrl = page.url.toString();
            if (!url.isValid() || url.isEmpty() || siteIcons->contains(pageUrl) ||
                attemptedExtensionIcons->contains(pageUrl) || issued >= 20) continue;
            attemptedExtensionIcons->insert(pageUrl);
            ++issued;
            QNetworkRequest request(url);
            request.setTransferTimeout(2500);
            request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                QNetworkRequest::ManualRedirectPolicy);
            auto* reply = network->get(request);
            connect(reply, &QNetworkReply::readyRead, &dialog, [reply] {
                if (reply->bytesAvailable() > 128 * 1024) reply->abort();
            });
            connect(reply, &QNetworkReply::finished, &dialog, [=] {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError ||
                    reply->bytesAvailable() > 128 * 1024 || !fetchFavicons->isChecked()) return;
                QBuffer buffer;
                buffer.setData(reply->readAll());
                if (!buffer.open(QIODevice::ReadOnly)) return;
                QImageReader reader(&buffer);
                const QSize dimensions = reader.size();
                if (!dimensions.isValid() || dimensions.width() > 512 ||
                    dimensions.height() > 512 || dimensions.width() < 1 || dimensions.height() < 1) return;
                reader.setScaledSize(dimensions.scaled(32, 32, Qt::KeepAspectRatio));
                const QImage image = reader.read();
                if (image.isNull()) return;
                (*siteIcons)[pageUrl] = QIcon(QPixmap::fromImage(image));
                if (scope->isEnabled()) populateSources();
            });
        }
    };
    const auto loadSources = [=, this, &dialog] {
        ++*generation;
        const int version = *generation;
        *windows = clicknova::platform::listWindows();
        browserTabs->clear();
        detectedBrowserWindows->clear();
        devToolsPages->clear();
        populateSources();
        *lastTarget = std::nullopt;
        lastResults->elements.clear();
        results->setRowCount(0);
        bind->setEnabled(false);
        const auto browsers = *windows;
        refreshSources->setEnabled(false);
        *tabLoading = true;
        if (devToolsEnabled->isChecked()) {
            devToolsState->setText(trText("Reading local browser tabs…", "Читаем вкладки браузера…"));
            QNetworkRequest request(clicknova::gui::devToolsListUrl(
                static_cast<quint16>(devToolsPort->value())));
            request.setTransferTimeout(2500);
            auto* reply = network->get(request);
            connect(reply, &QNetworkReply::readyRead, &dialog, [reply] {
                if (reply->bytesAvailable() > 512 * 1024) reply->abort();
            });
            connect(reply, &QNetworkReply::finished, &dialog, [=, this, &dialog] {
                reply->deleteLater();
                if (*generation != version) return;
                if (reply->error() != QNetworkReply::NoError || reply->bytesAvailable() > 512 * 1024) {
                    devToolsState->setText(trText("DevTools unavailable (UIA remains available)",
                        "DevTools недоступен (UIA работает отдельно)"));
                    return;
                }
                *devToolsPages = clicknova::gui::parseDevToolsPages(reply->readAll());
                devToolsState->setText(trText("DevTools websites: %1", "Сайтов DevTools: %1")
                    .arg(devToolsPages->size()));
                if (scope->isEnabled()) populateSources();
                if (!fetchFavicons->isChecked()) return;
                // Only request first-party favicons. Limit concurrent network
                // traffic and decoded image size. Never send URLs to an icon proxy.
                auto iconQueue = std::make_shared<QQueue<QPair<QString, QUrl>>>();
                QSet<QString> seen;
                for (const auto& page : *devToolsPages) {
                    const QString pageUrl = page.url.toString();
                    const QUrl iconUrl = clicknova::gui::browserIconUrl(page);
                    if (!iconUrl.isValid() || seen.contains(iconUrl.toString()) ||
                        siteIcons->contains(pageUrl)) continue;
                    seen.insert(iconUrl.toString());
                    iconQueue->enqueue(qMakePair(pageUrl, iconUrl));
                }
                auto activeIcons = std::make_shared<int>(0);
                auto pump = std::make_shared<std::function<void()>>();
                std::weak_ptr<std::function<void()>> weakPump(pump);
                *pump = [=, this, &dialog] {
                    while (*activeIcons < 4 && !iconQueue->isEmpty() && *generation == version) {
                        const auto pending = iconQueue->dequeue();
                        ++*activeIcons;
                        QNetworkRequest iconRequest(pending.second);
                        iconRequest.setTransferTimeout(2500);
                        iconRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                            QNetworkRequest::ManualRedirectPolicy);
                        auto* iconReply = network->get(iconRequest);
                        const auto keepPumpAlive = weakPump.lock();
                        connect(iconReply, &QNetworkReply::readyRead, &dialog, [iconReply] {
                            if (iconReply->bytesAvailable() > 128 * 1024) iconReply->abort();
                        });
                        connect(iconReply, &QNetworkReply::finished, &dialog, [=, this] {
                            (void)keepPumpAlive;
                            iconReply->deleteLater();
                            --*activeIcons;
                            if (*generation == version &&
                                iconReply->error() == QNetworkReply::NoError &&
                                iconReply->bytesAvailable() <= 128 * 1024) {
                                const QByteArray image = iconReply->readAll();
                                QBuffer buffer;
                                buffer.setData(image);
                                if (buffer.open(QIODevice::ReadOnly)) {
                                    QImageReader reader(&buffer);
                                    const QSize dimensions = reader.size();
                                    if (dimensions.isValid() && dimensions.width() <= 512 &&
                                        dimensions.height() <= 512 && dimensions.width() > 0 &&
                                        dimensions.height() > 0) {
                                        reader.setScaledSize(dimensions.scaled(32, 32, Qt::KeepAspectRatio));
                                        const QImage decoded = reader.read();
                                        if (!decoded.isNull()) {
                                            const QIcon icon(QPixmap::fromImage(decoded));
                                            for (const auto& page : *devToolsPages)
                                                if (clicknova::gui::browserIconUrl(page) == pending.second)
                                                    (*siteIcons)[page.url.toString()] = icon;
                                            for (int index = 0; index < scope->count(); ++index) {
                                                bool ok = false;
                                                const int choiceIndex = scope->itemData(index).toInt(&ok);
                                                if (ok && choiceIndex >= 0 && choiceIndex < static_cast<int>(choices->size())) {
                                                    const auto& choice = (*choices)[static_cast<std::size_t>(choiceIndex)];
                                                    if (siteIcons->contains(choice.pageUrl)) {
                                                        scope->setItemIcon(index, siteIcons->value(choice.pageUrl));
                                                        const auto nodes = sourceTree->findItems(QStringLiteral("*"),
                                                            Qt::MatchWildcard | Qt::MatchRecursive, 0);
                                                        for (auto* node : nodes)
                                                            if (node->data(0, Qt::UserRole).isValid() &&
                                                                node->data(0, Qt::UserRole).toInt() == choiceIndex)
                                                                node->setIcon(0, siteIcons->value(choice.pageUrl));
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            if (auto next = weakPump.lock()) (*next)();
                        });
                    }
                };
                (*pump)();
            });
        } else {
            devToolsState->setText(trText("Automatic UIA discovery is active (DevTools optional)",
                "Автоматическое обнаружение UIA включено (DevTools необязателен)"));
        }
        auto sites = std::make_shared<std::vector<UiSearchTarget>>();
        auto browserHandles = std::make_shared<QSet<quintptr>>();
        auto* worker = QThread::create([browsers, sites, browserHandles] {
            // Enumerate all open top-level application windows, not just a
            // hard-coded list of executable names. Unknown applications need
            // both a browser-like address bar and selectable UIA TabItems.
            for (const auto& window : browsers) {
                const auto tabs = clicknova::accessibility::listBrowserTabs(window);
                if (!tabs.empty()) browserHandles->insert(window.handle);
                for (const auto& tab : tabs) {
                    UiSearchTarget candidate{window, tab.title, true};
                    if (tab.active && !tab.activeUrl.empty())
                        candidate.pageUrl = QString::fromStdWString(tab.activeUrl);
                    sites->push_back(std::move(candidate));
                }
            }
        });
        connect(worker, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &QThread::finished, &dialog, [=] {
            if (*generation != version) return;
            *tabLoading = false;
            *browserTabs = std::move(*sites);
            *detectedBrowserWindows = std::move(*browserHandles);
            refreshSources->setEnabled(true);
            if (scope->isEnabled()) populateSources();
            requestExtensionIcons(); // active UIA tab may have a first-party favicon
        });
        worker->start();
    };
    connect(refreshSources, &QPushButton::clicked, &dialog, [=] {
        *extensionPages = extensionBridge->pages();
        extensionStatus->setText(trText("Paired browser tabs: %1", "Вкладок расширений: %1")
            .arg(extensionPages->size()));
        loadSources();
        requestExtensionIcons();
    });
    extensionBridge->onPagesChanged([=] {
        *extensionPages = extensionBridge->pages();
        extensionStatus->setText(trText("Paired browser tabs: %1", "Вкладок расширений: %1")
            .arg(extensionPages->size()));
        if (scope->isEnabled()) populateSources();
        requestExtensionIcons();
    });
    connect(devToolsEnabled, &QCheckBox::toggled, &dialog, [=](bool enabled) {
        devToolsPort->setEnabled(enabled);
        loadSources();
    });
    connect(devToolsPort, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
        [=](int) { if (devToolsEnabled->isChecked()) loadSources(); });
    connect(fetchFavicons, &QCheckBox::toggled, &dialog,
        [=](bool enabled) {
            if (!enabled) siteIcons->clear();
            else {
                attemptedExtensionIcons->clear();
                requestExtensionIcons();
            }
            if (devToolsEnabled->isChecked()) loadSources();
            else if (scope->isEnabled()) populateSources();
        });
    connect(scope, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [=, this](int) {
        const int index = scope->currentData().toInt();
        lastResults->elements.clear();
        *lastTarget = std::nullopt;
        results->setRowCount(0);
        bind->setEnabled(false);
        if (index < 0 || index >= static_cast<int>(choices->size())) return;
        const auto& choice = (*choices)[static_cast<std::size_t>(index)];
        find->setEnabled(!choice.devToolsOnly);
        if (choice.devToolsOnly) {
            target->setText(trText("Website: %1", "Сайт: %1").arg(choice.pageUrl));
            state->setText(trText("This website is available as browser metadata only. UIA search/binding requires a uniquely matched Windows tab.",
                "Этот сайт доступен в списке браузера. Для поиска и привязки UIA требуется однозначно определённая вкладка Windows."));
            return;
        }
        target->setText(trText("Target: %1 · %2", "Цель: %1 · %2").arg(
            QString::fromStdWString(choice.window.title),
            choice.tab.empty() ? QString::fromStdWString(choice.window.process) :
                QString::fromStdWString(choice.tab)));
        state->setText(trText("Press Search to inspect the selected target (no actions are performed).",
            "Нажмите «Поиск», чтобы просмотреть элементы выбранной цели."));
    });
    // Selecting a real target updates the existing binding/search machinery.
    // User selection changes the target; a separate button or double click
    // requests an inspection. Metadata-only pages never acquire a guessed HWND.
    connect(sourceTree, &QTreeWidget::currentItemChanged, &dialog,
            [=](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current || !current->data(0, Qt::UserRole).isValid()) return;
        const int choiceIndex = current->data(0, Qt::UserRole).toInt();
        if (choiceIndex < 0 || choiceIndex >= static_cast<int>(choices->size())) return;
        const int comboIndex = scope->findData(choiceIndex);
        if (comboIndex < 0) return;
        scope->setCurrentIndex(comboIndex);
    });
    connect(targetFilter, &QLineEdit::textChanged, &dialog, [=](const QString& value) {
        const QString needle = value.trimmed();
        std::function<bool(QTreeWidgetItem*, bool)> apply =
            [&](QTreeWidgetItem* item, bool parentMatch) {
                const bool selfMatch = !needle.isEmpty() &&
                    item->text(0).contains(needle, Qt::CaseInsensitive);
                bool childVisible = false;
                for (int i = 0; i < item->childCount(); ++i)
                    childVisible = apply(item->child(i), parentMatch || selfMatch) || childVisible;
                const bool visible = needle.isEmpty() || parentMatch || selfMatch || childVisible;
                item->setHidden(!visible);
                if (childVisible) item->setExpanded(true);
                return visible;
            };
        for (int i = 0; i < sourceTree->topLevelItemCount(); ++i)
            apply(sourceTree->topLevelItem(i), false);
    });
    connect(sourceTree, &QTreeWidget::itemDoubleClicked, &dialog,
            [=](QTreeWidgetItem* item, int) {
        if (!item || !item->data(0, Qt::UserRole).isValid() || !find->isEnabled()) return;
        // A second click explicitly requests a scan; avoid entering a game or
        // switching tabs simply by expanding the target hierarchy.
        find->click();
    });
    loadSources();
    auto* extensionRefreshTimer = new QTimer(&dialog);
    extensionRefreshTimer->setInterval(12000);
    connect(extensionRefreshTimer, &QTimer::timeout, &dialog, [=] {
        const auto live = extensionBridge->pages();
        if (live.size() != extensionPages->size()) {
            *extensionPages = live;
            extensionStatus->setText(trText("Paired browser tabs: %1", "Вкладок расширений: %1")
                .arg(extensionPages->size()));
            if (scope->isEnabled()) populateSources();
        }
        // Continue bounded favicon batches for browsers with many open tabs.
        requestExtensionIcons();
    });
    extensionRefreshTimer->start();
    // Keep discovering browsers opened after this dialog was shown. Do not
    // discard an in-progress search or currently displayed UIA results.
    auto* autoUiATimer = new QTimer(&dialog);
    autoUiATimer->setInterval(30000);
    connect(autoUiATimer, &QTimer::timeout, &dialog, [=] {
        if (!*tabLoading && scope->isEnabled() && lastResults->elements.empty())
            loadSources();
    });
    autoUiATimer->start();
    auto refreshBinding = [=] {
        using namespace clicknova::accessibility;
        const auto kind = static_cast<ElementCommand>(taskKind->currentIndex());
        const int index = results->currentRow();
        const auto op = static_cast<Operation>(operation->currentData().toInt());
        const bool takesValue = kind == ElementCommand::SetText ||
            (kind == ElementCommand::Activate &&
             (op == Operation::SetValue || op == Operation::SetRangeValue));
        actionValue->setVisible(takesValue);
        operation->setVisible(kind == ElementCommand::Activate);
        scrollAxis->setVisible(kind == ElementCommand::Scroll);
        scrollAmount->setVisible(kind == ElementCommand::Scroll);
        timeout->setVisible(kind == ElementCommand::WaitVisible);
        polling->setVisible(kind == ElementCommand::WaitVisible);
        doubleInterval->setVisible(kind == ElementCommand::DoubleActivate);
        const bool canModify = selectedActionRows().size() == 1 && (
            std::holds_alternative<ElementTask>(actions_[static_cast<std::size_t>(selectedActionRows().front())]) ||
            (std::get_if<Click>(&actions_[static_cast<std::size_t>(selectedActionRows().front())]) &&
             std::get<Click>(actions_[static_cast<std::size_t>(selectedActionRows().front())]).button == Button::Left &&
             std::get<Click>(actions_[static_cast<std::size_t>(selectedActionRows().front())]).count == 1));
        const bool chosen = *lastTarget && index >= 0 &&
            static_cast<std::size_t>(index) < lastResults->elements.size();
        bool valid = false;
        if (chosen && canModify) {
            const auto& e = lastResults->elements[static_cast<std::size_t>(index)];
            const bool hasSelector = !e.name.empty() || !e.automationId.empty();
            bool supported = false;
            switch (kind) {
            case ElementCommand::Activate:
                supported = supportsProvider(op, e.role, e.provider) &&
                    validValue(op, takesValue ? actionValue->text().toUtf8().toStdString() : std::string{});
                break;
            case ElementCommand::DoubleActivate:
                supported = e.provider < Provider::Visual ||
                    supportsProvider(Operation::Invoke, e.role, e.provider);
                break;
            case ElementCommand::SetText:
                supported = (e.provider >= Provider::Visual ||
                    supportsProvider(Operation::SetValue, e.role, e.provider)) &&
                    !actionValue->text().isEmpty() && actionValue->text().size() <= 2048;
                break;
            case ElementCommand::Scroll:
                supported = supportsProvider(Operation::Scroll, e.role, e.provider) &&
                    scrollAmount->value() != 0;
                break;
            case ElementCommand::WaitVisible:
            case ElementCommand::AssertVisible:
                supported = true;
                break;
            }
            valid = hasSelector && supported &&
                (e.enabled || kind == ElementCommand::WaitVisible || kind == ElementCommand::AssertVisible);
        }
        bind->setEnabled(valid);
    };
    connect(results, &QTableWidget::itemSelectionChanged, &dialog, [=] {
        const int row = results->currentRow();
        if (row >= 0 && static_cast<std::size_t>(row) < lastResults->elements.size()) {
            using clicknova::accessibility::Role;
            using clicknova::accessibility::Operation;
            const auto& selectedElement = lastResults->elements[static_cast<std::size_t>(row)];
            const Role selectedRole = selectedElement.role;
            const auto from = selectedElement.provider;
            auto supported = [=](Operation op) {
                return clicknova::accessibility::supportsProvider(op, selectedRole, from);
            };
            Operation suggested = Operation::Focus;
            if (supported(Operation::Invoke)) suggested = Operation::Invoke;
            else if (selectedRole == Role::Edit && supported(Operation::SetValue)) suggested = Operation::SetValue;
            else if (selectedRole == Role::CheckBox && supported(Operation::Check)) suggested = Operation::Check;
            else if (supported(Operation::Select)) suggested = Operation::Select;
            else if (supported(Operation::Expand)) suggested = Operation::Expand;
            else if (supported(Operation::SetRangeValue)) suggested = Operation::SetRangeValue;
            operation->setCurrentIndex(static_cast<int>(suggested));
        }
        refreshBinding();
    });
    connect(operation, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [=](int) { refreshBinding(); });
    connect(taskKind, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
            [=](int) { refreshBinding(); });
    connect(scrollAmount, QOverload<int>::of(&QSpinBox::valueChanged), &dialog,
            [=](int) { refreshBinding(); });
    connect(actionValue, &QLineEdit::textChanged, &dialog,
            [=](const QString&) { refreshBinding(); });
    refreshBinding();
    // QDialog is a non-copyable QWidget. Capture the stack-owned dialog by
    // reference (the connection context disconnects when it is destroyed).
    connect(bind, &QPushButton::clicked, &dialog, [=, this, &dialog] {
        const int index = results->currentRow();
        if (index < 0 || static_cast<std::size_t>(index) >= lastResults->elements.size() ||
            isRunning() || selectedActionRows().size() != 1) return;
        if (!*lastTarget) return; // A completed search fixes the window identity.
        const auto currentRow = static_cast<std::size_t>(selectedActionRows().front());
        auto selectedWindow = (*lastTarget)->window;
        // The browser title may have changed when its tab was selected.
        // Preserve its latest visible title, not the old tab's window title.
        for (const auto& live : clicknova::platform::listWindows()) {
            if (live.handle == selectedWindow.handle && live.processId == selectedWindow.processId) {
                selectedWindow = live;
                break;
            }
        }
        const auto* click = std::get_if<Click>(&actions_[currentRow]);
        if (!std::holds_alternative<ElementTask>(actions_[currentRow]) &&
            (!click || click->button != Button::Left || click->count != 1)) return;
        const auto& item = lastResults->elements[static_cast<std::size_t>(index)];
        const auto kind = static_cast<ElementCommand>(taskKind->currentIndex());
        using clicknova::accessibility::Operation;
        Operation op = Operation::Invoke;
        if (kind == ElementCommand::Activate)
            op = static_cast<Operation>(operation->currentData().toInt());
        if (kind == ElementCommand::SetText)
            op = item.provider >= clicknova::accessibility::Provider::Visual ?
                Operation::Invoke : Operation::SetValue;
        if (kind == ElementCommand::Scroll) op = Operation::Scroll;
        const std::string parameter = (kind == ElementCommand::Activate &&
            (op == Operation::SetValue || op == Operation::SetRangeValue)) ?
            actionValue->text().toUtf8().toStdString() : std::string{};
        if ((item.name.empty() && item.automationId.empty()) ||
            (kind != ElementCommand::WaitVisible && kind != ElementCommand::AssertVisible &&
             !clicknova::accessibility::supportsProvider(op, item.role, item.provider))) return;
        ElementTask command;
        command.command = kind;
        if (kind == ElementCommand::SetText) command.text = actionValue->text().toStdU16String();
        command.axis = static_cast<Axis>(scrollAxis->currentIndex());
        command.delta = scrollAmount->value();
        command.timeoutMs = timeout->value();
        command.pollMs = polling->value();
        command.doubleIntervalMs = doubleInterval->value();
        std::wstring portableSelector = item.automationId;
        if (item.provider == clicknova::accessibility::Provider::Visual) {
            // A scenario must remain usable after moving the portable folder.
            // Capture the user-selected template into data/templates by its
            // SHA-256 and store only a relative, validated asset token.
            std::wstring chosenPath;
            int confidence = 0, radiusPercent = 0;
            if (!clicknova::accessibility::visual::parseSelector(item.provider,
                    item.automationId, chosenPath, confidence, radiusPercent)) return;
            QFile templateFile(QString::fromStdWString(chosenPath));
            if (!templateFile.open(QIODevice::ReadOnly) || templateFile.size() < 64 ||
                templateFile.size() > 8 * 1024 * 1024) {
                QMessageBox::warning(&dialog, QStringLiteral("ClickNova"),
                    QStringLiteral("Образец изображения недоступен или слишком велик."));
                return;
            }
            const QByteArray bytes = templateFile.readAll();
            if (bytes.size() != templateFile.size()) return;
            const QString extension = QFileInfo(templateFile.fileName()).suffix().toLower();
            if (!QStringList{QStringLiteral("png"), QStringLiteral("jpg"),
                    QStringLiteral("jpeg"), QStringLiteral("bmp")}.contains(extension)) return;
            const QString sha = QString::fromLatin1(
                QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
            const QString directory = clicknova::portable::filePath(QStringLiteral("templates"));
            if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)) {
                QMessageBox::warning(&dialog, QStringLiteral("ClickNova"),
                    QStringLiteral("Не удалось создать безопасную папку образцов."));
                return;
            }
            const QString assetName = sha + QLatin1Char('.') + extension;
            const QString storedPath = QDir(directory).filePath(assetName);
            if (QFileInfo(storedPath).isSymLink()) return;
            if (!QFileInfo::exists(storedPath) && !QFile::copy(templateFile.fileName(), storedPath)) {
                QMessageBox::warning(&dialog, QStringLiteral("ClickNova"),
                    QStringLiteral("Не удалось скопировать образец в переносимую папку."));
                return;
            }
            QFile saved(storedPath);
            if (!saved.open(QIODevice::ReadOnly) ||
                QCryptographicHash::hash(saved.readAll(), QCryptographicHash::Sha256).toHex() != sha.toLatin1()) {
                QMessageBox::warning(&dialog, QStringLiteral("ClickNova"),
                    QStringLiteral("Сохранённый образец повреждён или изменён. Привязка отменена."));
                return;
            }
            portableSelector = clicknova::accessibility::visual::makeSelector(item.provider,
                (QStringLiteral("asset:") + assetName).toStdWString(), confidence, radiusPercent);
            if (portableSelector.empty()) return;
        }
        checkpointActions();
        auto& binding = stepWindows_[currentRow];
        binding.title = QString::fromStdWString(selectedWindow.title).toUtf8().toStdString();
        binding.process = QString::fromStdWString(selectedWindow.process).toUtf8().toStdString();
        binding.enforce = true;
        binding.activate = true; // Explicit binding opts into focusing its selected window.
        binding.elementName = QString::fromStdWString(item.name).toUtf8().toStdString();
        binding.elementAutomationId = QString::fromStdWString(portableSelector).toUtf8().toStdString();
        // Chromium frequently exposes Tailwind/CSS utility class lists as
        // UIA ClassName. They change with loading/hover/theme and are not a
        // persistent selector. Prefer exact Name + Role (+ AutomationId).
        const bool browserPage = (*lastTarget)->browser && !(*lastTarget)->tab.empty() &&
            item.provider == clicknova::accessibility::Provider::Uia;
        const bool cssClass = item.className.size() > 100 ||
            item.className.find(L' ') != std::wstring::npos ||
            item.className.find(L':') != std::wstring::npos;
        binding.elementClass = browserPage && cssClass ? std::string{} :
            QString::fromStdWString(item.className).toUtf8().toStdString();
        binding.elementRole = static_cast<int>(item.role);
        binding.elementProvider = static_cast<int>(item.provider);
        binding.elementBrowser = (*lastTarget)->browser && item.provider == clicknova::accessibility::Provider::Uia;
        binding.elementTab = binding.elementBrowser ?
            QString::fromStdWString((*lastTarget)->tab).toUtf8().toStdString() : std::string{};
        binding.elementOperation = static_cast<int>(op);
        binding.elementValue = parameter;
        actions_[currentRow] = command;
        refreshTable(static_cast<int>(currentRow));
        scheduleAutosave();
        reportStatus(trText("Accessibility action bound to step: ", "Действие элемента привязано к шагу: ") +
            QString::fromStdWString(item.automationId.empty() ? item.name : item.automationId));
        dialog.accept();
    });

    connect(find, &QPushButton::clicked, &dialog, [=, &dialog, ru = russian_] {
        const int selected = scope->currentData().toInt();
        if (selected < 0 || selected >= static_cast<int>(choices->size())) {
            state->setText(ru ? QString::fromUtf8("Выберите приложение или вкладку.") :
                QStringLiteral("Choose an application or tab."));
            return;
        }
        const UiSearchTarget selectedTarget = (*choices)[static_cast<std::size_t>(selected)];
        if (selectedTarget.devToolsOnly) return; // No native window identity: never guess.
        clicknova::accessibility::Query parameters;
        parameters.text = query->text().toStdWString();
        parameters.role = static_cast<clicknova::accessibility::Role>(role->currentData().toInt());
        parameters.browserOnly = selectedTarget.browser;
        // Large webpages routinely have far more than 80 accessible elements.
        // The backend bounds results and rejects invalid/oversized requests.
        parameters.limit = 400;
        parameters.browserTab = selectedTarget.tab;
        parameters.includeAnonymous = showTechnical->isChecked();
        parameters.visualTemplate = useImage->isChecked() ? templateFile->text().toStdWString() : L"";
        parameters.visualOcr = useOcr->isChecked();
        parameters.imageConfidence = imageScore->value();
        parameters.ocrConfidence = ocrScore->value();
        parameters.visualRadius = searchRadius->value();
        if (useImage->isChecked() && parameters.visualTemplate.empty()) {
            state->setText(ru ? QString::fromUtf8("Сначала выберите образец изображения.") :
                QStringLiteral("Choose an image template first."));
            return;
        }
        if (!clicknova::accessibility::validQuery(parameters) ||
            (!parameters.visualTemplate.empty() &&
             !clicknova::accessibility::visual::validTemplatePath(parameters.visualTemplate))) {
            state->setText(ru ? QString::fromUtf8("Слишком длинный запрос.") :
                                 QStringLiteral("Invalid search query."));
            return;
        }
        find->setEnabled(false);
        refreshSources->setEnabled(false);
        scope->setEnabled(false);
        sourceTree->setEnabled(false);
        *lastTarget = std::nullopt;
        *lastResults = {};
        bind->setEnabled(false);
        results->setRowCount(0);
        const bool visualScan = !parameters.visualTemplate.empty() || parameters.visualOcr;
        state->setText(ru ? QString::fromUtf8("Поиск элементов…") : QStringLiteral("Searching elements…"));
        // Temporarily put the game in front and make the selector transparent
        // so the desktop screenshot cannot include ClickNova's dialog.
        // A failed focus request must NEVER capture some unrelated window.
        if (visualScan) {
            dialog.setWindowOpacity(0.0);
            if (!clicknova::platform::activateWindow(selectedTarget.window)) {
                dialog.setWindowOpacity(1.0);
                state->setText(ru ? QString::fromUtf8("Не удалось активировать окно игры.") :
                    QStringLiteral("Could not focus the selected game window."));
                find->setEnabled(true);
                scope->setEnabled(true);
                sourceTree->setEnabled(true);
                refreshSources->setEnabled(true);
                return;
            }
        }
        auto outcome = std::make_shared<clicknova::accessibility::Result>();
        auto* thread = QThread::create([outcome, selectedTarget, parameters] {
            if (!selectedTarget.tab.empty() &&
                !clicknova::accessibility::selectBrowserTab(selectedTarget.window, selectedTarget.tab)) {
                outcome->error = "The browser did not allow selecting this tab via UI Automation.";
                return;
            }
            *outcome = clicknova::accessibility::search(selectedTarget.window, parameters);
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        connect(thread, &QThread::finished, &dialog, [=, &dialog] {
            if (visualScan) {
                dialog.setWindowOpacity(1.0);
                dialog.raise();
                dialog.activateWindow();
            }
            find->setEnabled(true);
            scope->setEnabled(true);
            sourceTree->setEnabled(true);
            if (!*tabLoading) refreshSources->setEnabled(true);
            if (!outcome->error.empty()) {
                const QString error = QString::fromStdString(outcome->error);
                state->setText(ru ?
                    (error.contains(QStringLiteral("tab"), Qt::CaseInsensitive) ?
                        QString::fromUtf8("Браузер не позволил выбрать эту вкладку через UIA. Откройте её вручную и повторите поиск.") :
                     error.contains(QStringLiteral("browser"), Qt::CaseInsensitive) ?
                        QString::fromUtf8("Выберите окно поддерживаемого браузера.") :
                     error.contains(QStringLiteral("unavailable"), Qt::CaseInsensitive) ?
                        QString::fromUtf8("UI Automation недоступна для выбранного окна.") :
                     error.contains(QStringLiteral("template"), Qt::CaseInsensitive) ?
                        QString::fromUtf8("Ошибка образца изображения: ") + error :
                     error.contains(QStringLiteral("Tesseract"), Qt::CaseInsensitive) ?
                        QString::fromUtf8("Ошибка распознавания OCR: ") + error :
                     error.contains(QStringLiteral("OpenCV"), Qt::CaseInsensitive) ?
                        QString::fromUtf8("Ошибка OpenCV: ") + error :
                        QString::fromUtf8("Не удалось получить элементы: ") + error) : error);
                return;
            }
            *lastResults = *outcome;
            *lastTarget = selectedTarget;
            results->setRowCount(static_cast<int>(outcome->elements.size()));
            for (std::size_t i = 0; i < outcome->elements.size(); ++i) {
                const auto& item = outcome->elements[i];
                const QString name = QString::fromStdWString(item.name);
                const QString automationId = QString::fromStdWString(item.automationId);
                const QString className = QString::fromStdWString(item.className);
                const int index = static_cast<int>(item.role);
                // Role::Any means an unclassified actual type, NOT the user's
                // "All" search filter. Ask UIA for its real localized type.
                const QString typeName = index > 0 && index < 13 ?
                    QString::fromUtf8(ru ? russian[index] : english[index]) :
                    (!item.localizedRole.empty() ? QString::fromStdWString(item.localizedRole) :
                        (ru ? QString::fromUtf8("Не указан") : QStringLiteral("Unspecified")));
                const QString providerLabel = QString::fromLatin1(clicknova::accessibility::providerName(item.provider));
                const std::array<QString, 5> columns = {
                    name.isEmpty() ? (ru ? QString::fromUtf8("(без имени)") : QStringLiteral("(unnamed)")) : name,
                    automationId.isEmpty() ? QString::fromUtf8("—") : automationId,
                    typeName + QStringLiteral(" · ") + providerLabel,
                    className.isEmpty() ? QString::fromUtf8("—") : className,
                    item.enabled ? (ru ? QString::fromUtf8("Да") : QStringLiteral("Yes")) :
                                   (ru ? QString::fromUtf8("Нет") : QStringLiteral("No"))};
                const QString explanation = ru ?
                    QString::fromUtf8("Имя: %1\nAutomationId: %2\nТип: %3\nКласс: %4\nВключён: %5\nПустые значения не предоставлены приложением.") :
                    QStringLiteral("Name: %1\nAutomationId: %2\nType: %3\nClass: %4\nEnabled: %5\nEmpty values are not provided by the application.");
                const QString detail = explanation.arg(columns[0], columns[1], columns[2], columns[3], columns[4]);
                for (int c = 0; c < 5; ++c) {
                    auto* cell = new QTableWidgetItem(columns[c]);
                    if (c == 0) {
                        using clicknova::accessibility::Role;
                        const clicknova::gui::Icon kind = item.role == Role::Button ||
                            item.role == Role::MenuItem ? clicknova::gui::Icon::Click :
                            item.role == Role::Edit ? clicknova::gui::Icon::Edit :
                            item.role == Role::Text ? clicknova::gui::Icon::Text :
                            item.role == Role::CheckBox || item.role == Role::RadioButton ?
                                clicknova::gui::Icon::Check : clicknova::gui::Icon::Target;
                        cell->setIcon(clicknova::gui::icon(kind));
                    }
                    cell->setToolTip(detail);
                    if ((c == 0 && name.isEmpty()) || (c == 1 && automationId.isEmpty()) ||
                        (c == 3 && className.isEmpty()))
                        cell->setForeground(QColor(QStringLiteral("#98a9bd")));
                    results->setItem(static_cast<int>(i), c, cell);
                }
            }
            state->setText(ru ? QString::fromUtf8("Показано: %1%2. Безымянные технические контейнеры %3.")
                    .arg(static_cast<qulonglong>(outcome->elements.size()))
                    .arg(outcome->truncated ? QString::fromUtf8(" (лимит %1)").arg(static_cast<qulonglong>(parameters.limit)) : QString())
                    .arg(parameters.includeAnonymous ? QString::fromUtf8("включены") : QString::fromUtf8("скрыты")) :
                QStringLiteral("Shown: %1%2. Unnamed technical containers are %3.")
                    .arg(static_cast<qulonglong>(outcome->elements.size()))
                    .arg(outcome->truncated ? QStringLiteral(" (limit %1)").arg(static_cast<qulonglong>(parameters.limit)) : QString())
                    .arg(parameters.includeAnonymous ? QStringLiteral("included") : QStringLiteral("hidden")));
        });
        if (visualScan) QTimer::singleShot(350, &dialog, [thread] { thread->start(); });
        else thread->start();
    });
    dialog.exec();
}

void InputLabWidget::clearStepUiElement() {
    if (isRunning() || selectedActionRows().size() != 1) return;
    const auto row = static_cast<std::size_t>(selectedActionRows().front());
    auto& binding = stepWindows_[row];
    if (binding.elementName.empty() && binding.elementAutomationId.empty()) return;
    checkpointActions();
    binding.elementName.clear();
    binding.elementAutomationId.clear();
    binding.elementClass.clear();
    binding.elementRole = 0;
    binding.elementProvider = 0;
    binding.elementBrowser = false;
    binding.elementTab.clear();
    binding.elementOperation = 0;
    binding.elementValue.clear();
    if (std::holds_alternative<ElementTask>(actions_[row]))
        actions_[row] = clicknova::input::Wait{0};
    refreshTable(static_cast<int>(row));
    scheduleAutosave();
}

void InputLabWidget::clearStepWindow() {
    if (isRunning() || selectedActionRows().size() != 1) return;
    const auto row = static_cast<std::size_t>(selectedActionRows().front());
    if (stepWindows_[row].title.empty()) return;
    checkpointActions();
    const auto groupId = stepWindows_[row].manualGroupId;
    const auto groupTitle = stepWindows_[row].manualGroupTitle;
    stepWindows_[row] = {};
    stepWindows_[row].manualGroupId = groupId;
    stepWindows_[row].manualGroupTitle = groupTitle;
    if (std::holds_alternative<ElementTask>(actions_[row]))
        actions_[row] = clicknova::input::Wait{0};
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
        binding.elementName.clear();
        binding.elementAutomationId.clear();
        binding.elementClass.clear();
        binding.elementRole = 0;
    binding.elementProvider = 0;
        binding.elementBrowser = false;
        binding.elementTab.clear();
        binding.elementOperation = 0;
        binding.elementValue.clear();
        if (std::holds_alternative<ElementTask>(actions_[static_cast<std::size_t>(row)]))
            actions_[static_cast<std::size_t>(row)] = clicknova::input::Wait{0};
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
    if (binding.enforce && !binding.activate &&
        (!binding.elementAutomationId.empty() || !binding.elementName.empty()) &&
        !clicknova::input::backgroundElementAction(binding)) {
        reportStatus(trText("This element needs an active foreground window.",
            "Для этого элемента требуется активное окно."));
        refreshStepWindowEditor(static_cast<int>(row));
        return;
    }
    binding.waitMs = binding.enforce ? ms : 0;
    if (!binding.enforce) {
        binding.elementName.clear();
        binding.elementAutomationId.clear();
        binding.elementClass.clear();
        binding.elementRole = 0;
    binding.elementProvider = 0;
        binding.elementBrowser = false;
        binding.elementTab.clear();
        binding.elementOperation = 0;
        binding.elementValue.clear();
    }
    if (stepWindows_[row] == binding) return;
    checkpointActions();
    stepWindows_[row] = std::move(binding);
    if (!stepWindows_[row].enforce && std::holds_alternative<ElementTask>(actions_[row]))
        actions_[row] = clicknova::input::Wait{0};
    refreshTable(static_cast<int>(row));
    refreshStepWindowEditor(static_cast<int>(row));
    scheduleAutosave();
}

bool InputLabWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == stepResizeGrip_ && event) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                resizingSteps_ = true;
                gripStartY_ = mouse->globalPosition().toPoint().y();
                gripStartHeight_ = table_->height();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove && resizingSteps_) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (!(mouse->buttons() & Qt::LeftButton)) { resizingSteps_ = false; return true; }
            resizeStepList(gripStartHeight_ + mouse->globalPosition().toPoint().y() - gripStartY_ - table_->height());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && resizingSteps_) {
            resizingSteps_ = false;
            return true;
        }
    }
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
    if (hasRow && row < static_cast<int>(groupEndByStart_.size()) && groupEndByStart_[row] > row + 1) {
        auto* selectGroup = item("Select entire consecutive group", "Выделить всю группу подряд",
            clicknova::gui::Icon::SelectAll, "stepSelectConsecutiveGroup");
        connect(selectGroup, &QAction::triggered, this, [this, row] {
            std::vector<int> rows;
            for (int child = row; child < groupEndByStart_[row]; ++child) rows.push_back(child);
            // Clear transient filters and expand before selecting all real rows.
            stepSearch_->clear();
            stepFilter_->setCurrentIndex(0);
            collapsedStepGroups_.remove(row);
            applyStepVisibility();
            refreshSelectedRows(rows);
        });
        menu->addSeparator();
    }
    if (hasRow) {
        const int namedStart = row < static_cast<int>(manualStartByRow_.size()) ?
            manualStartByRow_[row] : -1;
        if (namedStart >= 0) {
            auto* selectNamed = item("Select entire named group", "Выделить всю именованную группу",
                clicknova::gui::Icon::SelectAll, "stepSelectManualGroup");
            connect(selectNamed, &QAction::triggered, this, [this, namedStart] {
                stepSearch_->clear(); stepFilter_->setCurrentIndex(0);
                collapsedManualGroups_.remove(QString::fromStdString(stepWindows_[namedStart].manualGroupId));
                std::vector<int> rows;
                for (int child = namedStart; child < manualEndByStart_[namedStart]; ++child) {
                    const int autoParent = groupStartByRow_[child];
                    if (autoParent >= 0) collapsedStepGroups_.remove(autoParent);
                    rows.push_back(child);
                }
                applyStepVisibility();
                refreshSelectedRows(rows);
            });
        }
        int groupFirst = -1, groupEnd = -1;
        if (selectedManualGroup(groupFirst, groupEnd)) {
            auto* rename = item("Rename group", "Переименовать группу",
                clicknova::gui::Icon::Edit, "stepRenameManualGroup");
            connect(rename, &QAction::triggered, this, [this] { renameManualGroup(); });
            auto* ungroup = item("Ungroup", "Разгруппировать",
                clicknova::gui::Icon::Close, "stepUngroupManualGroup");
            connect(ungroup, &QAction::triggered, this, [this] { ungroupManualSteps(); });
        }
        auto* create = item("Group consecutive selected steps", "Сгруппировать выделенные шаги",
            clicknova::gui::Icon::Folder, "stepCreateManualGroup");
        const auto selected = selectedActionRows();
        create->setEnabled(!selected.empty() &&
            selected.back() - selected.front() + 1 == static_cast<int>(selected.size()));
        connect(create, &QAction::triggered, this, [this] { createManualGroup(); });
        menu->addSeparator();
    }
    // Each operation is present only once: labels change with selection size.
    // Select All is useful only when at least one row is not selected.
    if (!actions_.empty() && selectionCount < static_cast<int>(actions_.size())) {
        auto* selectAll = item("Select all", "Выделить всё", clicknova::gui::Icon::SelectAll,
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
        auto* first = item("Move to beginning", "В самое начало", clicknova::gui::Icon::MoveTop,
                           "stepFirst");
        first->setEnabled(row > 0);
        connect(first, &QAction::triggered, this, [this, row] { moveActionRow(row, 0); });
        auto* last = item("Move to end", "В самый конец", clicknova::gui::Icon::MoveBottom,
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
        auto* pasteBefore = item("Paste before", "Вставить перед", clicknova::gui::Icon::Paste,
                                 "stepPasteBefore", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
        pasteBefore->setEnabled(actions_.size() < 512 && canPasteAction());
        connect(pasteBefore, &QAction::triggered, this, [this, row] { pasteActionAt(row); });
    }
    if (multiple) {
        auto* before = item("Paste before selection", "Вставить перед выделенными",
            clicknova::gui::Icon::Paste, "stepPasteBefore", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
        before->setEnabled(actions_.size() < 512 && canPasteAction());
        const auto rows = selectedActionRows();
        const int first = *std::min_element(rows.begin(), rows.end());
        connect(before, &QAction::triggered, this, [this, first] { pasteActionAt(first); });
    }
    auto* paste = item(!hasRow ? "Paste at end" : multiple ? "Paste after selection" : "Paste after",
                       !hasRow ? "Вставить в конец" : multiple ? "Вставить после выделенных" : "Вставить после",
                       clicknova::gui::Icon::Paste, "stepPaste", QKeySequence(QKeySequence::Paste));
    paste->setEnabled(actions_.size() < 512 && canPasteAction());
    const auto rows = selectedActionRows();
    const int at = !hasRow ? static_cast<int>(actions_.size()) :
        multiple ? *std::max_element(rows.begin(), rows.end()) + 1 : row + 1;
    connect(paste, &QAction::triggered, this, [this, at] { pasteActionAt(at); });
    menu->addSeparator();
    auto* undo = item("Undo", "Отменить", clicknova::gui::Icon::Undo,
                      "listUndo", QKeySequence(QKeySequence::Undo));
    undo->setEnabled(actionHistory_.canUndo());
    connect(undo, &QAction::triggered, this, [this] { undoActionEdit(); });
    auto* redo = item("Redo", "Повторить", clicknova::gui::Icon::Redo,
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

void InputLabWidget::reportActionChange(const QString& english, const QString& russian, int count) {
    // Summaries only: neither typed text nor PowerShell step contents enter the
    // shared activity log. The current step count makes undo/redo analysis easier.
    reportStatus((russian_ ? russian : english).arg(count).arg(static_cast<qulonglong>(actions_.size())));
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
    const int selectedRows = static_cast<int>(selectedActionRows().size());
    const auto chosen = selectedActionRows();
    const bool contiguous = !chosen.empty() && chosen.back() - chosen.front() + 1 == static_cast<int>(chosen.size());
    createGroup_->setEnabled(idle && contiguous);
    int groupFirst = -1, groupEnd = -1;
    const bool hasGroup = selectedManualGroup(groupFirst, groupEnd);
    renameGroup_->setEnabled(idle && hasGroup);
    ungroupSteps_->setEnabled(idle && hasGroup);
    duplicateStep_->setEnabled(idle && selectedRows > 0 && actions_.size() + selectedRows <= 512);
    deleteStep_->setEnabled(idle && selectedRows > 0);
    moveStepUp_->setEnabled(idle && selectedRows > 0);
    moveStepDown_->setEnabled(idle && selectedRows > 0);
    undoStep_->setEnabled(idle && actionHistory_.canUndo());
    redoStep_->setEnabled(idle && actionHistory_.canRedo());
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
        // Importing actions is not a user row selection. Leave the table
        // unselected until the user explicitly chooses a step to edit.
        refreshTable(-1);
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
    collapsedManualGroups_.clear();
    collapsedStepGroups_.clear();
    actions_ = plan.actions;
    stepWindows_ = plan.stepWindows;
    normalizeStepWindows();
    repetitions_->setValue(plan.repetitions);
    passDelay_->setText(QString::number(plan.passDelayMs));
    // Loading a plan is not a user selection; never highlight row zero implicitly.
    refreshTable(-1);
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
    // A read-only preflight warns about accidental mass clicks, extremely long
    // runs and local code. It never silently changes any user actions.
    const auto safety = clicknova::input::preflight(plan);
    if (safety.confirmationRecommended) {
        QStringList warnings;
        for (const auto& issue : safety.issues) {
            QString text;
            switch (issue.risk) {
            case clicknova::input::PreflightRisk::UnboundForegroundInput:
                text = trText("Input is not restricted to a target window.",
                    "Ввод не ограничен выбранным окном."); break;
            case clicknova::input::PreflightRisk::LocalScript:
                text = trText("Local PowerShell code will run with your user permissions. Only run code you trust.",
                    "Локальный PowerShell-код будет выполнен с правами вашей учётной записи. Запускайте только доверенный код."); break;
            case clicknova::input::PreflightRisk::ManyClicks:
                text = trText("High click count (%1 across all passes).",
                    "Много кликов (%1 за все проходы).")
                        .arg(QString::number(safety.estimatedClickCount)); break;
            case clicknova::input::PreflightRisk::LongMinimumDuration:
                text = trText("The scenario has at least %1 minutes of configured waits/delays.",
                    "В сценарии минимум %1 минут ожиданий и задержек.")
                        .arg(safety.minimumDurationMs / 60000); break;
            case clicknova::input::PreflightRisk::ManualHeldInput:
                text = trText("A key or mouse button is held between steps; a crash could leave it pressed.",
                    "Клавиша или кнопка удерживается между шагами; аварийное завершение может оставить её нажатой."); break;
            }
            warnings.push_back(trText("Step %1: %2", "Шаг %1: %2")
                .arg(static_cast<qulonglong>(issue.stepIndex + 1)).arg(text));
        }
        const QString prompt = trText("Check this scenario before running:\n\n%1\n\nContinue?",
            "Проверьте сценарий перед запуском:\n\n%1\n\nПродолжить?").arg(warnings.join(QStringLiteral("\n")));
        if (QMessageBox::warning(this, QStringLiteral("ClickNova"), prompt,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    }
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
#if !defined(_WIN32) && !defined(CLICKNOVA_WITH_X11)
    reportStatus(trText("Input playback requires Windows or Linux X11 with XTEST.",
        "Для ввода нужна Windows либо Linux X11 с XTEST."));
    return;
#else
#ifdef CLICKNOVA_WITH_X11
    if (!x11Stop_) x11Stop_ = std::make_unique<X11EmergencyStop>();
    if (!x11Stop_->start([this] {
            QMetaObject::invokeMethod(this, [this] { stopPlayback(); }, Qt::QueuedConnection);
        })) {
        reportStatus(trText("Cannot reserve global F12 on X11. Check the X11 session and shortcuts.",
            "Не удалось зарезервировать F12 в X11. Проверьте сеанс и занятые сочетания."));
        return;
    }
    { // Do not start unsupported UIA, PowerShell, or browser-element actions on X11.
        std::size_t row = 0;
        for (const auto& action : actions_) {
            if (std::holds_alternative<RunScript>(action) || std::holds_alternative<ElementTask>(action) ||
                (row < stepWindows_.size() &&
                 (!stepWindows_[row].elementName.empty() ||
                  !stepWindows_[row].elementAutomationId.empty() ||
                  !stepWindows_[row].elementClass.empty() ||
                  !stepWindows_[row].elementTab.empty() ||
                  !stepWindows_[row].elementValue.empty() ||
                  stepWindows_[row].elementBrowser || stepWindows_[row].elementProvider != 0 ||
                  stepWindows_[row].elementRole != 0 || stepWindows_[row].elementOperation != 0))) {
                x11Stop_->stop();
                reportStatus(trText("Linux X11 does not support this scripted/accessibility action. Step %1.",
                    "Шаг %1: сценарий/действие с элементом ещё не поддерживается в X11.")
                    .arg(static_cast<qulonglong>(row + 1)));
                return;
            }
            ++row;
        }
    }
#else
    if (!stopHotkeyController_ || !stopHotkeyController_(true)) {
        reportStatus(trText("Cannot reserve the Stop shortcut. Choose a free Stop key in Settings.",
            "Не удалось зарегистрировать клавишу остановки. Выберите свободную клавишу в настройках."));
        return;
    }
#endif
#ifdef CLICKNOVA_WITH_X11
    { // XTEST must be available before starting the three-second countdown.
        X11InputBackend probe(x11Stop_->triggered(), targetWindow_);
        if (!probe.ready()) {
            x11Stop_->stop();
            reportStatus(trText("XTEST input injection is unavailable. Install libXtst and use an X11 session.",
                "XTEST недоступен. Установите libXtst и используйте сеанс X11."));
            return;
        }
    }
#endif
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
    const QString stopKey =
#ifdef CLICKNOVA_WITH_X11
        QStringLiteral("F12");
#else
        stopHotkeyLabel_ ? stopHotkeyLabel_() : QStringLiteral("Stop");
#endif
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
#if defined(_WIN32) || defined(CLICKNOVA_WITH_X11)
#ifdef _WIN32
        ScriptBackend backend(transientTarget);
        clicknova::platform::WindowsAwakeGuard awake(keepWindowsAwake);
#else
        (void)keepWindowsAwake;
        X11InputBackend backend(x11Stop_->triggered(), transientTarget);
#endif
        SteadyWaiter waiter;
#ifdef _WIN32
        const bool inputAvailable = true;
#else
        const bool inputAvailable = backend.ready();
#endif
        const Result result = inputAvailable
            ? play(plan, backend, waiter, token,
                [this, generation](const clicknova::input::PlaybackProgress& event) {
                    QMetaObject::invokeMethod(this, [this, generation, event] {
                        if (generation == playbackGeneration_ && running_)
                            onPlaybackProgress(event);
                    }, Qt::QueuedConnection);
                })
            : Result{Status::DispatchFailed, "X11 XTEST is unavailable"};
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
#ifdef CLICKNOVA_WITH_X11
        if (x11Stop_) x11Stop_->stop();
#endif
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
#ifdef CLICKNOVA_WITH_X11
    if (x11Stop_) x11Stop_->stop();
#endif
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
    if (result.status == Status::DispatchFailed && result.failedStepIndex) {
        outcome += trText(" · step %1, pass %2", " · шаг %1, проход %2")
            .arg(static_cast<qulonglong>(*result.failedStepIndex + 1)).arg(result.failedPassNumber);
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
    return clicknova::portable::filePath(QStringLiteral("clicknova-steps-autosave.json"));
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
    collapsedManualGroups_.clear();
    collapsedStepGroups_.clear();
    actions_ = std::move(restored);
    stepWindows_ = std::move(restoredWindows);
    requireWindowBinding_ = draft.value(QStringLiteral("requireTargetWindow")).toBool();
    targetWindow_.reset(); // Never reuse stale HWND from a previous run.
    repetitions_->setValue(repetitions);
    passDelay_->setText(delay);
    // Opening an entire plan must not silently highlight its first action.
    refreshTable(-1);
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
