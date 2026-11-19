// SPDX-License-Identifier: MIT
#pragma once
#include "clicknova/visual_worker_protocol.hpp"
#include <QString>
#include <stop_token>

namespace clicknova::gui::qml::visual_transport {
enum class Outcome {Success,Cancelled,TimedOut,Unavailable,Malformed};
struct Result {
    Outcome outcome{Outcome::Unavailable};
    clicknova::visual::worker::Reply reply{};
};
// Production always runs QCoreApplication::applicationFilePath() directly.
// No shell, temp files, user-controlled argv, inherited stderr or image data.
Result inspect(const clicknova::visual::worker::Request& request,
               std::stop_token cancel);
}
