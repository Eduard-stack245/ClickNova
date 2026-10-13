// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/input_actions.hpp"
#include <QWidget>
#include <optional>

// Captures only inside this focused dialog. No system-wide hooks or key logging.
std::optional<clicknova::input::Button> chooseMouseButtonByPress(QWidget* parent, bool russian);
std::optional<int> chooseVirtualKeyByPress(QWidget* parent, bool russian);
