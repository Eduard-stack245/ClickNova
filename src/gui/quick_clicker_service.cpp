// SPDX-License-Identifier: MIT
#include "quick_clicker_service.hpp"
#include "clicknova/input_actions.hpp"
#include <QMetaType>
#include <QStringList>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace clicknova::gui {
namespace {
bool readInt64(const QVariant& value, qint64& out) {
    const int kind = value.metaType().id();
    if (kind == QMetaType::Int || kind == QMetaType::UInt || kind == QMetaType::LongLong ||
        kind == QMetaType::ULongLong) {
        bool ok = false;
        out = value.toLongLong(&ok);
        return ok;
    }
    if (kind == QMetaType::Double || kind == QMetaType::Float) {
        bool ok = false;
        const double number = value.toDouble(&ok);
        // Never silently round a QML floating-point setting.
        if (!ok || !std::isfinite(number) || std::floor(number) != number ||
            number < -9007199254740991.0 || number > 9007199254740991.0) return false;
        out = static_cast<qint64>(number);
        return true;
    }
    return false;
}
} // namespace

QuickClickerService::QuickClickerService(std::unique_ptr<IQuickClickerDevice> device,
                                         QObject* parent)
    : QObject(parent), device_(std::move(device)) {
    Q_ASSERT(device_);
    for (QTimer* timer : {&countdownTimer_, &clickTimer_, &releaseTimer_}) {
        timer->setSingleShot(true);
        timer->setTimerType(Qt::PreciseTimer);
    }
    motionTimer_.setInterval(16);
    motionTimer_.setTimerType(Qt::PreciseTimer);
    holdSafetyTimer_.setInterval(100);
    holdSafetyTimer_.setTimerType(Qt::PreciseTimer);
    connect(&countdownTimer_, &QTimer::timeout, this, &QuickClickerService::countdownTick);
    connect(&clickTimer_, &QTimer::timeout, this, &QuickClickerService::cycle);
    connect(&releaseTimer_, &QTimer::timeout, this, [this] {
        if (!active() || !buttonHeld_) return;
        if (!releaseHeld(false)) { fail(QStringLiteral("Could not release mouse button")); return; }
        finishCycle();
    });
    connect(&motionTimer_, &QTimer::timeout, this, &QuickClickerService::motionTick);
    connect(&holdSafetyTimer_, &QTimer::timeout, this, &QuickClickerService::holdSafetyTick);
}
QuickClickerService::~QuickClickerService() {
    // Best-effort final cleanup while the host's hotkey controller still lives.
    if (active() || buttonHeld_) stop();
    if (stopReserved_) {
        stopReserved_ = false;
        if (releaseStop_) releaseStop_();
    }
    device_->keepAwake(false);
}

bool QuickClickerService::active() const noexcept { return state_ == Countdown || state_ == Running; }
void QuickClickerService::setState(State next) {
    if (state_ == next) return;
    state_ = next;
    emit stateChanged();
}
void QuickClickerService::setStatus(QString value) {
    if (status_ == value) return;
    status_ = std::move(value);
    emit statusChanged();
}
void QuickClickerService::setRemainingDelay(qint64 value) {
    if (remainingDelayMs_ == value) return;
    remainingDelayMs_ = value;
    emit remainingDelayChanged();
}
bool QuickClickerService::setSelectedWindow(std::optional<platform::WindowTarget> target) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (active() || buttonHeld_) return false;
    selectedWindow_ = std::move(target);
    return true;
}
void QuickClickerService::setSafetyHooks(std::function<bool()> acquire,
                                         std::function<void()> release,
                                         std::function<bool()> available) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (active()) return;
    acquireStop_ = std::move(acquire);
    releaseStop_ = std::move(release);
    workflowAvailable_ = std::move(available);
}

bool QuickClickerService::configure(const QVariantMap& values) {
    Q_ASSERT(thread() == QThread::currentThread());
    if (active() || buttonHeld_) {
        setStatus(QStringLiteral("Stop and confirm mouse release before editing quick-click settings"));
        return false;
    }
    quick::ClickerOptions candidate = options_;
    QString problem;
    const auto readInteger = [&](const QString& name, qint64& number) {
        auto it = values.constFind(name);
        if (it == values.cend()) return true;
        if (readInt64(it.value(), number)) return true;
        problem = QStringLiteral("Invalid integer: %1").arg(name);
        return false;
    };
    const auto readBool = [&](const QString& name, bool& out) {
        auto it = values.constFind(name);
        if (it == values.cend()) return true;
        if (it.value().metaType().id() != QMetaType::Bool) {
            problem = QStringLiteral("Invalid boolean: %1").arg(name);
            return false;
        }
        out = it.value().toBool();
        return true;
    };
    const auto intSetting = [&](const char* literal, int& out) {
        const QString key = QString::fromLatin1(literal);
        if (!values.contains(key)) return true;
        qint64 number = 0;
        if (!readInteger(key, number)) return false;
        if (number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
            problem = QStringLiteral("Out of range: %1").arg(key);
            return false;
        }
        out = static_cast<int>(number);
        return true;
    };
    int action = static_cast<int>(candidate.action);
    if (!intSetting("action", action) || !intSetting("button", candidate.button) ||
        !intSetting("intervalMs", candidate.intervalMs) || !intSetting("holdMs", candidate.holdMs) ||
        !intSetting("x", candidate.position.x) || !intSetting("y", candidate.position.y) ||
        !intSetting("destinationX", candidate.destination.x) ||
        !intSetting("destinationY", candidate.destination.y) ||
        !intSetting("movementMs", candidate.motionDurationMs) ||
        !intSetting("clockHour", candidate.clockHour) ||
        !intSetting("clockMinute", candidate.clockMinute) ||
        !intSetting("clockSecond", candidate.clockSecond) ||
        !readBool(QStringLiteral("unlimited"), candidate.unlimited) ||
        !readBool(QStringLiteral("fixedPosition"), candidate.fixedPosition) ||
        !readBool(QStringLiteral("targetWindow"), candidate.restrictToWindow) ||
        !readBool(QStringLiteral("destinationConfigured"), candidate.destinationConfigured) ||
        !readBool(QStringLiteral("keepAwake"), candidate.keepAwake) ||
        !readBool(QStringLiteral("useClockStart"), candidate.startAtClock)) {
        setStatus(problem);
        return false;
    }
    candidate.action = static_cast<quick::ActionMode>(action);
    qint64 count = static_cast<qint64>(candidate.count);
    if (!readInteger(QStringLiteral("count"), count) ||
        !readInteger(QStringLiteral("delayMs"), candidate.delayMs)) {
        setStatus(problem);
        return false;
    }
    if (count < 0) { setStatus(QStringLiteral("Count must not be negative")); return false; }
    candidate.count = static_cast<std::uint64_t>(count);
    static const QStringList allowedKeys{
        QStringLiteral("action"), QStringLiteral("button"), QStringLiteral("intervalMs"),
        QStringLiteral("holdMs"), QStringLiteral("count"), QStringLiteral("unlimited"),
        QStringLiteral("fixedPosition"), QStringLiteral("x"), QStringLiteral("y"),
        QStringLiteral("targetWindow"), QStringLiteral("destinationConfigured"),
        QStringLiteral("destinationX"), QStringLiteral("destinationY"),
        QStringLiteral("movementMs"), QStringLiteral("delayMs"),
        QStringLiteral("useClockStart"), QStringLiteral("clockHour"),
        QStringLiteral("clockMinute"), QStringLiteral("clockSecond"),
        QStringLiteral("keepAwake")};
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!allowedKeys.contains(it.key())) {
            setStatus(QStringLiteral("Unknown quick-click setting: %1").arg(it.key()));
            return false;
        }
    }
    const auto validation = quick::validateClickerOptions(candidate);
    if (!validation.empty()) { setStatus(QString::fromUtf8(validation.data(), validation.size())); return false; }
    options_ = candidate;
    cycleCounter_ = quick::ClickCycleCounter(candidate);
    emit completedChanged();
    setStatus(QStringLiteral("Quick-click configuration ready"));
    return true;
}

bool QuickClickerService::start() {
    Q_ASSERT(thread() == QThread::currentThread());
    if (active()) return false;
    if (buttonHeld_) {
        setStatus(QStringLiteral("CRITICAL: previous mouse release is unconfirmed; press Stop to retry"));
        return false;
    }
    const auto validation = quick::validateClickerOptions(options_);
    if (!validation.empty()) {
        setStatus(QString::fromUtf8(validation.data(), validation.size()));
        return false;
    }
    if (!acquireStop_ || !releaseStop_ || !workflowAvailable_ || !workflowAvailable_()) {
        setStatus(QStringLiteral("Emergency Stop / workflow guard is unavailable"));
        return false;
    }
    if (options_.restrictToWindow && !selectedWindow_) {
        setStatus(QStringLiteral("Select a fresh target window first"));
        return false;
    }
    QString error;
    if (options_.restrictToWindow &&
        !device_->validateTarget(*selectedWindow_, true, error)) {
        setStatus(error);
        return false;
    }
    // Resolve the wall-clock time once, then rely solely on a monotonic clock.
    startDelayMs_ = options_.startAtClock
        ? clicknova::input::millisecondsUntil(
            {options_.clockHour, options_.clockMinute, options_.clockSecond},
            std::chrono::system_clock::now())
        : options_.delayMs;
    if (startDelayMs_ < 0) {
        setStatus(QStringLiteral("Cannot calculate the requested start time"));
        return false;
    }
    if (!acquireStop_()) {
        setStatus(QStringLiteral("Cannot reserve the global Emergency Stop hotkey"));
        return false;
    }
    stopReserved_ = true;
    cycleCounter_.reset();
    emit completedChanged();
    device_->keepAwake(options_.keepAwake);
    countdownClock_.start();
    if (startDelayMs_ > 0) {
        setRemainingDelay(startDelayMs_);
        setState(Countdown);
        setStatus(QStringLiteral("Waiting for quick-click start"));
        countdownTick();
    } else {
        setRemainingDelay(0);
        setState(Running);
        setStatus(QStringLiteral("Quick clicker running"));
        clickTimer_.start(0);
    }
    return true;
}
void QuickClickerService::countdownTick() {
    if (state_ != Countdown) return;
    const std::int64_t elapsed = countdownClock_.elapsed();
    const std::int64_t remaining = std::max<std::int64_t>(0, startDelayMs_ - elapsed);
    setRemainingDelay(remaining);
    if (remaining == 0) {
        setState(Running);
        setStatus(QStringLiteral("Quick clicker running"));
        clickTimer_.start(0);
        return;
    }
    countdownTimer_.start(static_cast<int>(std::min<std::int64_t>(remaining, 1000)));
}

bool QuickClickerService::checkBoundPoint(quick::ClickerPoint point, QString& error) {
    if (!options_.restrictToWindow) return true;
    if (!selectedWindow_) { error = QStringLiteral("Target window is no longer selected"); return false; }
    return device_->checkScreenPoint(*selectedWindow_, point, error);
}
bool QuickClickerService::resolveOrigin(quick::ClickerPoint& out, QString& error) {
    if (options_.fixedPosition) {
        if (options_.restrictToWindow) {
            if (!device_->resolveClientPoint(*selectedWindow_, options_.position, out, error)) return false;
        } else out = options_.position;
        if (!checkBoundPoint(out, error) || !device_->movePointer(out, error)) return false;
        return checkBoundPoint(out, error);
    }
    return device_->cursorPosition(out, error) && checkBoundPoint(out, error);
}
bool QuickClickerService::resolveDestination(quick::ClickerPoint& out, QString& error) {
    if (options_.restrictToWindow) {
        return device_->resolveClientPoint(*selectedWindow_, options_.destination, out, error);
    }
    out = options_.destination;
    return true;
}
void QuickClickerService::cycle() {
    if (state_ != Running) return;
    QString error;
    quick::ClickerPoint from;
    if (!resolveOrigin(from, error)) { fail(error); return; }
    const auto mode = options_.action;
    if (mode == quick::ActionMode::Move || mode == quick::ActionMode::Drag) {
        quick::ClickerPoint to;
        if (!resolveDestination(to, error) || !checkBoundPoint(to, error)) { fail(error); return; }
        motion_ = {{from.x, from.y}, {to.x, to.y}, options_.motionDurationMs};
        if (!quick::sample(motion_, 0)) { fail(QStringLiteral("Invalid movement")); return; }
        if (mode == quick::ActionMode::Drag) {
            if (!checkBoundPoint(from, error) || !device_->press(options_.button, error)) { fail(error); return; }
            heldButton_ = options_.button;
            buttonHeld_ = true;
        }
        motionClock_.start();
        motionTick();
        if (state_ == Running && (motion_.durationMs > 0 || buttonHeld_)) {
            // motionTick() stops its timer when the motion already completed.
            // Recheck duration rather than starting an unnecessary timer.
            if (motionClock_.elapsed() < motion_.durationMs) motionTimer_.start();
        }
        return;
    }
    if (!checkBoundPoint(from, error) || !device_->press(options_.button, error)) { fail(error); return; }
    heldButton_ = options_.button;
    buttonHeld_ = true;
    if (mode == quick::ActionMode::HoldUntilStop) {
        if (options_.restrictToWindow) holdSafetyTimer_.start();
        setStatus(QStringLiteral("Mouse button held; use Emergency Stop to release"));
        return;
    }
    if (options_.holdMs > 0) {
        if (options_.restrictToWindow) holdSafetyTimer_.start();
        releaseTimer_.start(options_.holdMs);
        return;
    }
    if (!releaseHeld(false)) { fail(QStringLiteral("Mouse release failed")); return; }
    finishCycle();
}
void QuickClickerService::holdSafetyTick() {
    if (state_ != Running || !buttonHeld_ || !options_.restrictToWindow) {
        holdSafetyTimer_.stop();
        return;
    }
    QString error;
    quick::ClickerPoint point;
    if (!device_->cursorPosition(point, error) || !checkBoundPoint(point, error)) {
        fail(error.isEmpty() ? QStringLiteral("Target safety check failed during hold") : error);
    }
}
void QuickClickerService::motionTick() {
    if (state_ != Running) { motionTimer_.stop(); return; }
    const int elapsed = static_cast<int>(std::min<qint64>(motionClock_.elapsed(), 60000));
    const auto sampled = quick::sample(motion_, elapsed);
    if (!sampled) { fail(QStringLiteral("Invalid motion sample")); return; }
    quick::ClickerPoint screen{sampled->x, sampled->y};
    QString error;
    if (!checkBoundPoint(screen, error) || !device_->movePointer(screen, error) ||
        !checkBoundPoint(screen, error)) { fail(error); return; }
    if (elapsed < motion_.durationMs) return;
    motionTimer_.stop();
    if (buttonHeld_ && !releaseHeld(false)) { fail(QStringLiteral("Mouse release failed")); return; }
    finishCycle();
}
bool QuickClickerService::releaseHeld(bool cleanup) {
    if (!buttonHeld_) return true;
    QString error;
    if (!device_->release(heldButton_, cleanup, error)) {
        if (cleanup) {
            // Keep the held flag set when the OS has not confirmed release.
            // New sessions are prohibited until a later cleanup succeeds.
            setStatus(QStringLiteral("CRITICAL: mouse release failed: %1").arg(error));
        }
        return false;
    }
    buttonHeld_ = false;
    holdSafetyTimer_.stop();
    return true;
}
void QuickClickerService::finishCycle() {
    if (state_ != Running) return;
    const bool reachedLimit = cycleCounter_.completeCycle();
    emit completedChanged();
    if (reachedLimit) {
        finish(Completed, QStringLiteral("Quick clicking completed"));
    } else clickTimer_.start(options_.intervalMs);
}
void QuickClickerService::stop() {
    Q_ASSERT(thread() == QThread::currentThread());
    if (active()) {
        finish(Stopped, QStringLiteral("Quick clicking stopped"));
    } else if (buttonHeld_) {
        if (releaseHeld(true)) {
            // Never unregister the emergency key before the OS confirms release.
            if (stopReserved_) {
                stopReserved_ = false;
                if (releaseStop_) releaseStop_();
            }
            emit stateChanged(); // needsCleanup changed even though state stayed Failed
            setStatus(QStringLiteral("Mouse button release recovered"));
        } else {
            setStatus(QStringLiteral("CRITICAL: cannot release mouse button; retry Stop"));
        }
    }
}
void QuickClickerService::fail(const QString& error) {
    finish(Failed, error.isEmpty() ? QStringLiteral("Quick clicking failed") : error);
}
void QuickClickerService::finish(State result, const QString& message) {
    if (!active() && !buttonHeld_ && !stopReserved_) return;
    countdownTimer_.stop();
    clickTimer_.stop();
    releaseTimer_.stop();
    motionTimer_.stop();
    holdSafetyTimer_.stop();
    setRemainingDelay(0);
    const bool releaseOk = releaseHeld(true);
    device_->keepAwake(false);
    if (stopReserved_ && releaseOk) {
        stopReserved_ = false;
        if (releaseStop_) releaseStop_();
    }
    const bool success = result == Completed && releaseOk;
    setState(releaseOk ? result : Failed);
    setStatus(releaseOk ? message : QStringLiteral("CRITICAL: mouse release could not be confirmed"));
    emit finished(success);
}
} // namespace clicknova::gui
