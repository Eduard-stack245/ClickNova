// SPDX-License-Identifier: MIT
#include "../src/gui/qml/recovery_catalog.hpp"
#include "../src/gui/recording_checkpoint.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QTemporaryDir>
#include <vector>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    namespace checkpoint = clicknova::gui::recording_checkpoint;
    namespace catalog = clicknova::gui::recovery_catalog;
    std::vector<clicknova::recording::Sample> events;
    for (int i = 0; i < 37; ++i) {
        clicknova::recording::Sample sample;
        sample.kind = i % 2 == 0 ? clicknova::recording::Kind::KeyDown : clicknova::recording::Kind::Text;
        sample.milliseconds = static_cast<std::uint64_t>(i) * 50;
        sample.text = u"PRIVATE_INPUT_NEVER_EXPOSE";
        sample.virtualKey = 77;
        sample.window.title = u"PRIVATE_WINDOW";
        events.push_back(sample);
    }
    const QString path = checkpoint::newPath(directory.path());
    if (!checkpoint::save(path, QDateTime::currentDateTime(), events, 1850)) return 2;
    const auto sessions = catalog::sessions(directory.path());
    if (sessions.size() != 1 || sessions[0].eventCount != 37) return 3;
    const auto items = catalog::preview(QFileInfo(path).fileName(), directory.path());
    if (items.size() != 32) return 4;
    for (const auto& item : items) {
        if (item.kindKey != QStringLiteral("recoveryKeyDown") &&
            item.kindKey != QStringLiteral("recoveryText")) return 5;
        if (item.kindKey.contains(QStringLiteral("PRIVATE")) ||
            item.seconds.contains(QStringLiteral("PRIVATE"))) return 6;
    }
    if (!catalog::preview(QStringLiteral("../private.cnrecord.json"), directory.path()).empty()) return 7;
    if (!catalog::preview(QFileInfo(path).fileName(), directory.path(), 0).empty()) return 8;
    if (catalog::preview(QFileInfo(path).fileName(), directory.path(), 500).size() != 37) return 9;
    return 0;
}
