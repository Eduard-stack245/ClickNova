// SPDX-License-Identifier: MIT
#include "../src/gui/recording_checkpoint.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QDebug>
#include <vector>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid()) { qCritical() << "Temporary directory failed"; return 1; }
    namespace checkpoint = clicknova::gui::recording_checkpoint;

    clicknova::recording::Sample event;
    event.kind = clicknova::recording::Kind::Move;
    event.milliseconds = 125;
    event.x = 19;
    event.y = 28;
    const std::vector<clicknova::recording::Sample> samples{event};
    const QString path = checkpoint::newPath(tmp.path());
    const QString name = QFileInfo(path).fileName();
    if (!checkpoint::save(path, QDateTime::currentDateTime(), samples, 125)) return 2;

    checkpoint::Snapshot loaded;
    if (!checkpoint::loadNamed(name, loaded, tmp.path())) return 3;
    if (loaded.samples.size() != 1) return 4;
    if (loaded.samples[0].x != 19 || loaded.samples[0].y != 28) return 5;
    if (loaded.durationMs != 125) return 6;
    if (checkpoint::loadNamed(QStringLiteral("../") + name, loaded, tmp.path())) return 7;
    if (checkpoint::loadNamed(QStringLiteral("..\\") + name, loaded, tmp.path())) return 8;
    if (checkpoint::loadNamed(QStringLiteral("C:\\Windows\\evil.cnrecord.json"), loaded, tmp.path())) return 9;
    if (checkpoint::loadNamed(QStringLiteral("not-a-checkpoint.cnrecord.json"), loaded, tmp.path())) return 10;

    const auto listed = checkpoint::list(tmp.path());
    if (listed.size() != 1) return 11;

    // A corrupt checkpoint is never listed as a valid recovery candidate.
    const QString corruptPath = checkpoint::newPath(tmp.path());
    QFile corrupt(corruptPath);
    if (!corrupt.open(QIODevice::WriteOnly)) return 12;
    if (corrupt.write("{invalid-json") <= 0) return 13;
    corrupt.close();
    if (checkpoint::loadNamed(QFileInfo(corruptPath).fileName(), loaded, tmp.path())) return 14;
    if (checkpoint::list(tmp.path()).size() != 1) return 15;

    if (!checkpoint::remove(path)) return 16;
    if (checkpoint::loadNamed(name, loaded, tmp.path())) return 17;
    if (!checkpoint::list(tmp.path()).empty()) return 18;
    return 0;
}
