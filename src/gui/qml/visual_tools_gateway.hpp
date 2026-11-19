// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/visual_tools_policy.hpp"
#include <QVariantMap>
#include <QString>

namespace clicknova::gui::qml {
// Deliberately small QML projection: no screenshot bytes, OCR output,
// image paths, selector strings, target HWND/PID or private window titles.
QVariantMap describeVisualTools(const clicknova::visual::tools::Assessment& state,
                                bool ukrainian, const QString& lastRequestKey);
} // namespace clicknova::gui::qml
