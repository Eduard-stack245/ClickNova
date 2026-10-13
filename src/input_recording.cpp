// SPDX-License-Identifier: Apache-2.0
#include "clicknova/input_recording.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include <set>
#include <type_traits>

namespace clicknova::recording {
namespace {
// Keep this in sync with input::validate and the plan codec. The limit
// protects memory/serialization; the duration of a single Wait is int64_t.
// Conversion needs temporary room for raw down/wait/up triplets; the
// persisted 512-action limit is enforced AFTER semantic compaction.
constexpr std::size_t kMaximumIntermediateActions = 32768;
constexpr std::size_t kMaximumActions = 512;

bool append(Conversion& out, input::Action action, const WindowInfo& source) {
    if (out.plan.actions.size() >= kMaximumIntermediateActions) return false;
    out.plan.actions.push_back(std::move(action));
    out.windows.push_back(source);
    return true;
}

bool appendWait(Conversion& out, std::uint64_t duration, const WindowInfo& source) {
    // The plan already supports 64-bit, cancellable multi-day waits. Preserve
    // the actual recorded gap instead of silently truncating it to ten minutes
    // or filling the 512-action limit with one-minute slices.
    if (duration < 25) return true; // Ignore scheduler jitter between events.
    if (duration > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return false;
    return append(out, input::Wait{static_cast<std::int64_t>(duration)}, source);
}
// Reduce redundant cursor samples while preserving the first/last point,
// turns, button transitions, and a bounded time between retained samples.
// The shorter interval during a drag protects its visible trajectory.
double distanceFromSegment(const Sample& point, const Sample& first,
                           const Sample& last) {
    const double dx = static_cast<double>(last.x) - first.x;
    const double dy = static_cast<double>(last.y) - first.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared == 0.0)
        return std::hypot(static_cast<double>(point.x - first.x),
                          static_cast<double>(point.y - first.y));
    const double factor = std::clamp(
        ((static_cast<double>(point.x - first.x) * dx) +
         (static_cast<double>(point.y - first.y) * dy)) / lengthSquared, 0.0, 1.0);
    return std::hypot(static_cast<double>(point.x - first.x) - factor * dx,
                      static_cast<double>(point.y - first.y) - factor * dy);
}

std::vector<Sample> compactMoves(const std::vector<Sample>& samples) {
    std::vector<Sample> compacted;
    compacted.reserve(samples.size());
    std::set<input::Button> heldButtons;
    std::size_t i = 0;
    while (i < samples.size()) {
        if (samples[i].kind != Kind::Move) {
            const auto& event = samples[i];
            if (event.kind == Kind::MouseDown) heldButtons.insert(event.button);
            if (event.kind == Kind::MouseUp) heldButtons.erase(event.button);
            compacted.push_back(event);
            ++i;
            continue;
        }
        const std::size_t begin = i;
        // A pointer crossing into another window is a meaningful boundary.
        // Keep its first point even if the path on screen is perfectly straight.
        while (i < samples.size() && samples[i].kind == Kind::Move &&
               samples[i].window.title == samples[begin].window.title &&
               samples[i].window.process == samples[begin].window.process) ++i;
        const std::size_t end = i;
        compacted.push_back(samples[begin]);
        std::size_t kept = begin;
        const std::uint64_t maximumGap = heldButtons.empty() ? 500 : 180;
        const double maximumDeviation = heldButtons.empty() ? 5.0 : 2.0;
        while (kept + 1 < end) {
            std::size_t best = kept + 1;
            for (std::size_t candidate = kept + 2; candidate < end; ++candidate) {
                if (samples[candidate].milliseconds - samples[kept].milliseconds > maximumGap)
                    break;
                bool matchesPath = true;
                for (std::size_t middle = kept + 1; middle < candidate; ++middle) {
                    if (distanceFromSegment(samples[middle], samples[kept],
                                            samples[candidate]) > maximumDeviation) {
                        matchesPath = false;
                        break;
                    }
                }
                if (!matchesPath) break;
                best = candidate;
            }
            if (best == kept + 1 && best == end - 1) {
                if (best != begin) compacted.push_back(samples[best]);
            } else {
                compacted.push_back(samples[best]);
            }
            kept = best;
        }
    }
    return compacted;
}

// A held key/button belongs to one input gesture. During Alt+Tab and drags,
// Windows can report the new foreground/hovered window before the matching
// release, while playback must keep one target until the gesture is complete.
// Pin only that gesture to the window where its first press occurred; the next
// independent action can use the newly observed target. Raw checkpoints retain
// the original per-event window information for recovery and inspection.
void stabilizeHeldWindows(Conversion& result) {
    std::set<input::Button> heldButtons;
    std::set<int> heldKeys;
    WindowInfo gestureWindow;
    for (std::size_t i = 0; i < result.plan.actions.size(); ++i) {
        const auto& action = result.plan.actions[i];
        const bool holding = !heldButtons.empty() || !heldKeys.empty();
        if (!holding) gestureWindow = result.windows[i];
        if (holding) result.windows[i] = gestureWindow;
        if (const auto* down = std::get_if<input::ButtonDown>(&action))
            heldButtons.insert(down->button);
        else if (const auto* up = std::get_if<input::ButtonUp>(&action))
            heldButtons.erase(up->button);
        else if (const auto* keyDown = std::get_if<input::KeyDown>(&action))
            heldKeys.insert(keyDown->virtualKey);
        else if (const auto* keyUp = std::get_if<input::KeyUp>(&action))
            heldKeys.erase(keyUp->virtualKey);
    }
}
} // namespace

Conversion convert(const std::vector<Sample>& samples) {
    Conversion result;
    if (samples.empty()) { result.error = "Nothing was recorded."; return result; }
    // Check the raw timeline before compacting: removed movement samples must
    // never hide an invalid timestamp sequence.
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (samples[i].milliseconds < samples[i - 1].milliseconds) {
            result.error = "Event timestamps are not ordered.";
            return result;
        }
    }
    std::set<input::Button> buttons;
    std::set<int> keys;
    std::uint64_t lastTime = 0;
    const auto compacted = compactMoves(samples);
    bool havePosition = false;
    int lastX = 0;
    int lastY = 0;
    WindowInfo mostRecentWindow;
    for (const auto& s : compacted) {
        if (s.milliseconds < lastTime) { result.error = "Event timestamps are not ordered."; return result; }
        const auto elapsed = s.milliseconds - lastTime;
        mostRecentWindow = s.window;
        bool added = true;
        if (s.kind == Kind::Move) {
            if (!havePosition && !appendWait(result, elapsed, s.window)) added = false;
            if (s.x < -32768 || s.x > 32767 || s.y < -32768 || s.y > 32767) {
                result.error = "Recorded cursor coordinate is outside supported range."; return result;
            }
            if (added && (!havePosition || lastX != s.x || lastY != s.y)) {
                if (added) added = append(result, input::MovePointer{
                    input::MoveMode::Absolute, s.x, s.y,
                    havePosition ? static_cast<int>(std::min<std::uint64_t>(elapsed, 60000)) : 0}, s.window);
                havePosition = true;
                lastX = s.x; lastY = s.y;
            }
        } else {
            // A short run of printable keystrokes is one editable text step.
            // Do not insert a wait between characters inside the same step.
            const bool mergeText = s.kind == Kind::Text && elapsed < 800 &&
                !result.plan.actions.empty() &&
                std::holds_alternative<input::TypeText>(result.plan.actions.back()) &&
                result.windows.back().title == s.window.title &&
                result.windows.back().process == s.window.process &&
                std::get<input::TypeText>(result.plan.actions.back()).text.size() + s.text.size() <= 4096;
            if (!mergeText && !appendWait(result, elapsed, s.window)) added = false;
            if (added && (s.kind == Kind::MouseDown || s.kind == Kind::MouseUp) &&
                (!havePosition || lastX != s.x || lastY != s.y)) {
                added = append(result, input::MovePointer{input::MoveMode::Absolute, s.x, s.y, 0}, s.window);
                havePosition = true;
                lastX = s.x; lastY = s.y;
            }
            if (added) {
                switch (s.kind) {
                case Kind::MouseDown:
                    if (buttons.insert(s.button).second) added = append(result, input::ButtonDown{s.button}, s.window);
                    break;
                case Kind::MouseUp:
                    if (buttons.erase(s.button)) added = append(result, input::ButtonUp{s.button}, s.window);
                    break;
                case Kind::Wheel:
                    if (s.delta != 0) {
                        // Windows wheel deltas may arrive greater than executor limits.
                        int remaining = s.delta;
                        while (remaining != 0 && added) {
                            const int amount = std::clamp(remaining, -1200, 1200);
                            added = append(result, input::Wheel{s.axis, amount}, s.window);
                            remaining -= amount;
                        }
                    }
                    break;
                case Kind::KeyDown:
                    if (keys.insert(s.virtualKey).second) added = append(result, input::KeyDown{s.virtualKey}, s.window);
                    break;
                case Kind::KeyUp:
                    if (keys.erase(s.virtualKey)) added = append(result, input::KeyUp{s.virtualKey}, s.window);
                    break;
                case Kind::Text:
                    if (!s.text.empty()) {
                        // Keystroke text is kept as an editable TypeText action.
                        // Adjacent characters form a word/phrase until another
                        // action or a noticeable timing gap separates them.
                        if (mergeText && !result.plan.actions.empty() &&
                            std::holds_alternative<input::TypeText>(result.plan.actions.back()) &&
                            result.windows.back().title == s.window.title &&
                            result.windows.back().process == s.window.process) {
                            std::get<input::TypeText>(result.plan.actions.back()).text += s.text;
                        } else added = append(result, input::TypeText{s.text}, s.window);
                    }
                    break;
                case Kind::Move: break;
                }
            }
        }
        if (!added) { result.error = "Recording is too long (maximum 512 actions)."; return result; }
        lastTime = s.milliseconds;
    }
    for (auto key : keys) {
        if (!append(result, input::KeyUp{key}, mostRecentWindow)) {
            result.error = "Recording is too long to release all keys."; return result;
        }
        result.completedHolds = true;
    }
    for (auto b : buttons) {
        if (!append(result, input::ButtonUp{b}, mostRecentWindow)) {
            result.error = "Recording is too long to release all buttons."; return result;
        }
        result.completedHolds = true;
    }
    // A completed key or button press is one editable step. Keep the actual
    // duration when a nontrivial pause was recorded between down and up.
    // Never combine across an intervening action or a different window.
    std::vector<input::Action> reduced;
    std::vector<WindowInfo> reducedWindows;
    reduced.reserve(result.plan.actions.size());
    reducedWindows.reserve(result.windows.size());
    for (std::size_t i = 0; i < result.plan.actions.size();) {
        const auto& first = result.plan.actions[i];
        const auto& origin = result.windows[i];
        std::size_t release = i + 1;
        std::int64_t pressMs = 0;
        if (release < result.plan.actions.size()) {
            if (auto* pause = std::get_if<input::Wait>(&result.plan.actions[release])) {
                if (pause->durationMs > 0 && pause->durationMs <= 60'000 &&
                    result.windows[release].title == origin.title &&
                    result.windows[release].process == origin.process) {
                    pressMs = pause->durationMs;
                    ++release;
                }
            }
        }
        const bool sameWindow = release < result.plan.actions.size() &&
            result.windows[release].title == origin.title &&
            result.windows[release].process == origin.process;
        if (sameWindow) {
            if (auto* down = std::get_if<input::ButtonDown>(&first)) {
                if (auto* up = std::get_if<input::ButtonUp>(&result.plan.actions[release]);
                    up && down->button == up->button) {
                    if (pressMs > 0) reduced.emplace_back(input::MouseHold{down->button, static_cast<int>(pressMs)});
                    else reduced.emplace_back(input::Click{down->button, 1, 0});
                    reducedWindows.push_back(origin);
                    i = release + 1;
                    continue;
                }
            }
            if (auto* down = std::get_if<input::KeyDown>(&first)) {
                if (auto* up = std::get_if<input::KeyUp>(&result.plan.actions[release]);
                    up && down->virtualKey == up->virtualKey) {
                    if (pressMs > 0) reduced.emplace_back(input::KeyHold{down->virtualKey, static_cast<int>(pressMs)});
                    else reduced.emplace_back(input::KeyTap{down->virtualKey});
                    reducedWindows.push_back(origin);
                    i = release + 1;
                    continue;
                }
            }
        }
        reduced.push_back(first);
        reducedWindows.push_back(origin);
        ++i;
    }
    result.plan.actions = std::move(reduced);
    result.windows = std::move(reducedWindows);
    stabilizeHeldWindows(result);
    if (result.plan.actions.size() > kMaximumActions) {
        result.error = "Recording contains more than 512 meaningful steps; shorten the session or split it into scenarios.";
        return result;
    }
    result.error = input::validate(result.plan);
    return result;
}
} // namespace clicknova::recording
