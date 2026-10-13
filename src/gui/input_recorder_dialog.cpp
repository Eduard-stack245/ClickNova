// SPDX-License-Identifier: Apache-2.0
#include "input_recorder_dialog.hpp"
#include "ui_icons.hpp"
#include "recording_checkpoint.hpp"
#include <QCheckBox>
#include <QCloseEvent>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

using clicknova::recording::Kind;
using clicknova::recording::Sample;

namespace {
std::uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
}

// Low-level Win32 callbacks execute on the installing (GUI) thread. Ignore
// ClickNova windows and synthesized input. Never block or consume the event.
class InputRecorderDialog::NativeHooks final {
public:
    explicit NativeHooks(std::function<void(const Sample&)> handler)
        : handler_(std::move(handler)) {}
    bool start(bool keyboard, bool text) {
#ifdef _WIN32
        if (active_) return false;
        textEnabled_ = text;
        lastWindow_ = nullptr;
        lastWindowTick_ = 0;
        lastWindowInfo_ = {};
        textKeys_.clear();
        downKeys_.clear();
        active_ = this;
        mouse_ = SetWindowsHookExW(WH_MOUSE_LL, &mouseProc, GetModuleHandleW(nullptr), 0);
        if (keyboard) keyboard_ = SetWindowsHookExW(WH_KEYBOARD_LL, &keyProc, GetModuleHandleW(nullptr), 0);
        if (!mouse_ || (keyboard && !keyboard_)) { stop(); return false; }
        return true;
#else
        (void)keyboard;
        (void)text;
        return false;
#endif
    }
    void stop() {
#ifdef _WIN32
        if (mouse_) { UnhookWindowsHookEx(mouse_); mouse_ = nullptr; }
        if (keyboard_) { UnhookWindowsHookEx(keyboard_); keyboard_ = nullptr; }
        textKeys_.clear();
        downKeys_.clear();
        if (active_ == this) active_ = nullptr;
#endif
    }
    ~NativeHooks() { stop(); }
private:
    std::function<void(const Sample&)> handler_;
#ifdef _WIN32
    HHOOK mouse_{};
    HHOOK keyboard_{};
    bool textEnabled_{true};
    std::set<DWORD> textKeys_;
    std::set<DWORD> downKeys_;
    HWND lastWindow_{};
    std::uint64_t lastWindowTick_{};
    clicknova::recording::WindowInfo lastWindowInfo_;
    static inline NativeHooks* active_ = nullptr;
    static bool belongsToUs(HWND hwnd) {
        if (!hwnd) return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        return pid == GetCurrentProcessId();
    }
    static clicknova::recording::WindowInfo describeWindow(HWND hwnd) {
        clicknova::recording::WindowInfo out;
        if (!hwnd) return out;
        HWND root = GetAncestor(hwnd, GA_ROOT);
        if (root) hwnd = root;
        wchar_t title[260]{};
        const int length = GetWindowTextW(hwnd, title, 260);
        if (length > 0) out.title.assign(reinterpret_cast<const char16_t*>(title),
                                          static_cast<std::size_t>(length));
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid) {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (process) {
                wchar_t path[MAX_PATH]{}; DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(process, 0, path, &size)) {
                    const wchar_t* name = wcsrchr(path, L'\\');
                    name = name ? name + 1 : path;
                    out.process.assign(reinterpret_cast<const char16_t*>(name));
                }
                CloseHandle(process);
            }
        }
        return out;
    }
    clicknova::recording::WindowInfo cachedWindowInfo(HWND hwnd, std::uint64_t tick) {
        if (hwnd) {
            if (HWND root = GetAncestor(hwnd, GA_ROOT)) hwnd = root;
        }
        // Window titles/process information require cross-process Windows API
        // calls. Avoid repeating them on each mouse-move hook notification.
        if (hwnd == lastWindow_ && tick >= lastWindowTick_ &&
            tick - lastWindowTick_ < 500) return lastWindowInfo_;
        lastWindow_ = hwnd;
        lastWindowTick_ = tick;
        lastWindowInfo_ = describeWindow(hwnd);
        return lastWindowInfo_;
    }
    static std::u16string printableText(const KBDLLHOOKSTRUCT& e, HWND hwnd,
                                        bool shortcutModifierHeld, bool shiftHeld) {
        if (!hwnd || shortcutModifierHeld || (GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
            (GetAsyncKeyState(VK_MENU) & 0x8000) ||
            (GetAsyncKeyState(VK_LWIN) & 0x8000) ||
            (GetAsyncKeyState(VK_RWIN) & 0x8000)) return {};
        BYTE state[256]{};
        if (shiftHeld || (GetAsyncKeyState(VK_SHIFT) & 0x8000)) state[VK_SHIFT] = 0x80;
        if (GetKeyState(VK_CAPITAL) & 1) state[VK_CAPITAL] = 1;
        const DWORD thread = GetWindowThreadProcessId(hwnd, nullptr);
        const HKL layout = GetKeyboardLayout(thread);
        wchar_t chars[8]{};
        // Bit 2 prevents ToUnicodeEx from modifying the keyboard dead-key state
        // on Windows 10 1607 and newer. Unsupported dead keys stay physical.
        const int count = ToUnicodeEx(e.vkCode, e.scanCode, state, chars, 8, 4, layout);
        if (count < 1 || count > 8) return {};
        for (int i = 0; i < count; ++i) if (chars[i] < 0x20 || chars[i] == 0x7f) return {};
        return {reinterpret_cast<const char16_t*>(chars), static_cast<std::size_t>(count)};
    }
    static LRESULT CALLBACK mouseProc(int code, WPARAM message, LPARAM lparam) {
        if (code == HC_ACTION && active_) {
            const auto* e = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
            const HWND target = WindowFromPoint(e->pt);
            if (!(e->flags & LLMHF_INJECTED) && !belongsToUs(target)) {
                Sample s;
                s.milliseconds = nowMs();
                s.x = e->pt.x; s.y = e->pt.y;
                s.window = active_->cachedWindowInfo(target, s.milliseconds);
                bool send = true;
                switch (message) {
                case WM_MOUSEMOVE: s.kind = Kind::Move; break;
                case WM_LBUTTONDOWN: s.kind = Kind::MouseDown; s.button = clicknova::input::Button::Left; break;
                case WM_LBUTTONUP: s.kind = Kind::MouseUp; s.button = clicknova::input::Button::Left; break;
                case WM_RBUTTONDOWN: s.kind = Kind::MouseDown; s.button = clicknova::input::Button::Right; break;
                case WM_RBUTTONUP: s.kind = Kind::MouseUp; s.button = clicknova::input::Button::Right; break;
                case WM_MBUTTONDOWN: s.kind = Kind::MouseDown; s.button = clicknova::input::Button::Middle; break;
                case WM_MBUTTONUP: s.kind = Kind::MouseUp; s.button = clicknova::input::Button::Middle; break;
                case WM_XBUTTONDOWN: case WM_XBUTTONUP:
                    s.kind = message == WM_XBUTTONDOWN ? Kind::MouseDown : Kind::MouseUp;
                    s.button = HIWORD(e->mouseData) == XBUTTON1 ? clicknova::input::Button::X1 : clicknova::input::Button::X2;
                    break;
                case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                    s.kind = Kind::Wheel;
                    s.axis = message == WM_MOUSEWHEEL ? clicknova::input::Axis::Vertical : clicknova::input::Axis::Horizontal;
                    s.delta = static_cast<short>(HIWORD(e->mouseData));
                    break;
                default: send = false; break;
                }
                if (send) active_->handler_(s);
            }
        }
        return CallNextHookEx(nullptr, code, message, lparam);
    }
    static bool nativePasswordFocused() {
        HWND foreground = GetForegroundWindow();
        if (!foreground) return true; // Do not record without a known target.
        DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        // Web and elevated targets may not expose their GUI focus through
        // this API. An explicitly started recording must still capture normal
        // shortcuts in those programs. Users are warned not to enter secrets.
        if (!GetGUIThreadInfo(threadId, &info)) return false;
        if (!info.hwndFocus) return false;
        wchar_t className[80]{};
        GetClassNameW(info.hwndFocus, className, 80);
        // ES_PASSWORD is an edit-control style. The same low bit can mean
        // something else for arbitrary window classes, so don't discard
        // ordinary browser typing based only on a coincidental style bit.
        const bool nativeEdit = _wcsicmp(className, L"Edit") == 0 ||
            _wcsnicmp(className, L"RichEdit", 8) == 0;
        return nativeEdit && ((GetWindowLongPtrW(info.hwndFocus, GWL_STYLE) & ES_PASSWORD) != 0);
    }
    static LRESULT CALLBACK keyProc(int code, WPARAM message, LPARAM lparam) {
        if (code == HC_ACTION && active_ && !belongsToUs(GetForegroundWindow()) &&
            !nativePasswordFocused()) {
            const auto* e = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
            if (!(e->flags & LLKHF_INJECTED) && e->vkCode >= 0x08 && e->vkCode <= 0xFE &&
                (message == WM_KEYDOWN || message == WM_SYSKEYDOWN ||
                 message == WM_KEYUP || message == WM_SYSKEYUP)) {
                Sample s;
                s.milliseconds = nowMs();
                s.kind = message == WM_KEYDOWN || message == WM_SYSKEYDOWN ? Kind::KeyDown : Kind::KeyUp;
                s.virtualKey = static_cast<int>(e->vkCode);
                s.window = active_->cachedWindowInfo(GetForegroundWindow(), s.milliseconds);
                auto& pressed = active_->downKeys_;
                if (s.kind == Kind::KeyDown) pressed.insert(e->vkCode);
                else pressed.erase(e->vkCode);
                const auto held = [&pressed](DWORD key) { return pressed.contains(key); };
                const bool shortcutHeld = held(VK_CONTROL) || held(VK_LCONTROL) || held(VK_RCONTROL) ||
                    held(VK_MENU) || held(VK_LMENU) || held(VK_RMENU) ||
                    held(VK_LWIN) || held(VK_RWIN);
                const bool shiftHeld = held(VK_SHIFT) || held(VK_LSHIFT) || held(VK_RSHIFT);
                if (active_->textEnabled_) {
                    if (s.kind == Kind::KeyDown) {
                        s.text = printableText(*e, GetForegroundWindow(), shortcutHeld, shiftHeld);
                        if (!s.text.empty()) {
                            active_->textKeys_.insert(e->vkCode);
                            s.kind = Kind::Text;
                        }
                    } else if (active_->textKeys_.erase(e->vkCode)) {
                        return CallNextHookEx(nullptr, code, message, lparam);
                    }
                }
                active_->handler_(s);
            }
        }
        return CallNextHookEx(nullptr, code, message, lparam);
    }
#endif
};

InputRecorderDialog::InputRecorderDialog(bool russian, Finished finished)
    : QDialog(nullptr), russian_(russian), finished_(std::move(finished)) {
    setObjectName(QStringLiteral("inputRecorderOverlay"));
    setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    setWindowTitle(russian_ ? QString::fromUtf8("ClickNova — запись действий") : QStringLiteral("ClickNova — Record actions"));
    setMinimumWidth(360);
    auto* layout = new QVBoxLayout(this);
    info_ = new QLabel;
    info_->setWordWrap(true);
    info_->setObjectName(QStringLiteral("sectionHint"));
    layout->addWidget(info_);
    keyboard_ = new QCheckBox(russian_ ? QString::fromUtf8("Записывать клавиатуру (не вводите пароли)")
                                     : QStringLiteral("Record keyboard (do not type passwords)"));
    keyboard_->setObjectName(QStringLiteral("recorderKeyboard"));
    keyboard_->setChecked(QSettings().value(QStringLiteral("recorder/keyboard"), true).toBool());
    layout->addWidget(keyboard_);
    text_ = new QCheckBox(russian_ ? QString::fromUtf8("Собирать обычный набор в текстовые шаги")
                                    : QStringLiteral("Combine typing into text steps"));
    text_->setObjectName(QStringLiteral("recorderTypingText"));
    text_->setChecked(QSettings().value(QStringLiteral("recorder/text"), true).toBool());
    text_->setToolTip(russian_ ? QString::fromUtf8("Ctrl/Alt/Win-комбинации остаются нажатиями клавиш. Не вводите пароли при записи.")
                              : QStringLiteral("Ctrl/Alt/Win combinations stay as key presses. Do not enter passwords during recording."));
    text_->setEnabled(keyboard_->isChecked());
    connect(keyboard_, &QCheckBox::toggled, text_, &QWidget::setEnabled);
    layout->addWidget(text_);
    window_ = new QLabel;
    window_->setObjectName(QStringLiteral("recorderActiveWindow"));
    window_->setWordWrap(true);
    layout->addWidget(window_);
    movement_ = new QCheckBox(russian_ ? QString::fromUtf8("Записывать движение курсора")
                                     : QStringLiteral("Record cursor movement"));
    movement_->setObjectName(QStringLiteral("recorderMovement"));
    movement_->setChecked(true);
    movement_->setToolTip(russian_ ? QString::fromUtf8("Если выключено, клики всё равно сохраняют свои координаты. Удобно для записи только нажатий.")
                                  : QStringLiteral("When disabled, clicks still capture their positions. Useful for recording only button actions."));
    layout->addWidget(movement_);
    count_ = new QLabel;
    count_->setObjectName(QStringLiteral("recorderCounter"));
    layout->addWidget(count_);
    auto* recoveryHint = new QLabel(russian_
        ? QString::fromUtf8("Черновики записи сохраняются локально с датой и временем. Они могут содержать введённый текст — не вводите пароли.")
        : QStringLiteral("Recording recovery is saved locally with timestamps. It may contain typed text; do not type passwords."));
    recoveryHint->setWordWrap(true);
    layout->addWidget(recoveryHint);
    auto* buttons = new QHBoxLayout;
    start_ = new QPushButton(russian_ ? QString::fromUtf8("Запись") : QStringLiteral("Record"));
    start_->setObjectName(QStringLiteral("recorderStart"));
    start_->setProperty("actionRole", QStringLiteral("record"));
    pause_ = new QPushButton;
    pause_->setObjectName(QStringLiteral("recorderPause"));
    pause_->setProperty("actionRole", QStringLiteral("secondary"));
    stop_ = new QPushButton(russian_ ? QString::fromUtf8("Просмотр") : QStringLiteral("Review"));
    stop_->setObjectName(QStringLiteral("recorderStop"));
    stop_->setProperty("actionRole", QStringLiteral("success"));
    cancel_ = new QPushButton(russian_ ? QString::fromUtf8("Отмена") : QStringLiteral("Cancel"));
    cancel_->setObjectName(QStringLiteral("recorderCancel"));
    cancel_->setProperty("actionRole", QStringLiteral("danger"));
    start_->setToolTip(russian_ ? QString::fromUtf8("Начать запись выбранных действий.") : QStringLiteral("Start recording selected input events."));
    pause_->setToolTip(russian_ ? QString::fromUtf8("Приостановить запись или продолжить без учёта времени паузы.") : QStringLiteral("Pause or resume without recording the paused time."));
    stop_->setToolTip(russian_ ? QString::fromUtf8("Остановить и открыть просмотр. Нажатие кнопки не записывается.") : QStringLiteral("Stop and review. Pressing this control is not recorded."));
    cancel_->setToolTip(russian_ ? QString::fromUtf8("Отменить запись, ничего не меняя в сценарии.") : QStringLiteral("Discard the recording without changing the current plan."));
    clicknova::gui::setButtonIcon(start_, clicknova::gui::Icon::Record, QColor(QStringLiteral("#ffffff")));
    clicknova::gui::setButtonIcon(pause_, clicknova::gui::Icon::Pause);
    clicknova::gui::setButtonIcon(stop_, clicknova::gui::Icon::Check, QColor(QStringLiteral("#d8fff1")));
    clicknova::gui::setButtonIcon(cancel_, clicknova::gui::Icon::Close, QColor(QStringLiteral("#ffcbd2")));
    for (auto* b : {start_, pause_, stop_, cancel_}) { b->setMinimumHeight(38); buttons->addWidget(b); }
    layout->addLayout(buttons);
    hooks_ = new NativeHooks([this](const Sample& s) { record(s); });
    progressTimer_ = new QTimer(this);
    progressTimer_->setInterval(300);
    connect(progressTimer_, &QTimer::timeout, this, [this] { updateProgress(); maybeCheckpoint(); });
    connect(start_, &QPushButton::clicked, this, [this] { start(); });
    connect(pause_, &QPushButton::clicked, this, [this] { pauseOrResume(); });
    connect(stop_, &QPushButton::clicked, this, [this] { stop(false); });
    connect(cancel_, &QPushButton::clicked, this, [this] { stop(true); });
    const auto stored = QSettings().value(QStringLiteral("recorder/panelPosition"));
    if (stored.isValid()) move(stored.toPoint());
    else if (auto* screen = QGuiApplication::primaryScreen()) {
        const QRect rect = screen->availableGeometry();
        move(rect.right() - 500, rect.top() + 70);
    }
    updateUi();
}
InputRecorderDialog::~InputRecorderDialog() { delete hooks_; }
void InputRecorderDialog::start() {
    if (started_ || completed_) return;
    keyboardEnabled_ = keyboard_->isChecked();
    textEnabled_ = text_->isChecked();
    QSettings().setValue(QStringLiteral("recorder/keyboard"), keyboardEnabled_);
    QSettings().setValue(QStringLiteral("recorder/text"), textEnabled_);
    movementEnabled_ = movement_->isChecked();
    heldButtons_.clear();
    heldKeys_.clear();
    samples_.clear();
    checkpointFile_ = clicknova::gui::recording_checkpoint::newPath();
    checkpointStarted_ = QDateTime::currentDateTime();
    lastCheckpointTick_ = 0;
    checkpointPending_ = false;
    checkpointErrorShown_ = false;
    previousMeaning_ = -1;
    previousWindow_ = {};
    startedTick_ = nowMs();
    totalPaused_ = 0;
    lastMoveTick_ = 0;
    if (!hooks_->start(keyboardEnabled_, textEnabled_)) {
        info_->setText(russian_ ? QString::fromUtf8("Не удалось включить перехват Windows. Попробуйте закрыть другие программы захвата.")
                                : QStringLiteral("Could not start Windows hooks. Close other input capture tools."));
        return;
    }
    started_ = true;
    progressTimer_->start();
    updateUi();
}
void InputRecorderDialog::pauseOrResume() {
    if (!started_ || completed_) return;
    if (!paused_) {
        pausedTick_ = nowMs();
        hooks_->stop();
        closeRecordedHolds();
        paused_ = true;
        maybeCheckpoint(true);
    } else {
        if (!hooks_->start(keyboardEnabled_, textEnabled_)) {
            info_->setText(russian_ ? QString::fromUtf8("Не удалось продолжить запись.")
                                    : QStringLiteral("Could not resume recording."));
            return;
        }
        totalPaused_ += nowMs() - pausedTick_;
        paused_ = false;
    }
    updateUi();
}
void InputRecorderDialog::stop(bool cancelled) {
    if (completed_) return;
    hooks_->stop();
    progressTimer_->stop();
    if (started_ && !paused_) {
        pausedTick_ = nowMs();
        closeRecordedHolds();
    }
    const std::uint64_t endTick = paused_ ? pausedTick_ : nowMs();
    const std::uint64_t effectiveStart = startedTick_ + totalPaused_;
    const std::uint64_t recordedDurationMs = started_ && endTick >= effectiveStart
        ? endTick - effectiveStart : 0;
    if (started_ && !cancelled) maybeCheckpoint(true);
    if (cancelled) clicknova::gui::recording_checkpoint::remove(checkpointFile_);
    completed_ = true;
    QSettings().setValue(QStringLiteral("recorder/panelPosition"), pos());
    hide(); // Never cover the import-choice dialog with this topmost panel.
    auto finished = std::move(finished_);
    if (finished) finished(cancelled ? std::vector<Sample>{} : std::move(samples_), cancelled, recordedDurationMs);
    deleteLater();
}
void InputRecorderDialog::record(const Sample& event) {
    if (!started_ || paused_ || completed_) return;
    Sample sample = event;
    if (sample.kind == Kind::Move && !movementEnabled_) return;
    if (sample.kind == Kind::Move) {
        if (lastMoveTick_ && (sample.milliseconds - lastMoveTick_ < 85) &&
            std::abs(sample.x - lastMoveX_) < 14 && std::abs(sample.y - lastMoveY_) < 14) return;
        if (lastMoveTick_ && sample.milliseconds - lastMoveTick_ < 65) return;
        lastMoveTick_ = sample.milliseconds;
        lastMoveX_ = sample.x; lastMoveY_ = sample.y;
    }
    if (sample.milliseconds < startedTick_ + totalPaused_) return;
    sample.milliseconds -= startedTick_ + totalPaused_;
    // Bound memory, playback complexity and recording duration.
    // Keep enough reserve to synthesize balanced releases when capture stops.
    if (samples_.size() >= 29800) {
        // Never write to disk or unhook Windows from inside its input callback.
        paused_ = true;
        pausedTick_ = nowMs();
        QTimer::singleShot(0, this, [this] {
            if (completed_) return;
            hooks_->stop();
            closeRecordedHolds();
            maybeCheckpoint(true);
            updateUi();
            info_->setText(russian_ ? QString::fromUtf8("Достигнут лимит записи. Нажмите «Просмотр». Резервная запись сохранена при успешном автосохранении.")
                                    : QStringLiteral("Recording limit reached. Choose Review. Recovery copy is kept when saving succeeds."));
        });
        return;
    }
    // Streamline almost-collinear move samples while retaining turns, drag
    // transitions, window switches and the last timestamp/position.
    bool replacedMove = false;
    if (sample.kind == Kind::Move && samples_.size() >= 2) {
        const auto& middle = samples_.back();
        const auto& first = samples_[samples_.size() - 2];
        if (middle.kind == Kind::Move && first.kind == Kind::Move &&
            first.window.title == sample.window.title &&
            first.window.process == sample.window.process &&
            middle.window.title == sample.window.title &&
            middle.window.process == sample.window.process &&
            sample.milliseconds >= first.milliseconds &&
            sample.milliseconds - first.milliseconds <= (heldButtons_.empty() ? 500ULL : 180ULL)) {
            const double dx = static_cast<double>(sample.x) - first.x;
            const double dy = static_cast<double>(sample.y) - first.y;
            const double length = dx * dx + dy * dy;
            const double factor = length < 0.01 ? 0.0 : std::clamp(
                ((static_cast<double>(middle.x - first.x) * dx) +
                 (static_cast<double>(middle.y - first.y) * dy)) / length, 0.0, 1.0);
            const double deviation = std::hypot(middle.x - first.x - factor * dx,
                                                middle.y - first.y - factor * dy);
            if (deviation <= (heldButtons_.empty() ? 3.0 : 1.5)) {
                samples_.back() = sample;
                replacedMove = true;
            }
        }
    }
    if (!replacedMove) samples_.push_back(sample);
    // The first event of a different semantic action commits the preceding
    // group at the next safe GUI timer tick, never from inside the hook.
    const int meaning = sample.kind == Kind::Text ? 1 :
        sample.kind == Kind::Move ? 2 :
        sample.kind == Kind::Wheel ? 3 :
        sample.kind == Kind::MouseDown || sample.kind == Kind::MouseUp ? 4 : 5;
    if (previousMeaning_ >= 0 &&
        (meaning != previousMeaning_ || previousWindow_.title != sample.window.title ||
         previousWindow_.process != sample.window.process)) checkpointPending_ = true;
    previousMeaning_ = meaning;
    previousWindow_ = sample.window;
    if (sample.kind == Kind::MouseDown) heldButtons_.insert(sample.button);
    if (sample.kind == Kind::MouseUp) heldButtons_.erase(sample.button);
    if (sample.kind == Kind::KeyDown) heldKeys_.insert(sample.virtualKey);
    if (sample.kind == Kind::KeyUp) heldKeys_.erase(sample.virtualKey);
    // Progress and the window label are updated by the 300 ms timer, not
    // inside the time-sensitive low-level Windows hook callback.
}

void InputRecorderDialog::maybeCheckpoint(bool force) {
    if (!started_ || samples_.empty() || checkpointFile_.isEmpty()) return;
    const std::uint64_t now = nowMs();
    if (!force && now - lastCheckpointTick_ < (checkpointPending_ ? 800ULL : 3000ULL)) return;
    const auto latest = samples_.back().milliseconds;
    QString error;
    if (clicknova::gui::recording_checkpoint::save(
            checkpointFile_, checkpointStarted_, samples_, latest, &error)) {
        lastCheckpointTick_ = now;
        checkpointPending_ = false;
    } else if (!checkpointErrorShown_) {
        checkpointErrorShown_ = true;
        info_->setText((russian_ ? QString::fromUtf8("Не удалось сохранить резервную запись: ")
                                  : QStringLiteral("Recording recovery failed: ")) + error);
    }
}

void InputRecorderDialog::updateUi() {
    if (!started_) info_->setText(russian_ ? QString::fromUtf8("Передвиньте панель и нажмите «Запись».")
                                        : QStringLiteral("Move the panel, then press Record."));
    else info_->setText(paused_ ? (russian_ ? QString::fromUtf8("Пауза") : QStringLiteral("Paused"))
                                : (russian_ ? QString::fromUtf8("● Запись идёт") : QStringLiteral("● Recording")));
    updateProgress();
    start_->setEnabled(!started_);
    keyboard_->setEnabled(!started_);
    text_->setEnabled(!started_ && keyboard_->isChecked());
    movement_->setEnabled(!started_);
    pause_->setEnabled(started_ && samples_.size() < 29800);
    stop_->setEnabled(started_);
    pause_->setText(paused_ ? (russian_ ? QString::fromUtf8("Продолжить") : QStringLiteral("Resume"))
                            : (russian_ ? QString::fromUtf8("Пауза") : QStringLiteral("Pause")));
    clicknova::gui::setButtonIcon(pause_, paused_ ? clicknova::gui::Icon::Play : clicknova::gui::Icon::Pause);
}

void InputRecorderDialog::updateProgress() {
    if (!samples_.empty()) {
        const auto& origin = samples_.back().window;
        if (!origin.title.empty() || !origin.process.empty()) {
            const QString title = QString::fromStdU16String(origin.title);
            const QString process = QString::fromStdU16String(origin.process);
            const QString caption = russian_ ? QString::fromUtf8("Окно: %1 (%2)").arg(title, process)
                                             : QStringLiteral("Window: %1 (%2)").arg(title, process);
            if (window_->text() != caption) window_->setText(caption);
        }
    }
    std::uint64_t elapsed = 0;
    if (started_) {
        const auto end = paused_ ? pausedTick_ : nowMs();
        if (end >= startedTick_ + totalPaused_)
            elapsed = end - startedTick_ - totalPaused_;
    }
    const QString text = russian_ ? QString::fromUtf8("Записано: %1 событий · %2:%3")
                                  : QStringLiteral("Recorded: %1 events · %2:%3");
    count_->setText(text.arg(static_cast<qlonglong>(samples_.size()))
                        .arg(static_cast<qlonglong>(elapsed / 60000))
                        .arg(static_cast<qlonglong>((elapsed / 1000) % 60), 2, 10, QLatin1Char('0')));
}

void InputRecorderDialog::closeRecordedHolds() {
    // A release on the recorder panel is not recorded by the hooks. Close
    // captures at pause so playback never drags/holds across an idle period.
    if (samples_.empty()) {
        heldButtons_.clear(); heldKeys_.clear();
        return;
    }
    const std::uint64_t timestamp = pausedTick_ - startedTick_ - totalPaused_;
    int cursorX = 0;
    int cursorY = 0;
#ifdef _WIN32
    POINT cursor{};
    if (GetCursorPos(&cursor)) { cursorX = cursor.x; cursorY = cursor.y; }
#endif
    for (auto key : heldKeys_) {
        if (samples_.size() >= 30000) break;
        Sample sample;
        sample.kind = Kind::KeyUp;
        sample.milliseconds = timestamp;
        sample.virtualKey = key;
        sample.window = samples_.back().window;
        samples_.push_back(sample);
    }
    for (auto button : heldButtons_) {
        if (samples_.size() >= 30000) break;
        Sample sample;
        sample.kind = Kind::MouseUp;
        sample.milliseconds = timestamp;
        sample.button = button;
        sample.x = cursorX; sample.y = cursorY;
        sample.window = samples_.back().window;
        samples_.push_back(sample);
    }
    heldKeys_.clear();
    heldButtons_.clear();
}

void InputRecorderDialog::bringToFront() {
    if (completed_) return;
    if (isMinimized()) showNormal();
    else show();
    raise();
    activateWindow();
}

void InputRecorderDialog::reject() {
    // QDialog handles Escape by calling reject(), which otherwise hides the
    // recorder without invoking closeEvent(). Escape is often part of a macro.
    // Only the explicitly labelled Cancel button is allowed to discard data.
}

void InputRecorderDialog::hideEvent(QHideEvent* event) {
    QDialog::hideEvent(event);
    if (completed_) return;
    // Recover an unexpectedly hidden tool window (e.g. a window-manager event)
    // rather than leaving the main editor locked with an invisible recorder.
    QTimer::singleShot(0, this, [this] {
        if (!completed_ && !isVisible()) bringToFront();
    });
}

void InputRecorderDialog::closeEvent(QCloseEvent* event) {
    if (!completed_) {
        event->ignore();
        stop(!started_);
    } else {
        event->accept();
    }
}
