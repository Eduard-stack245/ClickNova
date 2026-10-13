// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/input_actions.hpp"
#include <QVariantMap>
#include <QString>
#include <QJsonObject>
#include <optional>

namespace clicknova::storage {
// Versioned, strictly validated representation of an experimental input plan.
// Portable action configuration is stored, including intentionally entered text.
// It never stores captured device-event histories or device IDs.
// Single-action codec for the editor clipboard. No plan-level hold balancing is
// imposed on one copied action; the full plan is validated before playback.
QJsonObject encodeInputAction(const input::Action& action);
QJsonObject encodeBoundInputAction(const input::Action& action, const input::StepWindow& window);
std::optional<input::StepWindow> decodeStepWindow(const QJsonObject& object);
std::optional<input::Action> decodeInputAction(const QJsonObject& data);
QVariantMap encodeInputPlan(const input::Plan& plan);
std::optional<input::Plan> decodeInputPlan(const QVariantMap& values, QString* error = nullptr);
}
