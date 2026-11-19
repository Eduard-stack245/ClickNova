// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
#include <QVector>

namespace clicknova::gui::recovery_catalog {
// Typed, view-independent recovery metadata. Never exposes captured text,
// virtual key values, window names, or script content to a QML preview.
struct Session {
    QString name;
    QString created;
    QString updated;
    int eventCount{0};
    double durationSeconds{0.0};
};
struct Event {
    QString kindKey;
    QString seconds;
};

QVector<Session> sessions(const QString& directory = {});
QVector<Event> preview(const QString& fileName, const QString& directory = {}, int limit = 32);
} // namespace clicknova::gui::recovery_catalog
