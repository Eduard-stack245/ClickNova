// SPDX-License-Identifier: MIT
#pragma once

#include "clicknova/quick_config_draft.hpp"
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace clicknova::gui::qml {
// QML-facing typed draft and input validator. No widget lookup or injection.
// The native Widgets view is temporarily mirrored only for old profile formats.
class QuickConfigurationModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
public:
    explicit QuickConfigurationModel(QObject* parent = nullptr);
    QVariantMap values() const;
    const quick::ConfigDraft& draft() const noexcept { return draft_; }
    bool accepts(const QString& name, const QVariant& value) const;
    bool setField(const QString& name, const QVariant& value);
    bool importNative(const QVariantMap& values); // atomic profile/scenario import
    QVariantMap engineOptions() const; // only typed fields; never an HWND/PID
signals:
    void valuesChanged();
private:
    static bool apply(quick::ConfigDraft& draft, const QString& name,
                      const QVariant& value, bool allowInternal = false);
    quick::ConfigDraft draft_;
};
} // namespace clicknova::gui::qml
