// SPDX-License-Identifier: Apache-2.0
#include "clicknova/start_delay.hpp"
#include <cstdint>
#include <iostream>
#include <limits>

int main() {
    using namespace clicknova::timing;
    // Use runtime checks rather than assert(): CMake's Release builds define NDEBUG.
    const bool ok =
        startDelayMs(0, 0) == 0 &&
        startDelayMs(3, 0) == 3000 &&
        startDelayMs(30, 1) == 1800000 &&
        startDelayMs(24, 2) == 86400000 &&
        startDelayMs(7, 3) == 604800000 &&
        startDelayMs(2147483647ULL, 3) == 185542587100800000LL &&
        !startDelayMs(1, -1) &&
        !startDelayMs(1, 4) &&
        !startDelayMs(std::numeric_limits<std::uint64_t>::max(), 3) &&
        remainingDelayMs(3000, 0) == 3000 &&
        remainingWholeSeconds(3000, 1) == 3 &&
        remainingWholeSeconds(3000, 2000) == 1 &&
        remainingWholeSeconds(3000, 2999) == 1 &&
        remainingDelayMs(3000, 3000) == 0 &&
        remainingDelayMs(3000, 4000) == 0 &&
        remainingDelayMs(0, 0) == 0 &&
        remainingDelayMs(3000, -500) == 3000;
    std::cout << (ok ? "Startup countdown arithmetic passed\n"
                     : "Startup countdown arithmetic FAILED\n");
    return ok ? 0 : 1;
}
