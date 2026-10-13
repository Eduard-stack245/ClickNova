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
    int holdDurationMs{0};
    int actionMode{0}; // Click, move, drag, or hold until Stop.
    int destinationX{0};
    int destinationY{0};
    int movementMs{600};
    bool destinationConfigured{false};
    bool unlimited{true};
    int count{100};
    int startDelayValue{3};
    int startDelayUnit{0};  // 0 seconds, 1 minutes, 2 hours, 3 days.
    int startMode{0};       // 0 after delay, 1 at specific local time.
    int startTimeSeconds{18 * 3600 + 30 * 60}; // HH:mm:ss encoded as seconds since midnight.
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
