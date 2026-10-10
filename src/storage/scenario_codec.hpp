// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "clicknova/sequence.hpp"
#include <QPoint>
#include <QString>
#include <QVariantMap>
#include <vector>

namespace clicknova::storage {

// Transfer objects: no QWidget dependency, so persistence can be tested independently.
struct AutoClickerScenario {
    int button{0};
    int intervalMs{200};
    bool unlimited{true};
    int count{100};
    bool fixed{false};
    bool bound{false};
    QPoint screen;
    QPoint client;
    QString windowTitle;
};

struct SequenceScenario {
    int repeatCount{100};
    bool unlimited{true};
    int passDelayMs{0};
    std::vector<sequence::Step> steps;
};

// Decode into a temporary object. On failure, the destination remains unchanged.
QVariantMap encodeScenario(const AutoClickerScenario& scenario);
QVariantMap encodeScenario(const SequenceScenario& scenario);
bool decodeScenario(const QVariantMap& values, AutoClickerScenario& output,
                    QString* error = nullptr);
bool decodeScenario(const QVariantMap& values, SequenceScenario& output,
                    QString* error = nullptr);
} // namespace clicknova::storage
