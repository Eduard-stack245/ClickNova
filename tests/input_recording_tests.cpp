// SPDX-License-Identifier: Apache-2.0
#include "clicknova/input_recording.hpp"
#include <cassert>
#include <limits>
#include <variant>
using namespace clicknova;
using namespace clicknova::recording;
int main() {
    {
        const auto r = convert({
            {Kind::Move, 0, 100, 100},
            {Kind::MouseDown, 50, 100, 100},
            {Kind::Move, 150, 150, 150},
            {Kind::MouseUp, 240, 150, 150},
        });
        assert(r.error.empty());
        assert(input::validate(r.plan).empty());
        bool seenDrag = false;
        for (const auto& a : r.plan.actions)
            if (const auto* m = std::get_if<input::MovePointer>(&a); m && m->durationMs == 100) seenDrag = true;
        assert(seenDrag);
    }
    {
        const auto r = convert({{Kind::KeyDown, 0, 0, 0, {}, {}, 0, 0x41},
                                {Kind::KeyDown, 50, 0, 0, {}, {}, 0, 0x41}});
        assert(r.error.empty() && r.completedHolds);
        assert(input::validate(r.plan).empty());
    }
    {
        const auto r = convert({{Kind::MouseDown, 10, 20, 30},
                                {Kind::MouseUp, 20, 20, 30}});
        assert(r.error.empty());
        assert(input::validate(r.plan).empty());
    }
    {
        const auto r = convert({{Kind::KeyDown, 50, 0, 0, {}, {}, 0, 0x11},
                                {Kind::KeyUp, 10, 0, 0, {}, {}, 0, 0x11}});
        assert(!r.error.empty());
    }
    {
        // Invalid timing cannot be hidden by cursor-path simplification.
        const auto r = convert({{Kind::Move, 100, 1, 1},
                                {Kind::Move, 50, 2, 2},
                                {Kind::Move, 200, 3, 3}});
        assert(!r.error.empty());
    }
    {
        // Thousands of nearly straight cursor samples become a manageable
        // plan while the final position and total recording time remain intact.
        std::vector<Sample> longMotion;
        for (int i = 0; i < 1800; ++i)
            longMotion.push_back({Kind::Move, static_cast<std::uint64_t>(i * 30),
                                  i, 200});
        const auto r = convert(longMotion);
        assert(r.error.empty());
        assert(input::validate(r.plan).empty());
        assert(r.plan.actions.size() < 200);
        const auto* end = std::get_if<input::MovePointer>(&r.plan.actions.back());
        assert(end && end->x == 1799 && end->y == 200);
    }
    {
        // A turn during dragging cannot be removed, and releases must remain.
        const auto r = convert({
            {Kind::MouseDown, 0, 100, 100},
            {Kind::Move, 50, 130, 100},
            {Kind::Move, 100, 150, 100},
            {Kind::Move, 150, 150, 130},
            {Kind::Move, 200, 150, 160},
            {Kind::MouseUp, 250, 150, 160},
        });
        assert(r.error.empty());
        int moves = 0;
        for (const auto& a : r.plan.actions)
            if (std::holds_alternative<input::MovePointer>(a)) ++moves;
        assert(moves >= 3);
        assert(std::holds_alternative<input::ButtonUp>(r.plan.actions.back()));
    }
    {
        // A late first click keeps the initial wait instead of clicking early.
        const auto r = convert({{Kind::MouseDown, 1800, 60, 60},
                                {Kind::MouseUp, 1900, 60, 60}});
        assert(r.error.empty());
        assert(std::holds_alternative<input::Wait>(r.plan.actions.front()));
    }
    {
        // Typed letters from a browser are retained as a visible editable
        // TypeText step; the exact foreground app title follows the step.
        Sample first{Kind::Text, 0};
        first.text = u"П";
        first.window = {u"Поиск — Браузер", u"browser.exe"};
        Sample second{Kind::Text, 110};
        second.text = u"р";
        second.window = first.window;
        Sample third{Kind::Text, 210};
        third.text = u"и";
        third.window = first.window;
        const auto result = convert({first, second, third});
        assert(result.error.empty());
        assert(result.plan.actions.size() == 1);
        const auto* text = std::get_if<input::TypeText>(&result.plan.actions.front());
        assert(text && text->text == u"При");
        assert(result.windows.size() == result.plan.actions.size());
        assert(result.windows.front().title == first.window.title);
        assert(result.windows.front().process == first.window.process);
    }
    {
        // A new window must not join the previous window's typed text.
        Sample a{Kind::Text, 0}; a.text = u"a";
        a.window = {u"Browser", u"browser.exe"};
        Sample b{Kind::Text, 100}; b.text = u"b";
        b.window = {u"Editor", u"editor.exe"};
        const auto result = convert({a, b});
        assert(result.error.empty());
        assert(result.plan.actions.size() == 3); // Text, Wait, Text.
        assert(result.windows.size() == result.plan.actions.size());
        assert(result.windows.back().process == b.window.process);
    }
    {
        // Ctrl+C remains physical keyboard actions, not a text action.
        Sample downCtrl{Kind::KeyDown, 0}; downCtrl.virtualKey = 0x11;
        Sample downC{Kind::KeyDown, 50}; downC.virtualKey = 0x43;
        Sample upC{Kind::KeyUp, 100}; upC.virtualKey = 0x43;
        Sample upCtrl{Kind::KeyUp, 140}; upCtrl.virtualKey = 0x11;
        const auto result = convert({downCtrl, downC, upC, upCtrl});
        assert(result.error.empty());
        assert(input::validate(result.plan).empty());
        assert(!result.plan.actions.empty());
        for (const auto& action : result.plan.actions)
            assert(!std::holds_alternative<input::TypeText>(action));
    }
    {
        // Regression: multi-day idle time in an explicitly recorded scenario
        // must not be silently shortened to ten minutes or split into actions.
        constexpr std::uint64_t threeDays = 3ULL * 24 * 60 * 60 * 1000;
        Sample start{Kind::Text, 0}; start.text = u"A";
        Sample finish{Kind::Text, threeDays}; finish.text = u"B";
        const auto result = convert({start, finish});
        assert(result.error.empty());
        assert(result.plan.actions.size() == 3);
        assert(std::get<input::Wait>(result.plan.actions[1]).durationMs ==
               static_cast<std::int64_t>(threeDays));
        assert(input::validate(result.plan).empty());
    }
    {
        // Very large monotonic timestamps must never overflow signed Wait.
        Sample start{Kind::Text, 0}; start.text = u"A";
        Sample finish{Kind::Text, std::numeric_limits<std::uint64_t>::max()};
        finish.text = u"B";
        assert(!convert({start, finish}).error.empty());
    }
    {
        // A click, short key and long held modifier use one action each,
        // with timing and window metadata preserved.
        Sample a{Kind::MouseDown, 0, 42, 77}; a.window = {u"One", u"one.exe"};
        Sample b{Kind::MouseUp, 65, 42, 77}; b.window = a.window;
        Sample c{Kind::KeyDown, 180}; c.virtualKey = 0x11; c.window = a.window;
        Sample d{Kind::KeyUp, 470}; d.virtualKey = 0x11; d.window = a.window;
        const auto r = convert({a,b,c,d});
        assert(r.error.empty());
        assert(r.plan.actions.size() == 4); // Move, MouseHold, Wait, KeyHold.
        assert(std::holds_alternative<input::MouseHold>(r.plan.actions[1]));
        assert(std::get<input::MouseHold>(r.plan.actions[1]).durationMs == 65);
        assert(std::holds_alternative<input::KeyHold>(r.plan.actions.back()));
        assert(std::get<input::KeyHold>(r.plan.actions.back()).durationMs == 290);
        assert(r.windows.size() == r.plan.actions.size());
        assert(r.windows.back().process == u"one.exe");
    }
    {
        // More than 4096 typed characters must split into valid text steps
        // without dropping any characters.
        std::vector<Sample> typed;
        for (int i = 0; i < 5000; ++i) {
            Sample c{Kind::Text, static_cast<std::uint64_t>(i * 40)};
            c.text = u"x";
            typed.push_back(c);
        }
        const auto r = convert(typed);
        assert(r.error.empty());
        assert(r.plan.actions.size() >= 2);
        std::size_t count = 0;
        for (const auto& action : r.plan.actions)
            if (const auto* text = std::get_if<input::TypeText>(&action)) count += text->text.size();
        assert(count == 5000);
    }
    {
        // Regression: Alt+Tab changes the foreground window before Alt is
        // released. The release and its preceding wait must target the first
        // window; the next independent action must target the new one.
        const WindowInfo browser{u"Browser", u"browser.exe"};
        const WindowInfo editor{u"Editor", u"editor.exe"};
        Sample altDown{Kind::KeyDown, 0}; altDown.virtualKey = 0x12; altDown.window = browser;
        Sample tabDown{Kind::KeyDown, 40}; tabDown.virtualKey = 0x09; tabDown.window = browser;
        Sample tabUp{Kind::KeyUp, 110}; tabUp.virtualKey = 0x09; tabUp.window = editor;
        Sample altUp{Kind::KeyUp, 150}; altUp.virtualKey = 0x12; altUp.window = editor;
        Sample typed{Kind::Text, 230}; typed.text = u"A"; typed.window = editor;
        const auto converted = convert({altDown, tabDown, tabUp, altUp, typed});
        assert(converted.error.empty());
        assert(input::validate(converted.plan).empty());
        assert(converted.windows.size() == converted.plan.actions.size());
        assert(converted.windows.back().process == editor.process);
        bool gestureComplete = false;
        std::size_t releaseCount = 0;
        for (std::size_t i = 0; i < converted.plan.actions.size(); ++i) {
            assert(converted.windows[i].title ==
                   (gestureComplete ? editor.title : browser.title));
            if (const auto* release = std::get_if<input::KeyUp>(&converted.plan.actions[i])) {
                ++releaseCount;
                if (release->virtualKey == 0x12) gestureComplete = true;
            }
        }
        assert(gestureComplete && releaseCount == 2);
        // These labels become the saved per-action bindings on import.
        input::Plan bound = converted.plan;
        for (const auto& label : converted.windows) {
            // The test uses ASCII-only fixtures. Validate the selected target
            // explicitly instead of narrowing UTF-16 characters to char.
            assert(label.title == browser.title || label.title == editor.title);
            const bool isBrowser = label.title == browser.title;
            assert(label.process == (isBrowser ? browser.process : editor.process));
            bound.stepWindows.push_back(isBrowser
                ? input::StepWindow{"Browser", "browser.exe"}
                : input::StepWindow{"Editor", "editor.exe"});
        }
        assert(input::validate(bound).empty());
    }
    {
        // Dragging across two application windows is one held gesture.
        // The next separate click can select the second window normally.
        const WindowInfo source{u"Source", u"source.exe"};
        const WindowInfo destination{u"Destination", u"destination.exe"};
        Sample down{Kind::MouseDown, 0, 50, 50}; down.window = source;
        Sample move{Kind::Move, 70, 150, 150}; move.window = destination;
        Sample up{Kind::MouseUp, 110, 150, 150}; up.window = destination;
        Sample secondDown{Kind::MouseDown, 220, 155, 155}; secondDown.window = destination;
        Sample secondUp{Kind::MouseUp, 280, 155, 155}; secondUp.window = destination;
        const auto converted = convert({down, move, up, secondDown, secondUp});
        assert(converted.error.empty());
        bool afterRelease = false;
        bool sawFirstRelease = false;
        for (std::size_t i = 0; i < converted.plan.actions.size(); ++i) {
            if (!afterRelease) assert(converted.windows[i].title == source.title);
            else assert(converted.windows[i].title == destination.title);
            if (std::holds_alternative<input::ButtonUp>(converted.plan.actions[i])) {
                afterRelease = true;
                sawFirstRelease = true;
            }
        }
        assert(sawFirstRelease);
        assert(converted.windows.back().title == destination.title);
    }
    {
        // Straight movement across a window boundary retains the boundary.
        Sample a{Kind::Move, 0, 10, 10}; a.window = {u"A", u"a.exe"};
        Sample b{Kind::Move, 40, 20, 20}; b.window = a.window;
        Sample c{Kind::Move, 80, 30, 30}; c.window = {u"B", u"b.exe"};
        Sample d{Kind::Move, 120, 40, 40}; d.window = c.window;
        const auto converted = convert({a, b, c, d});
        assert(converted.error.empty());
        bool seenA = false, seenB = false;
        for (const auto& w : converted.windows) {
            if (w.process == u"a.exe") seenA = true;
            if (w.process == u"b.exe") seenB = true;
        }
        assert(seenA && seenB);
    }
    {
        std::vector<Sample> huge;
        for (int i = 0; i < 700; ++i) huge.push_back({Kind::Wheel, static_cast<std::uint64_t>(i * 100), 0, 0, {}, input::Axis::Vertical, 120});
        assert(!convert(huge).error.empty());
    }
}
