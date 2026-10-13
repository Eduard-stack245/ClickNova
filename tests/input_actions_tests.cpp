// SPDX-License-Identifier: Apache-2.0
#include "clicknova/input_actions.hpp"

#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stop_token>
#include <chrono>
#include <ctime>
#include <thread>
#include <string>
#include <vector>

using namespace clicknova::input;

namespace {
struct SpyBackend final : IBackend {
    std::vector<Event> events;
    int failOnCall{-1};
    bool denyCursorPosition{false};
    bool allowClientPosition{false};
    bool hasWindowBinding{false};
    bool allowScript{true};
    std::u16string lastScript;
    bool runScript(const std::u16string& code, std::stop_token stop) override {
        lastScript = code;
        return allowScript && !stop.stop_requested();
    }
    bool boundToWindow() const override { return hasWindowBinding; }
    std::optional<PointerPosition> clientToScreen(int x, int y) override {
        if (!allowClientPosition) return std::nullopt;
        return PointerPosition{x + 100, y + 200};
    }
    std::optional<PointerPosition> cursorPosition() override {
        if (denyCursorPosition) return std::nullopt;
        return PointerPosition{50, 70};
    }
    bool dispatch(const Event& event) override {
        const auto attempt = static_cast<int>(events.size());
        if (attempt == failOnCall) { failOnCall = -1; return false; }
        events.push_back(event);
        return true;
    }
};
struct SpyWaiter final : IWaiter {
    std::vector<std::int64_t> durations;
    bool cancelOnNextWait{false};
    std::stop_source* requestStop{nullptr};
    bool wait(std::int64_t duration, std::stop_token) override {
        durations.push_back(duration);
        if (cancelOnNextWait) {
            cancelOnNextWait = false;
            if (requestStop) requestStop->request_stop();
            return false;
        }
        return true;
    }
};

void testValidation() {
    assert(!validate({}).empty());
    assert(validate({{Click{Button::X1, 3, 20}, KeyTap{0x41}, Wheel{Axis::Horizontal, -120}}}).empty());
    assert(!validate({{Click{Button::Left, 0, 5}}}).empty());
    assert(!validate({{Click{Button::Left, 1, -5}}}).empty());
    assert(!validate({{Click{static_cast<Button>(99), 1, 5}}}).empty());
    assert(!validate({{Wheel{Axis::Vertical, 0}}}).empty());
    assert(!validate({{Wheel{static_cast<Axis>(42), 120}}}).empty());
    assert(!validate({{KeyTap{0}}}).empty());
    assert(!validate({{Wait{-1}}}).empty());
    assert(!validate({{KeyUp{0x41}}}).empty());
    assert(!validate({{KeyDown{0x41}}}).empty());
    assert(!validate({{KeyDown{0x41}, KeyDown{0x41}, KeyUp{0x41}}}).empty());
    assert(!validate({{KeyDown{0x41}, KeyTap{0x41}, KeyUp{0x41}}}).empty());
    assert(!validate({{ButtonUp{Button::X2}}}).empty());
    assert(!validate({{ButtonDown{Button::X2}, Click{Button::X2}, ButtonUp{Button::X2}}}).empty());
    assert(validate({{KeyDown{0x11}, KeyTap{0x43}, KeyUp{0x11}}, 2, 200}).empty());
    assert(!validate({{KeyTap{0x41}}, 0, 0}).empty());
    assert(validate({{KeyTap{0x41}}, 1, 60001}).empty());
    assert(validate({{Click{Button::Left, 10000, 0}}, 100, 0}).empty());
    assert(validate({{Click{Button::Left, 5'000'000'000LL, 0}}}).empty());
    assert(validate({{Wait{7LL * 24 * 60 * 60 * 1000}}}).empty());
    assert(validate({{Wait{0}}}).empty());
}

void testWaitUntilClockTime() {
    using namespace std::chrono;
    assert(validate({{WaitUntil{18, 30, 0}, KeyTap{0x41}}}).empty());
    assert(!validate({{WaitUntil{24, 0, 0}}}).empty());
    assert(!validate({{WaitUntil{12, 60, 0}}}).empty());
    assert(!validate({{WaitUntil{1, 2, -1}}}).empty());
    // Convert a stable local calendar time, not the current time on the test machine.
    std::tm local{};
    local.tm_year = 126; local.tm_mon = 9; local.tm_mday = 13;
    local.tm_hour = 17; local.tm_min = 20; local.tm_sec = 0;
    local.tm_isdst = -1;
    const auto now = system_clock::from_time_t(std::mktime(&local));
    assert(millisecondsUntil(WaitUntil{17, 21, 0}, now) == 60'000);
    // Past today's target => next calendar occurrence, never an immediate click.
    const auto tomorrow = millisecondsUntil(WaitUntil{17, 19, 0}, now);
    assert(tomorrow > 23LL * 3'600'000 && tomorrow <= 25LL * 3'600'000);
    assert(millisecondsUntil(WaitUntil{17, 20, 0}, now) > 23LL * 3'600'000);
    assert(millisecondsUntil(WaitUntil{24, 0, 0}, now) == -1);
    // Cancellation while waiting must suppress the next key action.
    SpyBackend backend;
    SpyWaiter waiter;
    waiter.cancelOnNextWait = true;
    const auto result = play({{WaitUntil{0, 0, 0}, KeyTap{0x41}}}, backend, waiter);
    assert(result.status == Status::Cancelled);
    assert(backend.events.empty());
    assert(waiter.durations.size() == 1 && waiter.durations[0] > 0);
}

void testPowerShellScriptAction() {
    assert(validate({{RunScript{u"Write-Output 'Ready'"}}}).empty());
    assert(!validate({{RunScript{u""}}}).empty());
    assert(!validate({{ButtonDown{Button::Left}, RunScript{u"Write-Host hi"},
                      ButtonUp{Button::Left}}}).empty());
    SpyBackend backend;
    SpyWaiter waiter;
    const Plan plan{{RunScript{u"Write-Output 'Ready'"}, Wait{0}}};
    assert(play(plan, backend, waiter).status == Status::Completed);
    assert(backend.lastScript == u"Write-Output 'Ready'");
    backend.allowScript = false;
    assert(play(plan, backend, waiter).status == Status::DispatchFailed);
    std::stop_source stop;
    stop.request_stop();
    assert(play(plan, backend, waiter, stop.get_token()).status == Status::Cancelled);
}

void testLongWaitStopToken() {
    SteadyWaiter waiter;
    bool completed = true;
    std::jthread worker([&](std::stop_token token) {
        completed = waiter.wait(7LL * 24 * 60 * 60 * 1000, token);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    worker.request_stop();
    worker.join();
    assert(!completed);
}

void testExtremelyLongWaitAndLargeClicksAreInterruptible() {
    SpyBackend backend;
    SpyWaiter waiter;
    const Plan pause{{Wait{7LL * 24 * 60 * 60 * 1000}}};
    assert(play(pause, backend, waiter).status == Status::Completed);
    assert(waiter.durations.size() == 1);
    assert(waiter.durations.front() == 604800000LL);

    std::stop_source stop;
    SpyBackend clicking;
    SpyWaiter cancelling;
    // The first interval is requested after the first completed click.
    cancelling.cancelOnNextWait = true;
    cancelling.requestStop = &stop;
    auto result = play({{Click{Button::Left, 9'000'000'000LL, 100}}},
                       clicking, cancelling, stop.get_token());
    assert(result.status == Status::Cancelled);
    assert(clicking.events.size() == 2);
}

void testUpcomingStepProgress() {
    SpyBackend backend;
    SpyWaiter waiter;
    std::vector<PlaybackProgress> notifications;
    const auto started = std::chrono::steady_clock::now();
    const Plan plan{{Wait{604'800'000}, KeyTap{0x41}, Wait{500}}, 2, 200};
    const auto result = play(plan, backend, waiter, {},
        [&](const PlaybackProgress& progress) { notifications.push_back(progress); });
    assert(result.status == Status::Completed);
    assert(notifications.size() == 11);
    using Kind = PlaybackProgress::Kind;
    // Pass 1: wait a week before the next key.
    assert(notifications[0].kind == Kind::ActionStarted && notifications[0].stepIndex == 0 &&
           notifications[0].passNumber == 1);
    assert(notifications[1].kind == Kind::WaitingForNextStep &&
           notifications[1].stepIndex == 1 && notifications[1].passNumber == 1 &&
           notifications[1].waitMs == 604'800'000);
    assert(notifications[1].startedAt >= started);
    assert(notifications[2].kind == Kind::ActionStarted && notifications[2].stepIndex == 1);
    // Last action of the pass: wait + gap between complete passes.
    assert(notifications[4].kind == Kind::WaitingForNextStep &&
           notifications[4].stepIndex == 0 && notifications[4].passNumber == 2 &&
           notifications[4].waitMs == 700);
    assert(notifications[5].kind == Kind::WaitingForPass &&
           notifications[5].stepIndex == 0 && notifications[5].passNumber == 2 &&
           notifications[5].waitMs == 200);
    assert(notifications[6].kind == Kind::ActionStarted && notifications[6].passNumber == 2);
    assert(notifications[10].kind == Kind::WaitingForNextStep &&
           notifications[10].passNumber == 3 && notifications[10].waitMs == 500);
    // No extra waits appear in the actual plan.
    assert((waiter.durations == std::vector<std::int64_t>{604'800'000, 500, 200, 604'800'000, 500}));

    SpyBackend cancelled;
    SpyWaiter cancelWait;
    cancelWait.cancelOnNextWait = true;
    std::vector<PlaybackProgress> beforeCancel;
    assert(play({{Wait{1000}, KeyTap{0x42}}}, cancelled, cancelWait, {},
                [&](const PlaybackProgress& e) { beforeCancel.push_back(e); }).status == Status::Cancelled);
    assert(beforeCancel.size() == 2);
    assert(beforeCancel[0].kind == Kind::ActionStarted);
    assert(beforeCancel[1].kind == Kind::WaitingForNextStep);
}

void testRepeatedClicksAndWheel() {
    const Plan plan{{Click{Button::X1, 3, 25}, Wheel{Axis::Vertical, 120},
                     Wheel{Axis::Horizontal, -120}, Wait{75}, KeyTap{0x70}}, 2, 400};
    SpyBackend backend;
    SpyWaiter waiter;
    auto result = play(plan, backend, waiter);
    assert(result.status == Status::Completed);
    assert(result.actionsCompleted == 10);
    assert(result.eventsSent == 20); // per pass: 6 mouse + 2 wheel + 2 key
    assert((waiter.durations == std::vector<std::int64_t>{25, 25, 75, 400, 25, 25, 75}));
    assert(backend.events.front().kind == EventKind::ButtonPress);
    assert(backend.events.front().button == Button::X1);
    assert(backend.events[1].kind == EventKind::ButtonRelease);
    assert(backend.events[6].kind == EventKind::WheelTurn);
    assert(backend.events[7].axis == Axis::Horizontal);
    assert(backend.events[7].delta == -120);
    assert(backend.events[8].kind == EventKind::KeyPress);
    assert(backend.events[9].kind == EventKind::KeyRelease);
}

void testChordAndReleaseOnDispatchFailure() {
    const Plan plan{{KeyDown{0x11}, KeyDown{0x10}, KeyTap{0x41},
                     KeyUp{0x10}, KeyUp{0x11}}};
    SpyBackend backend;
    SpyWaiter waiter;
    assert(play(plan, backend, waiter).status == Status::Completed);
    assert(backend.events.size() == 6);
    assert(backend.events.front().virtualKey == 0x11);

    SpyBackend broken;
    broken.failOnCall = 2; // ctrl and shift went down; first 'A' fails
    auto result = play(plan, broken, waiter);
    assert(result.status == Status::DispatchFailed);
    assert(!result.releaseFailed);
    assert(broken.events.size() == 4); // 2 down + 2 cleanup up
    assert(broken.events[2].kind == EventKind::KeyRelease);
    assert(broken.events[3].kind == EventKind::KeyRelease);
    assert(broken.events[2].virtualKey == 0x10); // shift released first
    assert(broken.events[3].virtualKey == 0x11); // ctrl released second
}

void testMixedMouseKeyboardSequence() {
    const Plan plan{{KeyDown{0x11}, Click{Button::X2, 1, 0},
                     Wheel{Axis::Vertical, -120}, KeyUp{0x11}}};
    SpyBackend backend;
    SpyWaiter waiter;
    const auto result = play(plan, backend, waiter);
    assert(result.status == Status::Completed);
    assert(backend.events.size() == 5);
    assert(backend.events[0].kind == EventKind::KeyPress);
    assert(backend.events[1].kind == EventKind::ButtonPress);
    assert(backend.events[1].button == Button::X2);
    assert(backend.events[2].kind == EventKind::ButtonRelease);
    assert(backend.events[3].kind == EventKind::WheelTurn);
    assert(backend.events[4].kind == EventKind::KeyRelease);
}

void testCancellationAlwaysReleasesHeldInputs() {
    const Plan plan{{ButtonDown{Button::X2}, Wait{1000}, ButtonUp{Button::X2}}};
    SpyBackend backend;
    SpyWaiter waiter;
    std::stop_source stop;
    waiter.cancelOnNextWait = true;
    waiter.requestStop = &stop;
    const auto result = play(plan, backend, waiter, stop.get_token());
    assert(result.status == Status::Cancelled);
    assert(result.actionsCompleted == 1);
    assert(backend.events.size() == 2);
    assert(backend.events.back().kind == EventKind::ButtonRelease);
    assert(backend.events.back().button == Button::X2);
}

void testCancellationFromWaiterWithoutStopToken() {
    const Plan plan{{KeyDown{0x11}, Wait{200}, KeyUp{0x11}}};
    SpyBackend backend;
    SpyWaiter waiter;
    waiter.cancelOnNextWait = true;
    const auto result = play(plan, backend, waiter);
    assert(result.status == Status::Cancelled);
    assert(backend.events.size() == 2);
    assert(backend.events.back().kind == EventKind::KeyRelease);
}

void testInvalidPlanNeverDispatches() {
    SpyBackend backend;
    SpyWaiter waiter;
    const auto result = play({{KeyDown{0x70}}}, backend, waiter);
    assert(result.status == Status::InvalidPlan);
    assert(backend.events.empty());
}
void testTimedHoldsAndText() {
    const Plan plan{{MouseHold{Button::Middle, 250}, KeyHold{0x41, 400},
                     TypeText{u"Hello, мир! 😀"}}};
    SpyBackend backend;
    SpyWaiter waiter;
    const auto result = play(plan, backend, waiter);
    assert(result.status == Status::Completed);
    assert(result.actionsCompleted == 3);
    assert((waiter.durations == std::vector<std::int64_t>{250, 400}));
    assert(backend.events.front().kind == EventKind::ButtonPress);
    assert(backend.events.front().button == Button::Middle);
    assert(backend.events[1].kind == EventKind::ButtonRelease);
    assert(backend.events[2].kind == EventKind::KeyPress);
    assert(backend.events[3].kind == EventKind::KeyRelease);
    assert(backend.events[4].kind == EventKind::UnicodePress);
    assert(backend.events[4].unicodeUnit == u'H');
    assert(backend.events[5].kind == EventKind::UnicodeRelease);
    assert(backend.events.back().kind == EventKind::UnicodeRelease);
    assert(result.eventsSent == 4 + 2 * std::u16string(u"Hello, мир! 😀").size());
    assert(validate({{TypeText{u"A\nB\tC"}}}).empty());
    assert(!validate({{TypeText{u""}}}).empty());
    assert(!validate({{TypeText{std::u16string{static_cast<char16_t>(0xD800)}}}}).empty());
    assert(!validate({{TypeText{std::u16string(4097, u'x')}}}).empty());
}

void testTimedHoldCancellationAndFailedRelease() {
    const Plan plan{{MouseHold{Button::X2, 1000}}};
    SpyBackend backend;
    SpyWaiter waiter;
    waiter.cancelOnNextWait = true;
    const auto result = play(plan, backend, waiter);
    assert(result.status == Status::Cancelled);
    assert(result.actionsCompleted == 0);
    assert(backend.events.size() == 2);
    assert(backend.events[0].kind == EventKind::ButtonPress);
    assert(backend.events[1].kind == EventKind::ButtonRelease);
    assert(backend.events[1].button == Button::X2);
    assert(!result.releaseFailed);

    SpyBackend broken;
    broken.failOnCall = 1; // explicit release fails once, cleanup retries
    SpyWaiter normal;
    const auto failed = play(plan, broken, normal);
    assert(failed.status == Status::DispatchFailed);
    assert(broken.events.size() == 2);
    assert(broken.events.back().kind == EventKind::ButtonRelease);
    assert(!failed.releaseFailed);
    assert(!validate({{MouseHold{Button::Left, 0}}}).empty());
    assert(!validate({{KeyHold{0x41, 60001}}}).empty());
    assert(!validate({{ButtonDown{Button::X1}, MouseHold{Button::X1, 200}, ButtonUp{Button::X1}}}).empty());
}


void testForegroundWindowClientMovement() {
    const Plan plan{{MovePointer{MoveMode::WindowClient, 35, 80, 0},
                     Click{Button::Left, 1, 0}}};
    assert(validate(plan).empty());
    SpyWaiter waiter;
    SpyBackend withoutBinding;
    assert(play(plan, withoutBinding, waiter).status == Status::DispatchFailed);
    assert(withoutBinding.events.empty());

    SpyBackend bound;
    bound.allowClientPosition = true;
    const auto result = play(plan, bound, waiter);
    assert(result.status == Status::Completed);
    assert(bound.events.size() == 3);
    assert(bound.events[0].kind == EventKind::PointerMove);
    assert(bound.events[0].absolute);
    assert(bound.events[0].x == 135 && bound.events[0].y == 280);
    // A saved window-bound plan must not silently run in any foreground app.
    Plan needsWindow{{KeyTap{0x41}}, 1, 0, true};
    SpyBackend unbound;
    const auto denied = play(needsWindow, unbound, waiter);
    assert(denied.status == Status::InvalidPlan && unbound.events.empty());
    SpyBackend boundKey;
    boundKey.hasWindowBinding = true;
    assert(play(needsWindow, boundKey, waiter).status == Status::Completed);
}

void testMovementAndUntimedDragging() {
    // An untimed down remains held while the pointer moves, until explicit up.
    const Plan drag{{ButtonDown{Button::Left},
                     MovePointer{MoveMode::Relative, 101, -51, 64},
                     ButtonUp{Button::Left}}};
    assert(validate(drag).empty());
    SpyBackend backend;
    SpyWaiter waiter;
    const Result result = play(drag, backend, waiter);
    assert(result.status == Status::Completed);
    assert(result.eventsSent == 6);
    assert(backend.events.front().kind == EventKind::ButtonPress);
    assert(backend.events.back().kind == EventKind::ButtonRelease);
    assert((waiter.durations == std::vector<std::int64_t>{16,16,16,16}));
    int x = 0, y = 0;
    for (std::size_t i = 1; i < 5; ++i) {
        const auto& ev = backend.events[i];
        assert(ev.kind == EventKind::PointerMove && !ev.absolute);
        x += ev.x;
        y += ev.y;
    }
    assert(x == 101 && y == -51);

    SpyBackend absolute;
    SpyWaiter absoluteWaiter;
    const auto moved = play({{MovePointer{MoveMode::Absolute, 150, 170, 80}}},
                            absolute, absoluteWaiter);
    assert(moved.status == Status::Completed);
    assert(absolute.events.size() == 5);
    assert(absolute.events.front().absolute);
    assert(absolute.events.front().x == 70 && absolute.events.front().y == 90);
    assert(absolute.events.back().x == 150 && absolute.events.back().y == 170);

    SpyBackend immediate;
    SpyWaiter immediateWaiter;
    immediate.denyCursorPosition = true;
    assert(play({{MovePointer{MoveMode::Absolute, -100, 200, 0}}},
                immediate, immediateWaiter).status == Status::Completed);
    assert(immediate.events.size() == 1 && immediate.events.front().x == -100);
    assert(immediateWaiter.durations.empty());
    assert(play({{MovePointer{MoveMode::Absolute, 150, 170, 32}}},
                immediate, immediateWaiter).status == Status::DispatchFailed);
    assert(!validate({{MovePointer{MoveMode::Absolute, 0, 0, -1}}}).empty());
    assert(!validate({{MovePointer{static_cast<MoveMode>(99), 0, 0, 1}}}).empty());
    assert(!validate({{MovePointer{MoveMode::Relative, 32768, 0, 0}}}).empty());
    assert(!validate({{ButtonDown{Button::Right}, MovePointer{MoveMode::Relative, 10, 20, 0}}}).empty());
}

void testCancellationReleasesMouseDuringMotion() {
    SpyBackend backend;
    SpyWaiter waiter;
    waiter.cancelOnNextWait = true;
    const Result result = play({{ButtonDown{Button::X1},
                                 MovePointer{MoveMode::Relative, 400, 0, 160},
                                 ButtonUp{Button::X1}}}, backend, waiter);
    assert(result.status == Status::Cancelled);
    assert(backend.events.size() == 2);
    assert(backend.events.front().kind == EventKind::ButtonPress);
    assert(backend.events.back().kind == EventKind::ButtonRelease);
    assert(backend.events.back().button == Button::X1);
}

} // namespace

int main() {
    testValidation();
    testPowerShellScriptAction();
    testWaitUntilClockTime();
    testLongWaitStopToken();
    testExtremelyLongWaitAndLargeClicksAreInterruptible();
    testUpcomingStepProgress();
    testRepeatedClicksAndWheel();
    testChordAndReleaseOnDispatchFailure();
    testMixedMouseKeyboardSequence();
    testCancellationAlwaysReleasesHeldInputs();
    testCancellationFromWaiterWithoutStopToken();
    testInvalidPlanNeverDispatches();
    testTimedHoldsAndText();
    testTimedHoldCancellationAndFailedRelease();
    testMovementAndUntimedDragging();
    testForegroundWindowClientMovement();
    testCancellationReleasesMouseDuringMotion();
}
