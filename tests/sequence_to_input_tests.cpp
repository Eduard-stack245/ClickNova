// SPDX-License-Identifier: Apache-2.0
#include "clicknova/sequence_to_input.hpp"
#include <cassert>
#include <variant>

int main() {
    using namespace clicknova;
    sequence::Step a{};
    a.x = 150; a.y = 220; a.delayMs = 400; a.clickCount = 3;
    a.intervalMs = 75; a.button = 1;
    sequence::Step b{};
    b.x = 250; b.y = 420; b.delayMs = 0; b.button = 0;
    sequence::Plan old{{a, b}, 2, false, 1000};
    std::string message;
    const auto good = sequence::toInputPlan(old, &message);
    assert(good && message.empty() && good->repetitions == 2 && good->passDelayMs == 1000);
    assert(good->actions.size() == 5);
    assert(std::get<input::Wait>(good->actions[0]).durationMs == 400);
    const auto& move = std::get<input::MovePointer>(good->actions[1]);
    assert(move.mode == input::MoveMode::Absolute && move.x == 150 && move.y == 220);
    const auto& click = std::get<input::Click>(good->actions[2]);
    assert(click.button == input::Button::Right && click.count == 3 && click.intervalMs == 75);
    assert(std::get<input::MovePointer>(good->actions[3]).x == 250);
    old.steps[0].space = sequence::Space::Client;
    const auto client = sequence::toInputPlan(old, &message);
    assert(client && client->requireTargetWindow);
    assert(std::get<input::MovePointer>(client->actions[1]).mode == input::MoveMode::WindowClient);
    old.steps[0].space = sequence::Space::Screen;
    assert(sequence::toInputPlan(old, &message, true)->requireTargetWindow);
    old.steps[0].ownWindow = true;
    assert(!sequence::toInputPlan(old));
    old.steps[0].ownWindow = false;
    old.unlimited = true;
    assert(!sequence::toInputPlan(old));
    old.unlimited = false;
    old.steps[0].configured = false;
    assert(!sequence::toInputPlan(old));
}
