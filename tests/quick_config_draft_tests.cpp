// SPDX-License-Identifier: MIT
#include "clicknova/quick_config_draft.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string_view>

using namespace clicknova::quick;

int main() {
    unsigned long long checks = 0;
    const auto expect = [&](bool ok) { ++checks; if (!ok) std::abort(); };
    constexpr std::string_view expected[] = {
        "quickActionMode", "quickMouseButton", "quickClickInterval", "quickClickCount",
        "quickUnlimited", "quickHoldDuration", "quickStartMode", "quickStartDelayValue",
        "quickStartDelayUnit", "quickStartAtTime", "quickKeepAwake", "positionMode",
        "coordinateX", "coordinateY", "quickDestinationX", "quickDestinationY",
        "quickMoveDuration", "targetMode", "destinationConfigured"};
    for (const auto field : expected) expect(draftField(field) != DraftField::Invalid);
    expect(draftField("quickTargetHwnd") == DraftField::Invalid);
    expect(draftField("targetWindowHandle") == DraftField::Invalid);
    expect(draftField("quickClickInterval ") == DraftField::Invalid);
    ConfigDraft defaults;
    const ConfigDraft original = defaults;
    expect(!assignDraftInt(defaults, DraftField::Interval, 9));
    expect(!assignDraftInt(defaults, DraftField::Interval, 60001));
    expect(!assignDraftInt(defaults, DraftField::Count, 0));
    expect(!assignDraftInt(defaults, DraftField::Count, INT64_MAX));
    expect(!assignDraftInt(defaults, DraftField::TargetMode, 2));
    expect(!assignDraftInt(defaults, DraftField::Button, 5));
    expect(!assignDraftInt(defaults, DraftField::DestinationX, -131073));
    expect(!assignDraftInt(defaults, DraftField::DestinationY, 131073));
    expect(!assignDraftInt(defaults, DraftField::Movement, -1));
    expect(!assignDraftBool(defaults, DraftField::Count, true));
    expect(!assignDraftInt(defaults, DraftField::KeepAwake, 1));
    expect(defaults == original);
    for (int i = 0; i <= 3; ++i) {
        auto current = original;
        expect(assignDraftInt(current, DraftField::Action, i));
        expect(current.action == i);
    }
    for (int i = -131072; i <= 131072; i += 53) {
        auto current = original;
        expect(assignDraftInt(current, DraftField::X, i));
        expect(current.x == i);
        expect(assignDraftInt(current, DraftField::DestinationY, i));
        expect(current.destinationY == i && current.destinationConfigured);
    }
    ConfigDraft clock = original;
    expect(assignDraftClock(clock, "00:00:00"));
    expect(clock.clockHour == 0 && clock.clockMinute == 0 && clock.clockSecond == 0);
    expect(assignDraftClock(clock, "23:59:59"));
    expect(clock.clockHour == 23 && clock.clockMinute == 59 && clock.clockSecond == 59);
    const ConfigDraft beforeBadClock = clock;
    for (auto bad : {"24:00:00", "12:60:00", "12:59:60", "2:30:00", "12:3:00",
                     " 1:02:03", "12-30-00", "xx:30:00", "12:30:00\n", "12:30:00X"}) {
        expect(!assignDraftClock(clock, bad));
        expect(clock == beforeBadClock);
    }
    expect(assignDraftBool(clock, DraftField::DestinationConfigured, false));
    expect(!clock.destinationConfigured);
    expect(assignDraftInt(clock, DraftField::DestinationX, 0));
    expect(clock.destinationConfigured); // Setting a 0, even if unchanged, means explicitly selected.
    std::cout << "Quick configuration draft: " << checks << " checks passed\n";
}
