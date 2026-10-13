// SPDX-License-Identifier: Apache-2.0
#include "clicknova/recording_review.hpp"
#include <cassert>
#include <variant>
using namespace clicknova;
int main() {
    using namespace input;
    using namespace recording::review;
    std::vector<Action> actions{ButtonDown{Button::Left}, MovePointer{MoveMode::Relative, 40, 0, 250}, ButtonUp{Button::Left}, Wait{150}, KeyTap{65}};
    const auto summary = summarize(actions);
    assert(summary.actions == 5 && summary.moves == 1 && summary.mousePresses == 1);
    assert(summary.keyPresses == 1 && summary.scheduledMilliseconds == 400);
    assert(!moveValidated(actions, 2, 0));
    assert(actions.size() == 5 && std::holds_alternative<ButtonDown>(actions[0]));
    assert(!replaceValidated(actions, 2, ButtonUp{Button::Right}));
    std::vector<std::size_t> removed;
    assert(eraseWithPair(actions, 0, &removed));
    assert((removed == std::vector<std::size_t>{0, 2}));
    assert(actions.size() == 3 && std::holds_alternative<MovePointer>(actions.front()));
    assert(moveValidated(actions, 2, 0));
    assert(!eraseWithPair(actions, 20));
    std::vector<Action> key{KeyDown{0x41}, Wait{75}, KeyUp{0x41}};
    assert(eraseWithPair(key, 2, &removed) && key.size() == 1);
    assert((removed == std::vector<std::size_t>{0, 2}));
    // Failed attempts must not clobber the caller's removed-index list.
    assert(!eraseWithPair(key, 20, &removed));
    assert((removed == std::vector<std::size_t>{0, 2}));
    assert(input::validate(input::Plan{key, 1, 0}).empty());
}
