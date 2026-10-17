// SPDX-License-Identifier: Apache-2.0
#include "recording_checkpoint.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include "portable_storage.hpp"
#include <QUuid>
#include <limits>

namespace clicknova::gui::recording_checkpoint {
namespace {
constexpr int kMaximumSamples = 30000;
constexpr qint64 kMaximumBytes = 24 * 1024 * 1024;
QString root(const QString& provided) {
    return provided.isEmpty() ? directory() : provided;
}
QString iso(const QDateTime& date) { return date.toString(Qt::ISODateWithMs); }
QJsonObject encode(const recording::Sample& s) {
    QJsonObject obj;
    obj[QStringLiteral("k")] = static_cast<int>(s.kind);
    obj[QStringLiteral("ms")] = QString::number(s.milliseconds);
    obj[QStringLiteral("x")] = s.x;
    obj[QStringLiteral("y")] = s.y;
    obj[QStringLiteral("button")] = static_cast<int>(s.button);
    obj[QStringLiteral("axis")] = static_cast<int>(s.axis);
    obj[QStringLiteral("delta")] = s.delta;
    obj[QStringLiteral("key")] = s.virtualKey;
    if (!s.text.empty()) obj[QStringLiteral("text")] = QString::fromStdU16String(s.text);
    if (!s.window.title.empty()) obj[QStringLiteral("title")] = QString::fromStdU16String(s.window.title);
    if (!s.window.process.empty()) obj[QStringLiteral("process")] = QString::fromStdU16String(s.window.process);
    return obj;
}
bool decode(const QJsonValue& value, recording::Sample& s) {
    if (!value.isObject()) return false;
    const QJsonObject obj = value.toObject();
    const int kind = obj.value(QStringLiteral("k")).toInt(-1);
    const int button = obj.value(QStringLiteral("button")).toInt(-1);
    const int axis = obj.value(QStringLiteral("axis")).toInt(-1);
    bool goodMs = false;
    const auto ms = obj.value(QStringLiteral("ms")).toString().toULongLong(&goodMs);
    if (kind < static_cast<int>(recording::Kind::Move) || kind > static_cast<int>(recording::Kind::Text) ||
        button < 0 || button > 4 || axis < 0 || axis > 1 || !goodMs ||
        !obj.value(QStringLiteral("x")).isDouble() || !obj.value(QStringLiteral("y")).isDouble()) return false;
    s.kind = static_cast<recording::Kind>(kind);
    s.milliseconds = ms;
    s.x = obj.value(QStringLiteral("x")).toInt();
    s.y = obj.value(QStringLiteral("y")).toInt();
    s.button = static_cast<input::Button>(button);
    s.axis = static_cast<input::Axis>(axis);
    s.delta = obj.value(QStringLiteral("delta")).toInt();
    s.virtualKey = obj.value(QStringLiteral("key")).toInt();
    s.text = obj.value(QStringLiteral("text")).toString().toStdU16String();
    s.window.title = obj.value(QStringLiteral("title")).toString().toStdU16String();
    s.window.process = obj.value(QStringLiteral("process")).toString().toStdU16String();
    return true;
}
void report(QString* error, const QString& message) { if (error) *error = message; }
}
QString directory() {
    return clicknova::portable::filePath(QStringLiteral("recording-recovery"));
}
QString newPath(const QString& baseDirectory) {
    const QString name = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz"));
    return QDir(root(baseDirectory)).filePath(name + QLatin1Char('_') +
        QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + QStringLiteral(".cnrecord.json"));
}
bool save(const QString& path, const QDateTime& created,
          const std::vector<recording::Sample>& samples, std::uint64_t durationMs, QString* error) {
    if (samples.size() > kMaximumSamples || !created.isValid()) {
        report(error, QStringLiteral("Invalid recording checkpoint.")); return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        report(error, QStringLiteral("Cannot create recording recovery directory.")); return false;
    }
    QJsonArray events;
    for (const auto& s : samples) events.append(encode(s));
    QJsonObject object;
    object[QStringLiteral("version")] = 1;
    object[QStringLiteral("created")] = iso(created);
    object[QStringLiteral("updated")] = iso(QDateTime::currentDateTime());
    object[QStringLiteral("durationMs")] = QString::number(durationMs);
    object[QStringLiteral("events")] = events;
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > kMaximumBytes) {
        report(error, QStringLiteral("Recording exceeds recovery file size limit.")); return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { report(error, file.errorString()); return false; }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        report(error, file.errorString()); return false;
    }
    return true;
}
bool load(const QString& path, Snapshot& snapshot, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaximumBytes || file.size() < 1) {
        report(error, QStringLiteral("Cannot read recording checkpoint.")); return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        report(error, QStringLiteral("Invalid recording checkpoint JSON.")); return false;
    }
    const QJsonObject obj = doc.object();
    const QJsonArray events = obj.value(QStringLiteral("events")).toArray();
    bool goodMs = false;
    const std::uint64_t duration = obj.value(QStringLiteral("durationMs")).toString().toULongLong(&goodMs);
    const auto created = QDateTime::fromString(obj.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs);
    const auto updated = QDateTime::fromString(obj.value(QStringLiteral("updated")).toString(), Qt::ISODateWithMs);
    if (obj.value(QStringLiteral("version")).toInt() != 1 || !created.isValid() ||
        !updated.isValid() || !goodMs || events.isEmpty() || events.size() > kMaximumSamples) {
        report(error, QStringLiteral("Incomplete recording checkpoint.")); return false;
    }
    Snapshot result;
    result.path = path;
    result.created = created;
    result.updated = updated;
    result.durationMs = duration;
    result.samples.reserve(static_cast<std::size_t>(events.size()));
    for (const auto& item : events) {
        recording::Sample sample;
        if (!decode(item, sample) ||
            (!result.samples.empty() && result.samples.back().milliseconds > sample.milliseconds)) {
            report(error, QStringLiteral("Invalid event in recording checkpoint.")); return false;
        }
        result.samples.push_back(std::move(sample));
    }
    snapshot = std::move(result);
    return true;
}
std::vector<Snapshot> list(const QString& baseDirectory) {
    const QDir dir(root(baseDirectory));
    const QFileInfoList entries = dir.entryInfoList({QStringLiteral("*.cnrecord.json")}, QDir::Files, QDir::Time);
    std::vector<Snapshot> result;
    for (const auto& entry : entries) {
        Snapshot checkpoint;
        if (load(entry.absoluteFilePath(), checkpoint)) result.push_back(std::move(checkpoint));
        if (result.size() >= 50) break;
    }
    return result;
}
bool remove(const QString& path) { return path.isEmpty() || QFile::remove(path); }
}
