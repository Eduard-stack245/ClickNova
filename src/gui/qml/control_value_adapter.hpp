// SPDX-License-Identifier: MIT
#pragma once
#include <QVariant>

class QObject;

namespace clicknova::gui::qml {
// Validates and serializes individual native field values. This module knows
// nothing about the step model, window selection, or automation execution.
// A future typed application service can reuse the same conversion contract.
class ControlValueAdapter final {
public:
    ControlValueAdapter() = delete;
    static QVariant read(const QObject* source);
    // Returns false instead of silently clamping, truncating or ignoring input.
    // The caller remains responsible for committing model-specific changes.
    static bool write(QObject* target, const QVariant& requested);
};
} // namespace clicknova::gui::qml
