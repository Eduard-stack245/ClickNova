// SPDX-License-Identifier: MIT
#include "clicknova/step_timing.hpp"
#include <cassert>
#include <chrono>
using namespace clicknova::input;
using namespace clicknova::input::timing;

int main() {
    const auto initial = std::chrono::system_clock::time_point{std::chrono::seconds{0}};
    Plan plan;
    plan.actions = {Click{Button::Left, 3, 250}, Wait{1500}, MovePointer{MoveMode::Relative,1,2,300},
                    MouseHold{Button::Left, 750}, KeyHold{65, 200}};
    auto rows = estimate(plan,initial);
    assert(rows.size()==5);
    assert(rows[0].durationMs==500 && rows[0].completedAfterMs==500);
    assert(rows[1].durationMs==1500 && rows[1].completedAfterMs==2000);
    assert(rows[2].durationMs==300 && rows[2].completedAfterMs==2300);
    assert(rows[4].completedAfterMs==3250);
    plan.stepWindows.resize(5);
    plan.stepWindows[0].title="Example";
    plan.stepWindows[0].enforce=true;
    plan.stepWindows[0].waitMs=4000;
    rows=estimate(plan,initial);
    assert(rows[0].variable && rows[0].durationMs==4500);
    assert(rows[4].completedAfterMs==7250);
    plan.actions[2]=RunScript{u"Write-Output test"};
    rows=estimate(plan,initial);
    assert(rows[2].unknown && !rows[2].cumulativeKnown);
    assert(!rows[3].cumulativeKnown);
    plan.actions={WaitUntil{0,0,1}};
    plan.stepWindows.clear();
    rows=estimate(plan,initial);
    assert(rows[0].durationMs>=0 && rows[0].durationMs<86400000 && rows[0].cumulativeKnown);
    plan.actions={ElementTask{ElementCommand::WaitVisible}};
    rows=estimate(plan,initial);
    assert(rows[0].variable && rows[0].durationMs==5000);
}
