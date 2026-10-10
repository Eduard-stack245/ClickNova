// SPDX-License-Identifier: Apache-2.0
#include "clicknova/sequence.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace clicknova::sequence;
int main() {
    Plan plan{{{10,20,0,30,Space::Screen}, {5,6,2,100,Space::Client}}, 2, false};
    assert(validate(plan).empty());
    Cursor cursor(plan);
    for (int repeat=0; repeat<2; ++repeat) {
        assert(cursor.peek() && cursor.peek()->x == 10);
        cursor.advance();
        assert(cursor.peek() && cursor.peek()->x == 5);
        assert(cursor.peek()->button == 2);
        cursor.advance();
    }
    assert(!cursor.peek());
    assert(cursor.clicksCompleted() == 4);
    assert(cursor.passesCompleted() == 2);
    cursor.advance();
    assert(cursor.clicksCompleted() == 4);

    plan.unlimited = true;
    Cursor unlimited(plan);
    for (int i=0;i<11;++i) { assert(unlimited.peek()); unlimited.advance(); }
    assert(unlimited.clicksCompleted() == 11);
    assert(unlimited.passesCompleted() == 5);

    plan.steps.clear();
    assert(!validate(plan).empty());
    try { Cursor bad(plan); (void)bad; assert(false); }
    catch (const std::invalid_argument&) {}
    plan.steps = {{0,0,0,10,Space::Screen}};
    plan.steps[0].delayMs = -1;
    assert(!validate(plan).empty());
    plan.steps[0].delayMs = 0;
    assert(validate(plan).empty());
    plan.steps[0].delayMs = 10;
    plan.steps[0].button = 3;
    assert(!validate(plan).empty());
    plan.steps[0].button = 1;
    plan.steps[0].space = Space::Client;
    plan.steps[0].x = -1;
    assert(!validate(plan).empty());
    plan.steps[0].x = 1;
    assert(validate(plan).empty());
    plan.steps[0].clickCount = 0;
    assert(!validate(plan).empty());
    plan.steps[0].clickCount = 3;
    assert(validate(plan).empty());
    Plan repeatClicks{{plan.steps[0], {50, 60, 1, 20, Space::Screen}}, 2, false};
    Cursor repeated(repeatClicks);
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < 3; ++i) {
            assert(repeated.peek() && repeated.peek()->x == 1);
            repeated.advance();
        }
        assert(repeated.peek() && repeated.peek()->x == 50);
        repeated.advance();
    }
    assert(repeated.clicksCompleted() == 8);
    Plan timing{{{10,20,0,75,Space::Screen}, {30,40,1,120,Space::Screen}}, 2, false, 500};
    timing.steps[0].clickCount = 3;
    timing.steps[0].intervalMs = 45;
    Cursor timed(timing);
    assert(timed.nextDelayMs() == 75);
    timed.advance(); assert(timed.nextDelayMs() == 45);
    timed.advance(); assert(timed.nextDelayMs() == 45);
    timed.advance(); assert(timed.nextDelayMs() == 120);
    timed.advance(); assert(timed.nextDelayMs() == 575);
    timed.advance(); assert(timed.nextDelayMs() == 45);
    timed.advance(); assert(timed.nextDelayMs() == 45);
    timed.advance(); assert(timed.nextDelayMs() == 120);
    timed.advance(); assert(!timed.peek());
    timing.passDelayMs = -1; assert(!validate(timing).empty());
    timing.passDelayMs = 0; timing.steps[0].intervalMs = 0; assert(!validate(timing).empty());
    assert(repeated.passesCompleted() == 2);
    assert(!repeated.peek());
    plan.steps[0].configured = false;
    assert(!validate(plan).empty());
    plan.steps[0].configured = true;
    plan.steps.resize(129);
    assert(!validate(plan).empty());
    std::cout << "Sequence execution, bounds and repeats: passed\n";
}
