// SPDX-License-Identifier: MIT
#include "clicknova/quick_clicker_policy.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); std::abort(); } } while (false)
using clicknova::quick::ClickerOptions;
using clicknova::quick::ClickCycleCounter;
using clicknova::quick::ActionMode;
int main() {
    ClickerOptions x;
    CHECK(clicknova::quick::validateClickerOptions(x).empty());
    x.button = 5;
    CHECK(!clicknova::quick::validateClickerOptions(x).empty());
    x.button = 4;
    x.intervalMs = 9;
    CHECK(!clicknova::quick::validateClickerOptions(x).empty());
    x.intervalMs = 10;
    x.count = 0;
    CHECK(!clicknova::quick::validateClickerOptions(x).empty());
    x.count = 3;
    CHECK(clicknova::quick::validateClickerOptions(x).empty());
    ClickCycleCounter finite(x);
    CHECK(!finite.completeCycle() && finite.completed() == 1);
    CHECK(!finite.completeCycle() && finite.completed() == 2);
    CHECK(finite.completeCycle() && finite.completed() == 3);
    finite.reset();
    CHECK(finite.completed() == 0);
    x.unlimited = true;
    x.count = 0;
    ClickCycleCounter infinite(x);
    for (int i = 0; i < 1000; ++i) CHECK(!infinite.completeCycle());
    x.action = ActionMode::Drag;
    CHECK(!clicknova::quick::validateClickerOptions(x).empty());
    x.destinationConfigured = true;
    CHECK(clicknova::quick::validateClickerOptions(x).empty());
    x.action = ActionMode::HoldUntilStop;
    CHECK(!clicknova::quick::validateClickerOptions(x).empty());
    x.unlimited = false;
    x.count = 1;
    CHECK(clicknova::quick::validateClickerOptions(x).empty());
    x.delayMs = -1;
    CHECK(!clicknova::quick::validateClickerOptions(x).empty());
    x.delayMs = 0;
    x.startAtClock = true;
    x.clockHour = 24;
    CHECK(!clicknova::quick::validateClickerOptions(x).empty());
    return 0;
}
