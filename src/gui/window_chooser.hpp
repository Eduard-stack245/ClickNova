// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/window_target.hpp"

#include <functional>
#include <optional>
#include <vector>

class QWidget;
class QString;

// Explicit list-based target selection without global input hooks.
std::optional<clicknova::platform::WindowTarget> chooseTargetWindow(
    QWidget* parent, const QString& language,
    std::function<std::vector<clicknova::platform::WindowTarget>()> provider = clicknova::platform::listWindows);
