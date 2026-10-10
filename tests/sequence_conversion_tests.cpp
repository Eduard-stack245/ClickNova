// SPDX-License-Identifier: Apache-2.0
#include "clicknova/sequence_conversion.hpp"
#include <cassert>
#include <cstdint>

int main() {
    using namespace clicknova::sequence;
    const auto steps = expandClicksToActions(340, 250, 1, 150, 4, Space::Screen);
    assert(steps && steps->size() == 4);
    for (const auto& step : *steps) {
        assert(step.x == 340 && step.y == 250);
        assert(step.button == 1 && step.clickCount == 1);
        assert(step.delayMs == 150 && step.intervalMs == 150);
        assert(step.configured && !step.ownWindow);
    }
    Cursor cursor(Plan{*steps, 1, false, 0});
    int total = 0;
    while (cursor.peek()) {
        assert(cursor.nextDelayMs() == 150);
        ++total;
        cursor.advance();
    }
    assert(total == 4);
    assert(!expandClicksToActions(0, 0, 0, 150, 129, Space::Screen));
    assert(!expandClicksToActions(0, 0, 0, 150, 0, Space::Screen));
    assert(!expandClicksToActions(0, 0, 4, 150, 5, Space::Screen));
    assert(!expandClicksToActions(-2, 20, 0, 150, 5, Space::Client));
    assert(!expandClicksToActions(0, 0, 0, 0, 5, Space::Screen));
}
