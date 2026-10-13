// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "clicknova/input_recording.hpp"
#include <QString>
#include <QDateTime>
#include <cstdint>
#include <vector>

namespace clicknova::gui::recording_checkpoint {
struct Snapshot {
    QString path;
    QDateTime created;
    QDateTime updated;
    std::uint64_t durationMs{};
    std::vector<recording::Sample> samples;
};
// A local, private working draft of an explicitly started recording.
// Never add this directory to Git or write it to the source tree.
QString directory();
QString newPath(const QString& baseDirectory = {});
bool save(const QString& path, const QDateTime& created,
          const std::vector<recording::Sample>& samples,
          std::uint64_t durationMs, QString* error = nullptr);
bool load(const QString& path, Snapshot& snapshot, QString* error = nullptr);
std::vector<Snapshot> list(const QString& baseDirectory = {});
bool remove(const QString& path);
}
