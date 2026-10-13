// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <optional>

namespace clicknova::storage {
// Scenario data is per-user, never inside the source tree or Git checkout.
enum class Kind { AutoClicker, Sequence, InputLab };
class ScenarioStore {
public:
    explicit ScenarioStore(QString root = {});
    QStringList names(Kind kind) const;
    bool save(Kind kind, const QString& name, const QVariantMap& values, QString* error = nullptr) const;
    std::optional<QVariantMap> load(Kind kind, const QString& name, QString* error = nullptr) const;
    bool remove(Kind kind, const QString& name) const;
    QString baseDirectory() const { return root_; }
    static bool validName(const QString& name);
private:
    QString filePath(Kind kind, const QString& name) const;
    QString root_;
};
}
