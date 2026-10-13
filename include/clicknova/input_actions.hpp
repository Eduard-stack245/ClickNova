// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <stop_token>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace clicknova::input {

// Input injection uses the foreground OS input stream, NOT a physical device.
// Source-device identification is a separate read-only Raw Input capability.
enum class Button : std::uint8_t { Left, Right, Middle, X1, X2 };
enum class Axis : std::uint8_t { Vertical, Horizontal };
// WindowClient uses coordinates inside a selected foreground window. The
// transient target is never stored with the plan; playback must bind it again.
enum class MoveMode : std::uint8_t { Relative, Absolute, WindowClient };
// Coordinates for Relative are offsets; Absolute targets virtual-desktop pixels.
// durationMs == 0 jumps immediately, otherwise motion is interpolated.
struct MovePointer { MoveMode mode{MoveMode::Relative}; int x{0}; int y{0}; int durationMs{0}; };
struct PointerPosition { int x{0}; int y{0}; };

struct Click { Button button{Button::Left}; std::int64_t count{1}; int intervalMs{100}; };
struct ButtonDown { Button button{Button::Left}; };
struct ButtonUp { Button button{Button::Left}; };
struct MouseHold { Button button{Button::Left}; int durationMs{500}; };
struct Wheel { Axis axis{Axis::Vertical}; int delta{120}; };
// Windows virtual-key codes are transported as numbers and validated portably.
// No characters, typed text or input history are recorded by this module.
struct KeyTap { int virtualKey{0}; };
struct KeyDown { int virtualKey{0}; };
struct KeyUp { int virtualKey{0}; };
struct KeyHold { int virtualKey{0}; int durationMs{500}; };
// UTF-16 for Windows KEYEVENTF_UNICODE; not physical scan codes.
struct TypeText { std::u16string text; };
struct Wait { std::int64_t durationMs{0}; };
// Pause until the next occurrence of a local Windows clock time (24-hour).
// If today's time has passed, the next occurrence is tomorrow.
// Unlike Wait, this stores a clock time, not an elapsed duration.
struct WaitUntil { int hour{12}; int minute{0}; int second{0}; };
// Explicit local PowerShell code step. No script runs on load or recording.
struct RunScript { std::u16string code; };
using Action = std::variant<Click, ButtonDown, ButtonUp, MouseHold, Wheel,
                            KeyTap, KeyDown, KeyUp, KeyHold, TypeText, Wait, MovePointer, RunScript, WaitUntil>;
// Per-step persisted window identity: NEVER a HWND, PID, or a raw native handle.
// Matching uses the selected window title and executable filename on Windows.
// Empty title means this action uses the plan-wide/default foreground target.
// Recorded labels are informational until the user explicitly requires a
// matching window. This avoids treating desktop/hovered window names as
// foreground playback targets while retaining the recorded context.
struct StepWindow {
    std::string title;          // UTF-8 title at selection or recording time
    std::string process;        // UTF-8 executable name, optional
    bool activate{false};      // Explicit opt-in: request foreground focus
    std::int64_t waitMs{0};    // 0 = fail immediately if the window is absent
    bool enforce{true};        // false = recorded metadata only; do not target
    bool operator==(const StepWindow&) const = default;
};
struct Plan {
    std::vector<Action> actions;
    int repetitions{1};
    std::int64_t passDelayMs{0};
    // Persist only the intent to restrict playback, never a native HWND.
    bool requireTargetWindow{false};
    // Empty for legacy scenarios; otherwise exactly one per action.
    std::vector<StepWindow> stepWindows;
};

// Milliseconds to the NEXT local occurrence of a wall-clock time. Returns -1
// for invalid time or a local-clock conversion error. Exposed for deterministic tests.
std::int64_t millisecondsUntil(const WaitUntil& target,
    std::chrono::system_clock::time_point now);

// Strong validation: a complete pass must have balanced holds/releases.
// This also prevents a loop from carrying a held key/button into a new pass.
std::string validate(const Plan& plan);

enum class EventKind : std::uint8_t {
    ButtonPress, ButtonRelease, WheelTurn, KeyPress, KeyRelease,
    UnicodePress, UnicodeRelease, PointerMove
};
struct Event {
    EventKind kind{EventKind::ButtonPress};
    Button button{Button::Left};
    Axis axis{Axis::Vertical};
    int delta{0};
    int virtualKey{0};
    char16_t unicodeUnit{0};
    bool absolute{false};
    int x{0};
    int y{0};
};

class IBackend {
public:
    virtual ~IBackend() = default;
    // False on failed dispatch. Implementations must NOT silently route to
    // a background window or specific device instead of foreground input.
    virtual bool dispatch(const Event& event) = 0;
    // Required only for smooth absolute motion; relative moves need no cursor query.
    virtual std::optional<PointerPosition> cursorPosition() { return std::nullopt; }
    // Resolve a foreground-window client coordinate only when a valid transient
    // target has been selected. The default is deliberately unsupported.
    virtual std::optional<PointerPosition> clientToScreen(int, int) { return std::nullopt; }
    // Cleanup must release held inputs even if the selected window lost focus.
    virtual bool releaseOnCleanup(const Event& event) { return dispatch(event); }
    virtual bool boundToWindow() const { return false; }
    // Called before each action (including when unbound, to reset prior focus).
    // Must fail closed when a requested target is absent/inactive.
    virtual bool prepareStepWindow(const StepWindow& binding, std::stop_token) {
        return binding.title.empty();
    }
    // Optional executor; the default refuses local code for unsupported backends.
    virtual bool runScript(const std::u16string&, std::stop_token) { return false; }
};
class IWaiter {
public:
    virtual ~IWaiter() = default;
    // Return false if stopped/cancelled.
    virtual bool wait(std::int64_t milliseconds, std::stop_token stop) = 0;
};
class SteadyWaiter final : public IWaiter {
public:
    bool wait(std::int64_t milliseconds, std::stop_token stop) override;
};

enum class Status : std::uint8_t { Completed, Cancelled, InvalidPlan, DispatchFailed };
struct Result {
    Status status{Status::InvalidPlan};
    std::string message;
    std::uint64_t eventsSent{0};
    std::uint64_t actionsCompleted{0};
    // Cleanup is best effort if injection fails or playback is cancelled.
    bool releaseFailed{false};
};

// Sent from the playback thread. The UI must marshal notifications to its own
// thread and use startedAt (steady clock) instead of starting a new timer
// when Qt eventually processes the notification.
struct PlaybackProgress {
    enum class Kind { ActionStarted, WaitingForNextStep, WaitingForPass };
    Kind kind{Kind::ActionStarted};
    std::size_t stepIndex{0};   // zero-based index of the upcoming/current action
    int passNumber{1};          // one-based pass number
    std::int64_t waitMs{0};
    std::chrono::steady_clock::time_point startedAt{};
};
using ProgressCallback = std::function<void(const PlaybackProgress&)>;

Result play(const Plan& plan, IBackend& backend, IWaiter& waiter,
            std::stop_token stop = {}, ProgressCallback onProgress = {});

} // namespace clicknova::input
