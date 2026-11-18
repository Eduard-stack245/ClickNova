// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/element_candidate_search.hpp"
#include <memory>
namespace clicknova::gui::qml {
// Read-only Java Access Bridge provider, strictly hosted in --uia-worker.
// A Java candidate is never eligible for native Verify/bind by ordinal.
std::shared_ptr<accessibility::candidates::IProvider> createNativeJavaCandidateProvider();
}
