// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/element_candidate_search.hpp"
#include <memory>
namespace clicknova::gui::qml {
// Production-only factory. No fallback to in-process COM if worker fails.
std::shared_ptr<accessibility::candidates::IProvider> createIsolatedUiaCandidateProvider();
// 0 UIA, 1 MSAA read-only, 2 Java Access Bridge read-only. Others refused.
std::shared_ptr<accessibility::candidates::IProvider>
createIsolatedAccessibilityCandidateProvider(int provider);
}
