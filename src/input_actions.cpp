// SPDX-License-Identifier: Apache-2.0
#include "clicknova/input_actions.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <set>
#include <type_traits>

namespace clicknova::input {
namespace {
constexpr std::size_t kMaxActions = 512;
constexpr int kMaxDelayMs = 60000;
constexpr std::size_t kMaxTextUnits = 4096;
constexpr int kMoveMin = -32768;
constexpr int kMoveMax = 32767;
constexpr int kMotionFrameMs = 16;
constexpr int motionSteps(int durationMs) noexcept {
    return std::max(1, (durationMs + kMotionFrameMs - 1) / kMotionFrameMs);
}

bool validButton(Button button) noexcept {
    return button >= Button::Left && button <= Button::X2;
}
bool validText(const std::u16string& text) noexcept {
    if (text.empty() || text.size() > kMaxTextUnits) return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t ch = text[i];
        if (ch < 0x20 && ch != u'\n' && ch != u'\r' && ch != u'\t') return false;
        if (ch >= 0xDC00 && ch <= 0xDFFF) return false;
        if (ch >= 0xD800 && ch <= 0xDBFF) {
            if (++i == text.size() || text[i] < 0xDC00 || text[i] > 0xDFFF) return false;
        }
    }
    return true;
}

bool validKey(int key) noexcept {
    // Reserved/undefined Windows VK values are not accepted by the executor.
    // The Win32 adapter is responsible for platform-specific support.
    return key >= 0x08 && key <= 0xFE && key != 0xFF;
}

struct Held {
    bool keyboard;
    int code;
    bool operator==(const Held& other) const noexcept {
        return keyboard == other.keyboard && code == other.code;
    }
    bool operator<(const Held& other) const noexcept {
        if (keyboard != other.keyboard) return keyboard < other.keyboard;
        return code < other.code;
    }
};
Held keyHeld(int key) { return {true, key}; }
Held buttonHeld(Button b) { return {false, static_cast<int>(b)}; }
Event eventFor(Held h, bool press) {
    Event ev;
    if (h.keyboard) {
        ev.kind = press ? EventKind::KeyPress : EventKind::KeyRelease;
        ev.virtualKey = h.code;
    } else {
        ev.kind = press ? EventKind::ButtonPress : EventKind::ButtonRelease;
        ev.button = static_cast<Button>(h.code);
    }
    return ev;
}

} // namespace

std::int64_t millisecondsUntil(const WaitUntil& target,
                                 std::chrono::system_clock::time_point now) {
    if (target.hour < 0 || target.hour > 23 || target.minute < 0 ||
        target.minute > 59 || target.second < 0 || target.second > 59) return -1;
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &seconds) != 0) return -1;
#else
    if (!localtime_r(&seconds, &local)) return -1;
#endif
    local.tm_hour = target.hour;
    local.tm_min = target.minute;
    local.tm_sec = target.second;
    local.tm_isdst = -1;
    auto deadlineSeconds = std::mktime(&local);
    if (deadlineSeconds == static_cast<std::time_t>(-1)) return -1;
    auto deadline = std::chrono::system_clock::from_time_t(deadlineSeconds);
    if (deadline <= now) {
        // Increment the *calendar day* (not +24h) to handle DST boundaries.
        local.tm_mday += 1;
        local.tm_isdst = -1;
        deadlineSeconds = std::mktime(&local);
        if (deadlineSeconds == static_cast<std::time_t>(-1)) return -1;
        deadline = std::chrono::system_clock::from_time_t(deadlineSeconds);
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
}

std::string validate(const Plan& plan) {
    if (plan.actions.empty()) return "Add at least one input action.";
    if (plan.actions.size() > kMaxActions) return "Too many input actions (maximum 512).";
    if (plan.repetitions < 1)
        return "Repetitions must be positive.";
    if (plan.passDelayMs < 0)
        return "Invalid delay between passes.";

    if (!plan.stepWindows.empty() && plan.stepWindows.size() != plan.actions.size())
        return "Window binding count does not match actions.";
    for (const auto& binding : plan.stepWindows) {
        if (binding.title.size() > 1024 || binding.process.size() > 260 ||
            binding.waitMs < 0 || binding.title.find('\0') != std::string::npos ||
            binding.process.find('\0') != std::string::npos ||
            (binding.title.empty() && (!binding.process.empty() || binding.activate || binding.waitMs != 0)) ||
            (!binding.enforce && (binding.activate || binding.waitMs != 0)))
            return "Invalid per-action window binding.";
    }
    std::set<Held> held;
    StepWindow heldWindow;
    for (std::size_t index = 0; index < plan.actions.size(); ++index) {
        const auto& action = plan.actions[index];
        const StepWindow binding = plan.stepWindows.empty() ? StepWindow{} : plan.stepWindows[index];
        // Observed window labels can change during Alt+Tab or dragging.
        // Only a change to a REQUIRED playback target is unsafe while held.
        const bool required = binding.enforce && !binding.title.empty();
        const bool heldRequired = heldWindow.enforce && !heldWindow.title.empty();
        if (!held.empty() && (required != heldRequired ||
            (required && (binding.title != heldWindow.title ||
                          binding.process != heldWindow.process))))
            return "Cannot switch target windows while a key or mouse button is held.";
        if (held.empty()) heldWindow = binding;

        const auto error = std::visit([&](const auto& a) -> std::string {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, Click>) {
                if (!validButton(a.button)) return "Invalid click button.";
                if (a.count < 1) return "Invalid click count.";
                if (a.intervalMs < 0 || a.intervalMs > kMaxDelayMs) return "Invalid click interval.";
                if (held.contains(buttonHeld(a.button))) return "Cannot click a held button.";
            } else if constexpr (std::is_same_v<T, ButtonDown>) {
                if (!validButton(a.button)) return "Invalid button.";
                if (!held.insert(buttonHeld(a.button)).second) return "Duplicate button down.";
            } else if constexpr (std::is_same_v<T, ButtonUp>) {
                if (!validButton(a.button)) return "Invalid button.";
                if (held.erase(buttonHeld(a.button)) != 1) return "Button up without down.";
            } else if constexpr (std::is_same_v<T, MouseHold>) {
                if (!validButton(a.button) || a.durationMs < 1 || a.durationMs > kMaxDelayMs)
                    return "Invalid mouse hold duration or button.";
                if (held.contains(buttonHeld(a.button))) return "Cannot hold an already held button.";
            } else if constexpr (std::is_same_v<T, KeyHold>) {
                if (!validKey(a.virtualKey) || a.durationMs < 1 || a.durationMs > kMaxDelayMs)
                    return "Invalid key hold duration or code.";
                if (held.contains(keyHeld(a.virtualKey))) return "Cannot hold an already held key.";
            } else if constexpr (std::is_same_v<T, RunScript>) {
                if (a.code.empty() || a.code.size() > 65536 ||
                    a.code.find(u'\0') != std::u16string::npos)
                    return "Invalid local script (1..65536 UTF-16 units).";
                if (!held.empty()) return "Release held inputs before running a script.";
            } else if constexpr (std::is_same_v<T, TypeText>) {
                if (!validText(a.text)) return "Invalid text (maximum 4096 UTF-16 units).";
            } else if constexpr (std::is_same_v<T, Wheel>) {
                if (a.axis != Axis::Vertical && a.axis != Axis::Horizontal)
                    return "Invalid wheel axis.";
                if (a.delta == 0 || a.delta < -1200 || a.delta > 1200)
                    return "Wheel delta must be between -1200 and 1200 and nonzero.";
            } else if constexpr (std::is_same_v<T, KeyTap>) {
                if (!validKey(a.virtualKey)) return "Invalid key code.";
                if (held.contains(keyHeld(a.virtualKey))) return "Cannot tap a held key.";
            } else if constexpr (std::is_same_v<T, KeyDown>) {
                if (!validKey(a.virtualKey)) return "Invalid key code.";
                if (!held.insert(keyHeld(a.virtualKey)).second) return "Duplicate key down.";
            } else if constexpr (std::is_same_v<T, KeyUp>) {
                if (!validKey(a.virtualKey)) return "Invalid key code.";
                if (held.erase(keyHeld(a.virtualKey)) != 1) return "Key up without down.";
            } else if constexpr (std::is_same_v<T, MovePointer>) {
                if (a.mode != MoveMode::Relative && a.mode != MoveMode::Absolute && a.mode != MoveMode::WindowClient)
                    return "Invalid pointer movement mode.";
                if (a.x < kMoveMin || a.x > kMoveMax || a.y < kMoveMin || a.y > kMoveMax)
                    return "Pointer movement coordinates are out of range.";
                if (a.durationMs < 0 || a.durationMs > kMaxDelayMs)
                    return "Invalid pointer movement duration.";
            } else if constexpr (std::is_same_v<T, Wait>) {
                if (a.durationMs < 0) return "Invalid wait duration.";
            } else if constexpr (std::is_same_v<T, WaitUntil>) {
                if (a.hour < 0 || a.hour > 23 || a.minute < 0 || a.minute > 59 ||
                    a.second < 0 || a.second > 59) return "Invalid clock time (HH:mm:ss).";
            }
            return {};
        }, action);
        if (!error.empty()) return error;
    }
    if (!held.empty()) return "Every key/button down must have a matching up in the same pass.";
    return {};
}

bool SteadyWaiter::wait(std::int64_t milliseconds, std::stop_token stop) {
    if (stop.stop_requested()) return false;
    if (milliseconds <= 0) return true;
    std::mutex mutex;
    std::condition_variable_any cv;
    std::unique_lock lock(mutex);
    // A bounded chunk avoids overflow of internal steady-clock deadlines for
    // extremely long waits. The stop token interrupts even multi-day waits.
    constexpr std::int64_t kChunkMs = 60'000;
    auto remaining = milliseconds;
    while (remaining > 0 && !stop.stop_requested()) {
        const auto chunk = std::min(remaining, kChunkMs);
        const auto before = std::chrono::steady_clock::now();
        cv.wait_for(lock, stop, std::chrono::milliseconds(chunk), [] { return false; });
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - before).count();
        remaining -= std::min(remaining, std::max<std::int64_t>(1, elapsed));
    }
    return !stop.stop_requested();
}

Result play(const Plan& plan, IBackend& backend, IWaiter& waiter,
            std::stop_token stop, ProgressCallback onProgress) {
    Result result;
    result.message = validate(plan);
    if (!result.message.empty()) return result;
    if (plan.requireTargetWindow && !backend.boundToWindow()) {
        result.message = "The saved scenario requires selecting its window again.";
        return result;
    }

    std::vector<Held> held;
    bool waitCancelled = false;
    const auto send = [&](const Event& event, bool press, bool release, Held which) {
        if (!backend.dispatch(event)) return false;
        ++result.eventsSent;
        if (press) held.push_back(which);
        if (release) {
            auto it = std::find(held.begin(), held.end(), which);
            if (it != held.end()) held.erase(it);
        }
        return true;
    };
    const auto sendHeld = [&](Held which, bool press) {
        return send(eventFor(which, press), press, !press, which);
    };
    const auto wait = [&](std::int64_t milliseconds) {
        if (stop.stop_requested() || !waiter.wait(milliseconds, stop)) {
            waitCancelled = true;
            return false;
        }
        return true;
    };
    const auto finalize = [&]() {
        // In case of cancellation/failure, never silently leave a held key down.
        for (auto it = held.rbegin(); it != held.rend(); ++it) {
            if (!backend.releaseOnCleanup(eventFor(*it, false))) result.releaseFailed = true;
            else ++result.eventsSent;
        }
        held.clear();
        return result;
    };
    result.status = Status::Completed;
    for (int rep = 0; rep < plan.repetitions; ++rep) {
        if (rep > 0) {
            if (onProgress) onProgress(PlaybackProgress{
                PlaybackProgress::Kind::WaitingForPass, 0, rep + 1,
                plan.passDelayMs, std::chrono::steady_clock::now()});
            if (!wait(plan.passDelayMs)) {
                result.status = Status::Cancelled;
                result.message = "Playback cancelled.";
                return finalize();
            }
        }
        for (std::size_t actionIndex = 0; actionIndex < plan.actions.size(); ++actionIndex) {
            if (stop.stop_requested()) {
                result.status = Status::Cancelled;
                return finalize();
            }
            const auto& action = plan.actions[actionIndex];
            const StepWindow binding = plan.stepWindows.empty() ? StepWindow{} : plan.stepWindows[actionIndex];
            if (!backend.prepareStepWindow(binding, stop)) {
                result.status = stop.stop_requested() ? Status::Cancelled : Status::DispatchFailed;
                result.message = stop.stop_requested() ? "Playback cancelled." :
                    "Required step window not found, not foreground, or activation denied.";
                return finalize();
            }
            if (onProgress) onProgress(PlaybackProgress{
                PlaybackProgress::Kind::ActionStarted, actionIndex, rep + 1,
                0, std::chrono::steady_clock::now()});
            bool okay = std::visit([&](const auto& a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, Click>) {
                    const auto heldButton = buttonHeld(a.button);
                    for (std::int64_t i = 0; i < a.count; ++i) {
                        if (i > 0 && !wait(a.intervalMs)) return false;
                        if (stop.stop_requested() || !sendHeld(heldButton, true) ||
                            !sendHeld(heldButton, false)) return false;
                    }
                    return true;
                } else if constexpr (std::is_same_v<T, ButtonDown>) {
                    return sendHeld(buttonHeld(a.button), true);
                } else if constexpr (std::is_same_v<T, ButtonUp>) {
                    return sendHeld(buttonHeld(a.button), false);
                } else if constexpr (std::is_same_v<T, MouseHold>) {
                    const auto which = buttonHeld(a.button);
                    if (!sendHeld(which, true)) return false;
                    if (!wait(a.durationMs)) return false;
                    return sendHeld(which, false);
                } else if constexpr (std::is_same_v<T, KeyHold>) {
                    const auto which = keyHeld(a.virtualKey);
                    if (!sendHeld(which, true)) return false;
                    if (!wait(a.durationMs)) return false;
                    return sendHeld(which, false);
                } else if constexpr (std::is_same_v<T, TypeText>) {
                    for (char16_t unit : a.text) {
                        if (stop.stop_requested()) { waitCancelled = true; return false; }
                        Event ev;
                        ev.unicodeUnit = unit;
                        ev.kind = EventKind::UnicodePress;
                        if (!send(ev, false, false, {})) return false;
                        ev.kind = EventKind::UnicodeRelease;
                        if (!send(ev, false, false, {})) return false;
                    }
                    return true;
                } else if constexpr (std::is_same_v<T, Wheel>) {
                    Event ev;
                    ev.kind = EventKind::WheelTurn;
                    ev.axis = a.axis;
                    ev.delta = a.delta;
                    if (!send(ev, false, false, {})) return false;
                    return true;
                } else if constexpr (std::is_same_v<T, KeyTap>) {
                    const auto key = keyHeld(a.virtualKey);
                    if (!sendHeld(key, true) || !sendHeld(key, false)) return false;
                    return true;
                } else if constexpr (std::is_same_v<T, KeyDown>) {
                    return sendHeld(keyHeld(a.virtualKey), true);
                } else if constexpr (std::is_same_v<T, KeyUp>) {
                    return sendHeld(keyHeld(a.virtualKey), false);
                } else if constexpr (std::is_same_v<T, MovePointer>) {
                    const int steps = motionSteps(a.durationMs);
                    PointerPosition origin{};
                    PointerPosition destination{a.x, a.y};
                    if (a.mode == MoveMode::WindowClient) {
                        const auto resolved = backend.clientToScreen(a.x, a.y);
                        if (!resolved) return false;
                        destination = *resolved;
                    }
                    if (a.mode != MoveMode::Relative && steps > 1) {
                        const auto position = backend.cursorPosition();
                        if (!position) return false;
                        origin = *position;
                    }
                    int previousX = 0;
                    int previousY = 0;
                    for (int i = 1; i <= steps; ++i) {
                        if (stop.stop_requested()) { waitCancelled = true; return false; }
                        if (i > 1 || a.durationMs > 0) {
                            const int elapsed = (a.durationMs * i) / steps;
                            const int before = (a.durationMs * (i - 1)) / steps;
                            if (!wait(elapsed - before)) return false;
                        }
                        Event ev;
                        ev.kind = EventKind::PointerMove;
                        ev.absolute = a.mode != MoveMode::Relative;
                        if (ev.absolute) {
                            // Resolve client coordinates once; guard every emitted move.
                            ev.x = origin.x + static_cast<int>(((static_cast<std::int64_t>(destination.x) - origin.x) * i) / steps);
                            ev.y = origin.y + static_cast<int>(((static_cast<std::int64_t>(destination.y) - origin.y) * i) / steps);
                        } else {
                            const int totalX = static_cast<int>((static_cast<std::int64_t>(a.x) * i) / steps);
                            const int totalY = static_cast<int>((static_cast<std::int64_t>(a.y) * i) / steps);
                            ev.x = totalX - previousX;
                            ev.y = totalY - previousY;
                            previousX = totalX;
                            previousY = totalY;
                        }
                        if (!send(ev, false, false, {})) return false;
                    }
                    return true;
                } else if constexpr (std::is_same_v<T, RunScript>) {
                    return backend.runScript(a.code, stop);
                } else if constexpr (std::is_same_v<T, WaitUntil>) {
                    const auto ms = millisecondsUntil(a, std::chrono::system_clock::now());
                    if (ms < 0) return false;
                    if (onProgress) {
                        const bool moreInPass = actionIndex + 1 < plan.actions.size();
                        const bool morePasses = rep + 1 < plan.repetitions;
                        auto shownDuration = ms;
                        if (!moreInPass && morePasses &&
                            plan.passDelayMs <= std::numeric_limits<std::int64_t>::max() - shownDuration)
                            shownDuration += plan.passDelayMs;
                        onProgress(PlaybackProgress{
                            PlaybackProgress::Kind::WaitingForNextStep,
                            moreInPass ? actionIndex + 1 : 0,
                            moreInPass ? rep + 1 : rep + 2,
                            shownDuration, std::chrono::steady_clock::now()});
                    }
                    return wait(ms);
                } else if constexpr (std::is_same_v<T, Wait>) {
                    if (onProgress) {
                        // The time refers to the next ACTION, not merely to the
                        // end of this pause. At pass boundaries include the
                        // between-passes delay as well.
                        const bool moreInPass = actionIndex + 1 < plan.actions.size();
                        const bool morePasses = rep + 1 < plan.repetitions;
                        auto duration = a.durationMs;
                        if (!moreInPass && morePasses &&
                            plan.passDelayMs <= std::numeric_limits<std::int64_t>::max() - duration)
                            duration += plan.passDelayMs;
                        onProgress(PlaybackProgress{
                            PlaybackProgress::Kind::WaitingForNextStep,
                            moreInPass ? actionIndex + 1 : 0,
                            moreInPass ? rep + 1 : rep + 2,
                            duration, std::chrono::steady_clock::now()});
                    }
                    return wait(a.durationMs);
                }
            }, action);
            if (!okay) {
                result.status = (stop.stop_requested() || waitCancelled) ? Status::Cancelled : Status::DispatchFailed;
                result.message = result.status == Status::Cancelled ? "Playback cancelled." :
                    (std::holds_alternative<RunScript>(action) ?
                         "Local PowerShell step failed or PowerShell was not available." : "Input dispatch failed.");
                return finalize();
            }
            ++result.actionsCompleted;
        }
    }
    return finalize();
}

} // namespace clicknova::input
