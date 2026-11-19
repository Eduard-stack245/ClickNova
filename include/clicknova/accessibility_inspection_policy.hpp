// SPDX-License-Identifier: MIT
#pragma once

// Describes *stored intent*, not live Windows UI Automation availability.
// No COM handles, selectors, process names, window titles or input data enter
// this policy. These facts can be tested without Qt or Windows.
namespace clicknova::accessibility {
enum class StepKind { None, Other, LeftClick, OtherClick, ElementTask };
enum class State { NoSelection, NotApplicable, NoSelector, Configured, InvalidProvider };
struct Facts {
    StepKind step{StepKind::None};
    int provider{0};                 // 0 UIA, 1 MSAA, 2 Java, 3 image, 4 OCR
    bool hasName{false};
    bool hasAutomationId{false};
    bool hasClass{false};
    bool hasBoundWindow{false};
    bool hasRecordedWindow{false};
    bool hasBrowserScope{false};
};
struct Assessment {
    State state{State::NoSelection};
    bool applicable{false};
    bool configured{false};
    bool canRequestSearch{false};
    bool canRequestClear{false};
    bool providerKnown{false};
    bool requiresLiveVerification{true};
    bool hasBoundWindow{false};
    bool hasRecordedWindow{false};
    bool hasBrowserScope{false};
};
constexpr Assessment inspect(Facts f) noexcept {
    Assessment r;
    r.applicable = f.step == StepKind::LeftClick || f.step == StepKind::ElementTask;
    r.configured = f.hasName || f.hasAutomationId || f.hasClass;
    r.providerKnown = f.provider >= 0 && f.provider <= 4;
    r.hasBoundWindow = f.hasBoundWindow;
    r.hasRecordedWindow = f.hasRecordedWindow;
    r.hasBrowserScope = f.hasBrowserScope;
    // Removing a stale/invalid selector is allowed as remediation. Execution is
    // never authorized by this descriptor or by its presentation flags.
    r.canRequestClear = f.step != StepKind::None && r.configured;
    r.canRequestSearch = r.applicable && (!r.configured || r.providerKnown);
    if (f.step == StepKind::None) r.state = State::NoSelection;
    else if (!r.applicable) r.state = State::NotApplicable;
    else if (!r.configured) r.state = State::NoSelector;
    else if (!r.providerKnown) r.state = State::InvalidProvider;
    else r.state = State::Configured;
    return r;
}
} // namespace clicknova::accessibility
