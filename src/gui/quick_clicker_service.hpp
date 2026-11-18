// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/quick_clicker_policy.hpp"
#include "clicknova/quick_motion.hpp"
#include "clicknova/window_target.hpp"
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>

namespace clicknova::gui {

// Port, owned by the service. Implementations MUST fail closed if a requested
// window is stale, not foreground, obscured or otherwise unsafe. No Widgets.
class IQuickClickerDevice {
public:
    virtual ~IQuickClickerDevice() = default;
    virtual bool validateTarget(const platform::WindowTarget& target,
                                bool allowNotForeground, QString& error) = 0;
    virtual bool resolveClientPoint(const platform::WindowTarget& target,
                                    quick::ClickerPoint client,
                                    quick::ClickerPoint& screen, QString& error) = 0;
    virtual bool checkScreenPoint(const platform::WindowTarget& target,
                                  quick::ClickerPoint screen, QString& error) = 0;
    virtual bool cursorPosition(quick::ClickerPoint& screen, QString& error) = 0;
    virtual bool movePointer(quick::ClickerPoint screen, QString& error) = 0;
    virtual bool press(int button, QString& error) = 0;
    virtual bool release(int button, bool cleanup, QString& error) = 0;
    virtual void keepAwake(bool enabled) = 0;
};

// Qt Core-only automation orchestrator. Can be bound from QML as a QObject or
// provided as a property of ClickNovaQmlBridge. Does not click a QWidget.
class QuickClickerService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool needsCleanup READ needsCleanup NOTIFY stateChanged)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(qulonglong completed READ completed NOTIFY completedChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(qint64 remainingDelayMs READ remainingDelayMs NOTIFY remainingDelayChanged)
public:
    enum State : int { Idle = 0, Countdown = 1, Running = 2,
                       Stopped = 3, Completed = 4, Failed = 5 };
    Q_ENUM(State)

    explicit QuickClickerService(std::unique_ptr<IQuickClickerDevice> device,
                                 QObject* parent = nullptr);
    ~QuickClickerService() override;
    QuickClickerService(const QuickClickerService&) = delete;
    QuickClickerService& operator=(const QuickClickerService&) = delete;

    bool active() const noexcept;
    bool needsCleanup() const noexcept { return buttonHeld_; }
    int state() const noexcept { return state_; }
    qulonglong completed() const noexcept { return static_cast<qulonglong>(cycleCounter_.completed()); }
    QString status() const { return status_; }
    qint64 remainingDelayMs() const noexcept { return remainingDelayMs_; }
    const quick::ClickerOptions& options() const noexcept { return options_; }

    // Only the trusted native window chooser may bind a live window identity.
    // Never accept HWND/PID from a QML-supplied map or a persisted scenario.
    bool setSelectedWindow(std::optional<platform::WindowTarget> target);
    void setSafetyHooks(std::function<bool()> acquireEmergencyStop,
                        std::function<void()> releaseEmergencyStop,
                        std::function<bool()> noOtherWorkflow = {});

    // Accepts validated, typed config keys. A rejected update is atomic.
    Q_INVOKABLE bool configure(const QVariantMap& values);
    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();

signals:
    void stateChanged();
    void completedChanged();
    void statusChanged();
    void remainingDelayChanged();
    void finished(bool success);

private:
    void setState(State state);
    void setStatus(QString value);
    void setRemainingDelay(qint64 value);
    void fail(const QString& reason);
    void finish(State result, const QString& message);
    void countdownTick();
    void cycle();
    void finishCycle();
    void motionTick();
    void holdSafetyTick();
    bool resolveOrigin(quick::ClickerPoint& out, QString& error);
    bool resolveDestination(quick::ClickerPoint& out, QString& error);
    bool checkBoundPoint(quick::ClickerPoint point, QString& error);
    bool releaseHeld(bool cleanup);

    std::unique_ptr<IQuickClickerDevice> device_;
    quick::ClickerOptions options_;
    std::optional<platform::WindowTarget> selectedWindow_;
    std::function<bool()> acquireStop_;
    std::function<void()> releaseStop_;
    std::function<bool()> workflowAvailable_;
    State state_{Idle};
    QString status_{QStringLiteral("Ready")};
    quick::ClickCycleCounter cycleCounter_{options_};
    bool stopReserved_{false};
    bool buttonHeld_{false};
    int heldButton_{0};
    QTimer countdownTimer_;
    QTimer clickTimer_;
    QTimer releaseTimer_;
    QTimer motionTimer_;
    QTimer holdSafetyTimer_;
    QElapsedTimer countdownClock_;
    QElapsedTimer motionClock_;
    std::int64_t startDelayMs_{0};
    qint64 remainingDelayMs_{0};
    quick::Motion motion_{};
};

std::unique_ptr<IQuickClickerDevice> makeDefaultQuickClickerDevice();
} // namespace clicknova::gui
