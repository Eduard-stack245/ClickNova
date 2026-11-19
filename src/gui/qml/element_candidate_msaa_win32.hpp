// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/element_candidate_search.hpp"
#include <memory>
namespace clicknova::gui::qml {
// Read-only MSAA; candidate names/values/identifiers are never requested.
// A returned candidate is NOT eligible for Verify and bind.
std::shared_ptr<accessibility::candidates::IProvider> createNativeMsaaCandidateProvider();
}
