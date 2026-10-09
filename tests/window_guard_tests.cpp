// SPDX-License-Identifier: Apache-2.0
#include "clicknova/window_target.hpp"
#include <array>
#include <iostream>

int main() {
    using namespace clicknova::platform;
    struct TestCase { TargetSnapshot state; TargetCheck expected; };
    constexpr std::array tests{
        TestCase{{true, true, false, true, true, true}, TargetCheck::Ready},
        TestCase{{false, true, false, true, true, true}, TargetCheck::Missing},
        TestCase{{true, false, false, true, true, true}, TargetCheck::Hidden},
        TestCase{{true, true, true, true, true, true}, TargetCheck::Minimized},
        TestCase{{true, true, false, false, true, true}, TargetCheck::NotForeground},
        TestCase{{true, true, false, true, false, true}, TargetCheck::OutOfBounds},
        TestCase{{true, true, false, true, true, false}, TargetCheck::Obscured},
    };
    unsigned count = 0;
    for (const auto& test : tests) {
        if (evaluateWindowTarget(test.state) != test.expected) {
            std::cerr << "Window guard policy failed at case " << count << '\n';
            return 1;
        }
        ++count;
    }
    std::cout << "Window guard cases passed: " << count << '\n';
    return 0;
}
