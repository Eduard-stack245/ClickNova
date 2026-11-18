// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/element_candidate_search.hpp"
#include <memory>
namespace clicknova::gui::qml {
// UIA-only, read-only Windows provider. Unsupported systems return nullptr;
// MSAA/Java keep their original picker until a native adapter is validated.
std::shared_ptr<accessibility::candidates::IProvider> createNativeUiaCandidateProvider();
}
