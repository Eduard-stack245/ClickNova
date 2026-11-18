// SPDX-License-Identifier: MIT
#include "clicknova/input_actions.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stop_token>
#include <vector>

using namespace clicknova::input;

namespace {
struct Backend final : IBackend {
    std::vector<Event> events;
    int failAt{-1};
    int attempted{0};
    bool dispatch(const Event& e) override {
        if (attempted++ == failAt) { failAt = -1; return false; }
        events.push_back(e);
        return true;
    }
};
struct Waiter final : IWaiter {
    std::vector<std::int64_t> waits;
    bool wait(std::int64_t duration, std::stop_token stop) override {
        waits.push_back(duration);
        return !stop.stop_requested();
    }
};
}

int main() {
    const Plan p{{Wait{11}, KeyTap{65}, Wait{13}, KeyTap{66}}, 3, 500};
    assert(validatePlaybackCursor(p, {}).empty());
    assert(validatePlaybackCursor(p, {2, 2}).empty());
    assert(!validatePlaybackCursor(p, {4, 2}).empty());
    assert(!validatePlaybackCursor(p, {2, 0}).empty());
    assert(!validatePlaybackCursor(p, {2, 4}).empty());
    assert(!validatePlaybackCursor(p, {0, -1}).empty());
    Backend backend;
    Waiter waiter;
    std::vector<PlaybackProgress> progress;
    const auto result = play(p, backend, waiter, {}, [&](const PlaybackProgress& e) {
        progress.push_back(e);
    }, {2, 2});
    assert(result.status == Status::Completed);
    assert(result.actionsCompleted == 6); // only the remainder of pass 2 + full pass 3
    assert((waiter.waits == std::vector<std::int64_t>{13, 500, 11, 13}));
    assert(backend.events.size() == 6);
    assert(backend.events[0].virtualKey == 66);
    assert(backend.events[2].virtualKey == 65);
    assert(backend.events[4].virtualKey == 66);
    assert(!progress.empty());
    assert(progress[0].kind == PlaybackProgress::Kind::ActionStarted);
    assert(progress[0].stepIndex == 2 && progress[0].passNumber == 2);
    bool enteredPass3 = false;
    for (const auto& event : progress) {
        if (event.kind == PlaybackProgress::Kind::WaitingForPass &&
            event.passNumber == 3) enteredPass3 = true;
        assert(event.passNumber == 2 || event.passNumber == 3);
    }
    assert(enteredPass3);
    // A real failure returns the original absolute step/pass. A subsequent
    // user-confirmed retry may reattempt that one step, not the earlier A.
    const Plan interrupted{{KeyTap{65}, KeyTap{66}, KeyTap{67}}, 2, 100};
    Backend first;
    first.failAt = 2; // B press fails after the successful A step
    Waiter firstWaiter;
    const auto failure = play(interrupted, first, firstWaiter);
    assert(failure.status == Status::DispatchFailed);
    assert(failure.failedStepIndex == 1 && failure.failedPassNumber == 1);
    assert(failure.actionsCompleted == 1);
    assert(validatePlaybackCursor(interrupted,
        {*failure.failedStepIndex, failure.failedPassNumber}).empty());
    Backend second;
    Waiter secondWaiter;
    const auto continued = play(interrupted, second, secondWaiter, {}, {},
        {*failure.failedStepIndex, failure.failedPassNumber});
    assert(continued.status == Status::Completed);
    assert(continued.actionsCompleted == 5);
    assert(second.events.size() == 10);
    assert(second.events[0].virtualKey == 66); // resumed pass begins with B
    assert(second.events[4].virtualKey == 65); // next complete pass begins with A
    assert((secondWaiter.waits == std::vector<std::int64_t>{100}));
    // A complete valid plan may contain a paired held key, but resuming
    // between those two actions would emit a stray KeyUp with no KeyDown.
    const Plan held{{KeyDown{0x11}, Wait{10}, KeyUp{0x11}, KeyTap{65}}, 2, 0};
    assert(validate(held).empty());
    assert(!validatePlaybackCursor(held, {1, 1}).empty());
    assert(!validatePlaybackCursor(held, {2, 2}).empty());
    assert(validatePlaybackCursor(held, {3, 2}).empty());
    Backend guarded;
    Waiter guardedWaiter;
    const auto invalid = play(held, guarded, guardedWaiter, {}, {}, {2, 1});
    assert(invalid.status == Status::InvalidPlan);
    assert(guarded.events.empty() && guardedWaiter.waits.empty());
    const auto valid = play(held, guarded, guardedWaiter, {}, {}, {3, 2});
    assert(valid.status == Status::Completed && guarded.events.size() == 2);
    // Invalid plan and out-of-range cursor must fail before doing any work.
    const auto bad = play(p, guarded, guardedWaiter, {}, {}, {0, 5});
    assert(bad.status == Status::InvalidPlan);
}
