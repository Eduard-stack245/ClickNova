// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/input_actions.hpp"
#include "clicknova/sequence.hpp"
#include <optional>
#include <string>

namespace clicknova::sequence {
// Convert legacy clicks into the shared editor. Client coordinates retain
// a required foreground-window binding; per-action targets remain unsupported
// because they cannot be represented safely by one shared window binding.
std::optional<clicknova::input::Plan> toInputPlan(const Plan& plan,
                                                 std::string* error = nullptr,
                                                 bool requireDefaultWindow = false);
} // namespace clicknova::sequence
