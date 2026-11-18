// SPDX-License-Identifier: MIT
#include "clicknova/input_actions.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <stop_token>
#include <thread>
#include <vector>

using namespace clicknova::input;
namespace {
struct Backend final : IBackend {
    std::vector<Event> sent;
    int attempts{0};
    int failOn{-1};
    bool dispatch(const Event& e) override {
        if (attempts++ == failOn) return false;
        sent.push_back(e);
        return true;
    }
};
struct Waiter final : IWaiter {
    bool wait(std::int64_t, std::stop_token stop) override {
        return !stop.stop_requested();
    }
};
}
int main() {
    Backend b;
    Waiter w;
    std::vector<PlaybackProgress> events;
    Plan plan{{Wait{25}, KeyTap{65}, KeyTap{66}}, 2, 0};
    auto result=play(plan,b,w,{},[&](const PlaybackProgress& p){events.push_back(p);});
    assert(result.status==Status::Completed);
    int starts=0,completed=0,failed=0;
    for(const auto& p:events) {
        if(p.kind==PlaybackProgress::Kind::ActionStarted) ++starts;
        if(p.kind==PlaybackProgress::Kind::ActionCompleted) {
            ++completed;
            assert(p.stepIndex<3 && (p.passNumber==1 || p.passNumber==2));
            assert(p.elapsedMs>=0);
        }
        if(p.kind==PlaybackProgress::Kind::ActionFailed) ++failed;
    }
    assert(starts==6 && completed==6 && failed==0);
    assert(events.front().kind==PlaybackProgress::Kind::ActionStarted);
    // Step failure must still have an actual elapsed event and the original index.
    Backend f;
    f.failOn=0; // first injected key occurs at source step index 1
    events.clear();
    result=play(plan,f,w,{},[&](const PlaybackProgress& p){events.push_back(p);});
    assert(result.status==Status::DispatchFailed);
    assert(result.failedStepIndex==1 && result.failedPassNumber==1);
    assert(result.actionsCompleted==1);
    assert(events.back().kind==PlaybackProgress::Kind::ActionFailed);
    assert(events.back().stepIndex==1 && events.back().passNumber==1);
    assert(events.back().elapsedMs>=0);
    // Aborting a prepared action also produces a failure outcome.
    Backend c;
    std::stop_source stop;
    stop.request_stop();
    events.clear();
    result=play(plan,c,w,stop.get_token(),[&](const PlaybackProgress& p){events.push_back(p);});
    assert(result.status==Status::Cancelled);
    assert(events.empty());
}
