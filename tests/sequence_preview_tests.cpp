// SPDX-License-Identifier: Apache-2.0
#include "clicknova/sequence_preview.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>

using namespace clicknova::sequence;

int main() {
    Plan plan{{Step{100, 200, 0, 500, Space::Screen},
               Step{250, 300, 1, 100, Space::Screen}}, 5, false, 2000};
    plan.steps[0].clickCount = 3;
    plan.steps[0].intervalMs = 100;
    plan.steps[1].clickCount = 2;
    plan.steps[1].intervalMs = 250;

    const auto p = preview(plan);
    assert(p);
    assert(p->clicksPerPass == 5);
    assert(p->waitMsPerPass == 1050); // 500 + 2*100 + 100 + 250.
    assert(p->totalClicks && *p->totalClicks == 25);
    assert(p->totalWaitMs && *p->totalWaitMs == 13250); // 5*1050 + 4*2000.

    Cursor cursor(plan);
    std::uint64_t observedWait = 0;
    std::uint64_t observedClicks = 0;
    while (cursor.peek()) {
        observedWait += static_cast<std::uint64_t>(cursor.nextDelayMs());
        ++observedClicks;
        cursor.advance();
    }
    assert(observedClicks == *p->totalClicks);
    assert(observedWait == *p->totalWaitMs);

    plan.unlimited = true;
    const auto unlimited = preview(plan);
    assert(unlimited && unlimited->clicksPerPass == 5);
    assert(!unlimited->totalClicks && !unlimited->totalWaitMs);

    plan.unlimited = false;
    plan.repetitions = 1;
    const auto singlePass = preview(plan);
    assert(singlePass && *singlePass->totalWaitMs == 1050);

    plan.steps[0].configured = false;
    assert(!preview(plan));
    plan.steps[0].configured = true;
    plan.steps[0].clickCount = 0;
    assert(!preview(plan));

    // The maximum legal plan must fit in unsigned 64-bit integer arithmetic.
    Plan maximum;
    maximum.steps.assign(128, Step{});
    for (auto& step : maximum.steps) {
        step.clickCount = 100000;
        step.delayMs = 60000;
        step.intervalMs = 60000;
    }
    maximum.repetitions = 1000000;
    maximum.passDelayMs = 60000;
    const auto large = preview(maximum);
    assert(large && large->totalClicks && large->totalWaitMs);
    assert(*large->totalClicks == 128ULL * 100000ULL * 1000000ULL);
    assert(*large->totalWaitMs == large->waitMsPerPass * 1000000ULL + 60000ULL * 999999ULL);
    std::cout << "Sequence preview calculations: passed\n";
}
