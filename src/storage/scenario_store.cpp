// SPDX-License-Identifier: Apache-2.0
#include "scenario_store.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <utility>
#include <QByteArray>

namespace clicknova::storage {
namespace {
QString category(Kind kind) {
    switch (kind) {
    case Kind::AutoClicker: return QStringLiteral("auto_clicker");
    case Kind::Sequence: return QStringLiteral("sequences");
    case Kind::InputLab: return QStringLiteral("input_lab");
    }
    return QStringLiteral("invalid");
}
QString encodedName(const QString& name) {
    return QString::fromLatin1(name.toUtf8().toBase64(QByteArray::Base64UrlEncoding |
        QByteArray::OmitTrailingEquals));
}
void fail(QString* error, const QString& message) { if (error) *error = message; }
}
ScenarioStore::ScenarioStore(QString root)
    : root_(root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
              QStringLiteral("/scenarios") : std::move(root)) {}

bool ScenarioStore::validName(const QString& name) {
    static const QRegularExpression re(QStringLiteral("^[\\p{L}\\p{N}][\\p{L}\\p{N} _-]{0,39}$"));
    return re.match(name).hasMatch();
}
QString ScenarioStore::filePath(Kind kind, const QString& name) const {
    return QDir(root_).filePath(category(kind) + QLatin1Char('/') + encodedName(name) +
                                QStringLiteral(".json"));
}
QStringList ScenarioStore::names(Kind kind) const {
    const QDir directory(QDir(root_).filePath(category(kind)));
    QStringList output;
    for (const auto& filename : directory.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        const QFileInfo info(directory.filePath(filename));
        if (info.size() > 1024 * 1024) continue;
        QFile file(info.filePath());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) continue;
        const auto obj = doc.object();
        const QString name = obj.value(QStringLiteral("name")).toString();
        if (obj.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
            obj.value(QStringLiteral("kind")).toString() != category(kind) ||
            !validName(name) || filePath(kind, name) != info.filePath() ||
            !obj.value(QStringLiteral("settings")).isObject()) continue;
        output.append(name);
    }
    output.sort(Qt::CaseInsensitive);
    return output;
}
bool ScenarioStore::save(Kind kind, const QString& name, const QVariantMap& values, QString* error) const {
    if (!validName(name)) { fail(error, QStringLiteral("Invalid scenario name")); return false; }
    const QString path = filePath(kind, name);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        fail(error, QStringLiteral("Cannot create scenario directory")); return false;
    }
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), 1);
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("kind"), category(kind));
    object.insert(QStringLiteral("settings"), QJsonObject::fromVariantMap(values));
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (bytes.size() > 1024 * 1024) { fail(error, QStringLiteral("Scenario too large")); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        fail(error, QStringLiteral("Cannot save scenario")); return false;
    }
    return true;
}
std::optional<QVariantMap> ScenarioStore::load(Kind kind, const QString& name, QString* error) const {
    if (!validName(name)) { fail(error, QStringLiteral("Invalid scenario name")); return {} ; }
    QFile file(filePath(kind, name));
    if (file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly)) {
        fail(error, QStringLiteral("Cannot read scenario")); return {};
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        fail(error, QStringLiteral("Invalid scenario JSON")); return {};
    }
    const auto obj = doc.object();
    if (obj.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
        obj.value(QStringLiteral("kind")).toString() != category(kind) ||
        obj.value(QStringLiteral("name")).toString() != name ||
        !obj.value(QStringLiteral("settings")).isObject()) {
        fail(error, QStringLiteral("Unsupported scenario format")); return {};
    }
    return obj.value(QStringLiteral("settings")).toObject().toVariantMap();
}
bool ScenarioStore::remove(Kind kind, const QString& name) const {
    return validName(name) && QFile::remove(filePath(kind, name));
}
} // namespace clicknova::storage
