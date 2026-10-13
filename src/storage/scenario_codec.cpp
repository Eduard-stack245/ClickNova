// SPDX-License-Identifier: Apache-2.0
#include "scenario_codec.hpp"

#include <QMetaType>
#include <QVariantList>
#include <cmath>
#include <limits>
#include <utility>

namespace clicknova::storage {
namespace {
void fail(QString* error, const QString& reason) {
    if (error) *error = reason;
}

bool integer(const QVariantMap& map, const QString& key, int lower, int upper,
             int& output, QString* error, bool optional = false) {
    const auto it = map.constFind(key);
    if (it == map.cend()) {
        if (optional) return true;
        fail(error, QStringLiteral("Missing setting: %1").arg(key));
        return false;
    }
    const QVariant& value = it.value();
    // QString and bool must not silently become integers through QVariant conversion.
    const int type = value.metaType().id();
    if (type == QMetaType::QString || type == QMetaType::Bool || !value.canConvert<double>()) {
        fail(error, QStringLiteral("Invalid number: %1").arg(key));
        return false;
    }
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || std::trunc(number) != number ||
        number < lower || number > upper) {
        fail(error, QStringLiteral("Number out of range: %1").arg(key));
        return false;
    }
    output = static_cast<int>(number);
    return true;
}

bool boolean(const QVariantMap& map, const QString& key, bool& output,
             QString* error, bool optional = false) {
    const auto it = map.constFind(key);
    if (it == map.cend() && optional) return true;
    if (it == map.cend() || it.value().metaType().id() != QMetaType::Bool) {
        fail(error, QStringLiteral("Invalid boolean: %1").arg(key));
        return false;
    }
    output = it.value().toBool();
    return true;
}

bool title(const QVariantMap& map, const QString& key, QString& output, QString* error) {
    const auto it = map.constFind(key);
    if (it == map.cend()) { output.clear(); return true; }
    if (it.value().metaType().id() != QMetaType::QString ||
        it.value().toString().size() > 512) {
        fail(error, QStringLiteral("Invalid window title"));
        return false;
    }
    output = it.value().toString();
    return true;
}
} // namespace

QVariantMap encodeScenario(const AutoClickerScenario& s) {
    return {{QStringLiteral("button"), s.button},
            {QStringLiteral("interval"), s.intervalMs},
            {QStringLiteral("holdDurationMs"), s.holdDurationMs},
            {QStringLiteral("actionMode"), s.actionMode},
            {QStringLiteral("destinationX"), s.destinationX},
            {QStringLiteral("destinationY"), s.destinationY},
            {QStringLiteral("movementMs"), s.movementMs},
            {QStringLiteral("destinationConfigured"), s.destinationConfigured},
            {QStringLiteral("unlimited"), s.unlimited},
            {QStringLiteral("count"), s.count},
            {QStringLiteral("startDelayValue"), s.startDelayValue},
            {QStringLiteral("startDelayUnit"), s.startDelayUnit},
            {QStringLiteral("startMode"), s.startMode},
            {QStringLiteral("startTimeSeconds"), s.startTimeSeconds},
            {QStringLiteral("fixed"), s.fixed},
            {QStringLiteral("bound"), s.bound},
            {QStringLiteral("screenX"), s.screen.x()},
            {QStringLiteral("screenY"), s.screen.y()},
            {QStringLiteral("clientX"), s.client.x()},
            {QStringLiteral("clientY"), s.client.y()},
            {QStringLiteral("windowTitle"), s.windowTitle}};
}

QVariantMap encodeScenario(const SequenceScenario& s) {
    QVariantList steps;
    for (const auto& step : s.steps) {
        steps.append(QVariantMap{{QStringLiteral("x"), step.x},
                                 {QStringLiteral("y"), step.y},
                                 {QStringLiteral("button"), step.button},
                                 {QStringLiteral("delayMs"), step.delayMs},
                                 {QStringLiteral("intervalMs"), step.intervalMs},
                                 {QStringLiteral("space"), static_cast<int>(step.space)},
                                 {QStringLiteral("clickCount"), step.clickCount},
                                 {QStringLiteral("configured"), step.configured},
                                 {QStringLiteral("ownWindow"), step.ownWindow},
                                 {QStringLiteral("windowTitle"), QString::fromStdString(step.windowTitle)}});
    }
    return {{QStringLiteral("repeatCount"), s.repeatCount},
            {QStringLiteral("unlimited"), s.unlimited},
            {QStringLiteral("passDelayMs"), s.passDelayMs},
            {QStringLiteral("steps"), steps}};
}

bool decodeScenario(const QVariantMap& values, AutoClickerScenario& output, QString* error) {
    AutoClickerScenario result;
    int screenX = 0, screenY = 0, clientX = 0, clientY = 0;
    if (!integer(values, QStringLiteral("button"), 0, 4, result.button, error) ||
        !integer(values, QStringLiteral("holdDurationMs"), 0, 60000, result.holdDurationMs, error, true) ||
        !integer(values, QStringLiteral("actionMode"), 0, 3, result.actionMode, error, true) ||
        !integer(values, QStringLiteral("destinationX"), -131072, 131072, result.destinationX, error, true) ||
        !integer(values, QStringLiteral("destinationY"), -131072, 131072, result.destinationY, error, true) ||
        !integer(values, QStringLiteral("movementMs"), 0, 60000, result.movementMs, error, true) ||
        !boolean(values, QStringLiteral("destinationConfigured"), result.destinationConfigured, error, true) ||
        !integer(values, QStringLiteral("interval"), 10, 60000, result.intervalMs, error) ||
        !integer(values, QStringLiteral("count"), 1, std::numeric_limits<int>::max(), result.count, error) ||
        !integer(values, QStringLiteral("startDelayValue"), 0, std::numeric_limits<int>::max(),
                 result.startDelayValue, error, true) ||
        !integer(values, QStringLiteral("startDelayUnit"), 0, 3,
                 result.startDelayUnit, error, true) ||
        !integer(values, QStringLiteral("startMode"), 0, 1,
                 result.startMode, error, true) ||
        !integer(values, QStringLiteral("startTimeSeconds"), 0, 86399,
                 result.startTimeSeconds, error, true) ||
        !integer(values, QStringLiteral("screenX"), -131072, 131072, screenX, error) ||
        !integer(values, QStringLiteral("screenY"), -131072, 131072, screenY, error) ||
        !integer(values, QStringLiteral("clientX"), -131072, 131072, clientX, error) ||
        !integer(values, QStringLiteral("clientY"), -131072, 131072, clientY, error) ||
        !boolean(values, QStringLiteral("unlimited"), result.unlimited, error) ||
        !boolean(values, QStringLiteral("fixed"), result.fixed, error) ||
        !boolean(values, QStringLiteral("bound"), result.bound, error) ||
        !title(values, QStringLiteral("windowTitle"), result.windowTitle, error)) return false;
    if (result.bound && result.destinationConfigured &&
        (result.destinationX < 0 || result.destinationY < 0)) {
        fail(error, QStringLiteral("Window destination must be non-negative"));
        return false;
    }
    if (result.bound && (clientX < 0 || clientY < 0)) {
        fail(error, QStringLiteral("Window coordinates must be non-negative"));
        return false;
    }
    result.screen = QPoint(screenX, screenY);
    result.client = QPoint(clientX, clientY);
    output = std::move(result);
    return true;
}

bool decodeScenario(const QVariantMap& values, SequenceScenario& output, QString* error) {
    SequenceScenario result;
    if (!integer(values, QStringLiteral("repeatCount"), 1, 1000000,
                 result.repeatCount, error) ||
        !integer(values, QStringLiteral("passDelayMs"), 0, 60000,
                 result.passDelayMs, error, true) ||
        !boolean(values, QStringLiteral("unlimited"), result.unlimited, error)) return false;
    const auto it = values.constFind(QStringLiteral("steps"));
    if (it == values.cend() || it.value().metaType().id() != QMetaType::QVariantList) {
        fail(error, QStringLiteral("Missing sequence actions"));
        return false;
    }
    const auto records = it.value().toList();
    if (records.empty() || records.size() > 128) {
        fail(error, QStringLiteral("Sequence must contain 1-128 actions"));
        return false;
    }
    result.steps.reserve(static_cast<std::size_t>(records.size()));
    for (const auto& record : records) {
        if (record.metaType().id() != QMetaType::QVariantMap) {
            fail(error, QStringLiteral("Invalid action object"));
            return false;
        }
        const auto map = record.toMap();
        sequence::Step step;
        int space = 0;
        QString windowTitle;
        if (!integer(map, QStringLiteral("x"), -131072, 131072, step.x, error) ||
            !integer(map, QStringLiteral("y"), -131072, 131072, step.y, error) ||
            !integer(map, QStringLiteral("button"), 0, 2, step.button, error) ||
            !integer(map, QStringLiteral("delayMs"), 0, 60000, step.delayMs, error) ||
            !integer(map, QStringLiteral("intervalMs"), 10, 60000, step.intervalMs, error, true) ||
            !integer(map, QStringLiteral("clickCount"), 1, std::numeric_limits<int>::max(), step.clickCount, error, true) ||
            !integer(map, QStringLiteral("space"), 0, 1, space, error) ||
            !boolean(map, QStringLiteral("configured"), step.configured, error, true) ||
            !boolean(map, QStringLiteral("ownWindow"), step.ownWindow, error, true) ||
            !title(map, QStringLiteral("windowTitle"), windowTitle, error)) return false;
        step.space = static_cast<sequence::Space>(space);
        if (step.ownWindow && step.space != sequence::Space::Client) {
            fail(error, QStringLiteral("Window-bound actions require client coordinates"));
            return false;
        }
        step.windowTitle = windowTitle.toStdString();
        // System window IDs are never restored from disk.
        step.windowHandle = 0;
        step.processId = 0;
        result.steps.push_back(std::move(step));
    }
    auto check = result.steps;
    for (auto& step : check) step.configured = true; // Drafts may be saved, but not executed.
    const auto failure = sequence::validate({check, result.repeatCount, result.unlimited,
                                             result.passDelayMs});
    if (!failure.empty()) {
        fail(error, QString::fromStdString(failure));
        return false;
    }
    output = std::move(result);
    return true;
}
} // namespace clicknova::storage
