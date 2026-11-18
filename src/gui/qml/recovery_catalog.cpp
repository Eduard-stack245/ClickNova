// SPDX-License-Identifier: MIT
#include "recovery_catalog.hpp"
#include "../recording_checkpoint.hpp"
#include <QFileInfo>
#include <QDateTime>
#include <algorithm>

namespace clicknova::gui::recovery_catalog {
QVector<Session> sessions(const QString& directory) {
    QVector<Session> result;
    const auto snapshots = recording_checkpoint::list(directory);
    result.reserve(static_cast<qsizetype>(snapshots.size()));
    for (const auto& snapshot : snapshots) {
        result.push_back({
            QFileInfo(snapshot.path).fileName(),
            snapshot.created.toLocalTime().toString(QStringLiteral("dd.MM.yyyy  HH:mm")),
            snapshot.updated.toLocalTime().toString(QStringLiteral("dd.MM.yyyy  HH:mm")),
            static_cast<int>(snapshot.samples.size()),
            static_cast<double>(snapshot.durationMs) / 1000.0
        });
    }
    return result;
}

QVector<Event> preview(const QString& fileName, const QString& directory, int limit) {
    if (limit <= 0) return {};
    recording_checkpoint::Snapshot snapshot;
    if (!recording_checkpoint::loadNamed(fileName, snapshot, directory)) return {};
    QVector<Event> result;
    const auto count = static_cast<std::size_t>(std::clamp(limit, 0, 100));
    const auto start = snapshot.samples.size() > count ? snapshot.samples.size() - count : 0;
    result.reserve(static_cast<qsizetype>(snapshot.samples.size() - start));
    for (std::size_t i = start; i < snapshot.samples.size(); ++i) {
        const auto& sample = snapshot.samples[i];
        QString key;
        switch (sample.kind) {
        case recording::Kind::Move: key = QStringLiteral("recoveryMove"); break;
        case recording::Kind::MouseDown: key = QStringLiteral("recoveryMouseDown"); break;
        case recording::Kind::MouseUp: key = QStringLiteral("recoveryMouseUp"); break;
        case recording::Kind::Wheel: key = QStringLiteral("recoveryWheel"); break;
        case recording::Kind::KeyDown: key = QStringLiteral("recoveryKeyDown"); break;
        case recording::Kind::KeyUp: key = QStringLiteral("recoveryKeyUp"); break;
        case recording::Kind::Text: key = QStringLiteral("recoveryText"); break;
        }
        result.push_back({key, QString::number(static_cast<double>(sample.milliseconds) / 1000.0, 'f', 2)});
    }
    return result;
}
} // namespace clicknova::gui::recovery_catalog
