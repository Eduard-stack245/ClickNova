// SPDX-License-Identifier: Apache-2.0
#include "clicknova/quick_motion.hpp"
#include <cassert>
#include <iostream>

int main() {
    using clicknova::quick::Motion;
    using clicknova::quick::Point;
    using clicknova::quick::sample;
    Motion motion{{100, 100}, {500, 300}, 1000};
    auto beginning = sample(motion, 0);
    auto halfway = sample(motion, 500);
    auto end = sample(motion, 1000);
    assert(beginning && beginning->x == 100 && beginning->y == 100);
    assert(halfway && halfway->x == 300 && halfway->y == 200);
    assert(end && end->x == 500 && end->y == 300);
    assert(sample(motion, 5000)->x == 500);
    motion.durationMs = 0;
    assert(sample(motion, 0)->x == 500);
    motion.durationMs = -1;
    assert(!sample(motion, 100));
    motion.durationMs = 60001;
    assert(!sample(motion, 100));
    motion.durationMs = 60000;
    assert(!sample(motion, -1));
    Motion cross{{-200, -100}, {-600, 700}, 400};
    assert(sample(cross, 200)->x == -400);
    assert(sample(cross, 200)->y == 300);
    std::cout << "Quick pointer interpolation tests passed\n";
}
