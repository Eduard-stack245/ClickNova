// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/input_actions.hpp"
#include "clicknova/accessibility_inspection_policy.hpp"
#include <QVariantMap>

namespace clicknova::gui::qml {
// Strictly read-only view of provider metadata. This is NOT a live UIA probe.
// Selector strings, window names, tab names, element values, HWND/PID and COM
// objects must never be added to the returned map.
clicknova::accessibility::Facts accessibilityFacts(const input::Action* action,
                                                    const input::StepWindow* binding);
QVariantMap describeAccessibility(const input::Action* action,
                                 const input::StepWindow* binding,
                                 bool ukrainian);
} // namespace clicknova::gui::qml
