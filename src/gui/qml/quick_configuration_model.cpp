// SPDX-License-Identifier: MIT
#include "quick_configuration_model.hpp"
#include "clicknova/start_delay.hpp"
#include <QMetaType>
#include <QTime>
#include <cmath>
#include <limits>
#include <string>

namespace clicknova::gui::qml {
namespace {
bool exactNumber(const QVariant& source, std::int64_t& out) {
    const int type = source.metaType().id();
    if (type == QMetaType::QString) {
        const QString text = source.toString();
        if (text.isEmpty() || text.size() > 20) return false;
        int start = (text.at(0) == QLatin1Char('-') || text.at(0) == QLatin1Char('+')) ? 1 : 0;
        if (start == text.size()) return false;
        for (int i = start; i < text.size(); ++i)
            if (text.at(i) < QLatin1Char('0') || text.at(i) > QLatin1Char('9')) return false;
        bool ok = false;
        out = text.toLongLong(&ok, 10);
        return ok;
    }
    if (type == QMetaType::Double || type == QMetaType::Float) {
        bool ok = false;
        const double n = source.toDouble(&ok);
        if (!ok || !std::isfinite(n) || std::floor(n) != n ||
            std::abs(n) > 9007199254740991.0) return false;
        out = static_cast<std::int64_t>(n);
        return true;
    }
    if (type == QMetaType::Int || type == QMetaType::UInt ||
        type == QMetaType::LongLong || type == QMetaType::ULongLong) {
        bool ok = false;
        out = source.toLongLong(&ok);
        return ok;
    }
    return false;
}
QString clockText(const quick::ConfigDraft& d) {
    return QTime(d.clockHour, d.clockMinute, d.clockSecond).toString(QStringLiteral("HH:mm:ss"));
}
} // namespace

QuickConfigurationModel::QuickConfigurationModel(QObject* parent) : QObject(parent) {}

bool QuickConfigurationModel::apply(quick::ConfigDraft& d, const QString& name,
                                    const QVariant& value, bool allowInternal) {
    if (!value.isValid()) return false;
    const QByteArray bytes = name.toLatin1();
    if (QString::fromLatin1(bytes) != name) return false;
    const auto field = quick::draftField(std::string_view(bytes.constData(),
                                                        static_cast<std::size_t>(bytes.size())));
    if (field == quick::DraftField::Invalid ||
        (!allowInternal && field == quick::DraftField::DestinationConfigured)) return false;
    if (quick::draftIntField(field)) {
        std::int64_t integer = 0;
        return exactNumber(value, integer) && quick::assignDraftInt(d, field, integer);
    }
    if (quick::draftBoolField(field)) {
        if (value.metaType().id() != QMetaType::Bool) return false;
        return quick::assignDraftBool(d, field, value.toBool());
    }
    if (field == quick::DraftField::Clock) {
        if (value.metaType().id() != QMetaType::QString) return false;
        const QByteArray text = value.toString().toLatin1();
        if (text.size() != 8) return false;
        return quick::assignDraftClock(d,
            std::string_view(text.constData(), static_cast<std::size_t>(text.size())));
    }
    return false;
}

bool QuickConfigurationModel::accepts(const QString& name, const QVariant& value) const {
    auto candidate = draft_;
    return apply(candidate, name, value);
}
bool QuickConfigurationModel::setField(const QString& name, const QVariant& value) {
    auto candidate = draft_;
    if (!apply(candidate, name, value)) return false;
    if (!(candidate == draft_)) { draft_ = candidate; emit valuesChanged(); }
    return true;
}
bool QuickConfigurationModel::importNative(const QVariantMap& values) {
    auto candidate = draft_;
    // Apply known fields atomically. Never accept arbitrary QML-provided handles.
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (it.key() == QStringLiteral("destinationConfigured")) continue;
        if (!apply(candidate, it.key(), it.value(), true)) return false;
    }
    if (values.contains(QStringLiteral("destinationConfigured")) &&
        !apply(candidate, QStringLiteral("destinationConfigured"),
               values.value(QStringLiteral("destinationConfigured")), true)) return false;
    if (!(candidate == draft_)) { draft_ = candidate; emit valuesChanged(); }
    return true;
}
QVariantMap QuickConfigurationModel::values() const {
    const auto& d = draft_;
    return {
        {QStringLiteral("quickActionMode"), d.action},
        {QStringLiteral("quickMouseButton"), d.button},
        {QStringLiteral("quickClickInterval"), d.intervalMs},
        {QStringLiteral("quickClickCount"), d.count},
        {QStringLiteral("quickUnlimited"), d.unlimited},
        {QStringLiteral("quickHoldDuration"), d.holdMs},
        {QStringLiteral("quickStartMode"), d.startMode},
        {QStringLiteral("quickStartDelayValue"), d.delayValue},
        {QStringLiteral("quickStartDelayUnit"), d.delayUnit},
        {QStringLiteral("quickStartAtTime"), clockText(d)},
        {QStringLiteral("quickKeepAwake"), d.keepAwake},
        {QStringLiteral("positionMode"), d.positionMode},
        {QStringLiteral("coordinateX"), d.x},
        {QStringLiteral("coordinateY"), d.y},
        {QStringLiteral("quickDestinationX"), d.destinationX},
        {QStringLiteral("quickDestinationY"), d.destinationY},
        {QStringLiteral("quickMoveDuration"), d.movementMs},
        {QStringLiteral("targetMode"), d.targetMode},
        {QStringLiteral("destinationConfigured"), d.destinationConfigured}
    };
}
QVariantMap QuickConfigurationModel::engineOptions() const {
    const auto& d = draft_;
    const auto delay = timing::startDelayMs(static_cast<std::uint64_t>(d.delayValue), d.delayUnit);
    if (!delay) return {};
    return {
        {QStringLiteral("action"), d.action},
        {QStringLiteral("button"), d.button},
        {QStringLiteral("intervalMs"), d.intervalMs},
        {QStringLiteral("holdMs"), d.holdMs},
        {QStringLiteral("count"), d.action == 3 ? 1 : d.count},
        {QStringLiteral("unlimited"), d.action == 3 ? false : d.unlimited},
        {QStringLiteral("fixedPosition"), d.positionMode == 1},
        {QStringLiteral("x"), d.x},
        {QStringLiteral("y"), d.y},
        {QStringLiteral("targetWindow"), d.targetMode == 1},
        {QStringLiteral("destinationConfigured"), d.destinationConfigured},
        {QStringLiteral("destinationX"), d.destinationX},
        {QStringLiteral("destinationY"), d.destinationY},
        {QStringLiteral("movementMs"), d.movementMs},
        {QStringLiteral("delayMs"), static_cast<qlonglong>(*delay)},
        {QStringLiteral("useClockStart"), d.startMode == 1},
        {QStringLiteral("clockHour"), d.clockHour},
        {QStringLiteral("clockMinute"), d.clockMinute},
        {QStringLiteral("clockSecond"), d.clockSecond},
        {QStringLiteral("keepAwake"), d.keepAwake}
    };
}
} // namespace clicknova::gui::qml
