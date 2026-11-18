// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/input_actions.hpp"
#include <QString>

namespace clicknova::gui::qml {
// A read-only presentation contract for the C++ action model. The QML layer
// does not inspect QTableWidget cells, parse the saved plan format or receive
// typed text and script bodies as part of an action-list summary.
struct ActionSummary {
    QString title;
    QString details;
    QString kind;
    bool editable{true};
    bool hasRequiredWindow{false};
};

ActionSummary describeAction(const input::Action& action,
                             const input::StepWindow* binding,
                             bool ukrainian);
} // namespace clicknova::gui::qml
