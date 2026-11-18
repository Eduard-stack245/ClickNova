// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/visual_worker_protocol.hpp"

namespace clicknova::gui::qml {
// No caller-provided output filename, no screenshots or recognized text returned.
// This must run ONLY in the child --visual-worker process.
clicknova::visual::worker::Reply inspectVisualTarget(
    const clicknova::visual::worker::Request& request) noexcept;
}
