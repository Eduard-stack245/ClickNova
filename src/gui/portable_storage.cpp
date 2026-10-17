// SPDX-License-Identifier: Apache-2.0
#include "portable_storage.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QFile>

namespace clicknova::portable {
namespace {
QString storageRoot;
bool portable = false;
QString preferredRoot() {
#ifdef __linux__
    // AppImage executables run from a transient mounted/extracted AppDir.
    // Store persistent data next to the actual AppImage, not in /tmp.
    const QString image = QFile::decodeName(qgetenv("APPIMAGE"));
    if (!image.isEmpty() && QFileInfo(image).isFile())
        return QDir(QFileInfo(image).absolutePath()).filePath(QStringLiteral("ClickNova-data"));
#endif
    // Dev build: build-vs.../Release/ClickNova.exe is inside a disposable
    // CMake tree. Keep user data in the SOURCE folder, not the build cache.
    QDir exe(QCoreApplication::applicationDirPath());
    QDir build(exe);
    if (build.cdUp() && QFileInfo::exists(build.filePath(QStringLiteral("CMakeCache.txt"))) &&
        build.cdUp() && QFileInfo::exists(build.filePath(QStringLiteral("CMakeLists.txt"))))
        return build.filePath(QStringLiteral("data"));
    return exe.filePath(QStringLiteral("data"));
}
void copyPreviousFiles(const QString& source, const QString& destination) {
    if (!QDir(source).exists() || QDir::cleanPath(source) == QDir::cleanPath(destination)) return;
    QDir().mkpath(destination);
    const QDir from(source);
    for (const QFileInfo& item : from.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        const QString to = QDir(destination).filePath(item.fileName());
        if (item.isDir()) copyPreviousFiles(item.absoluteFilePath(), to);
        else if (item.isFile() && item.size() >= 0 && item.size() <= 24 * 1024 * 1024 &&
                 !QFileInfo::exists(to)) QFile::copy(item.absoluteFilePath(), to);
    }
}
void migrateLegacyData() {
    QSettings settings(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
    if (settings.value(QStringLiteral("portableMigration/dataV1"), false).toBool()) return;
    const QString oldData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString oldLocal = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    copyPreviousFiles(QDir(oldData).filePath(QStringLiteral("scenarios")),
                      QDir(storageRoot).filePath(QStringLiteral("scenarios")));
    const QString oldDraft = QDir(oldData).filePath(QStringLiteral("clicknova-steps-autosave.json"));
    const QString newDraft = QDir(storageRoot).filePath(QStringLiteral("clicknova-steps-autosave.json"));
    if (QFileInfo::exists(oldDraft) && !QFileInfo::exists(newDraft)) QFile::copy(oldDraft, newDraft);
    copyPreviousFiles(QDir(oldLocal).filePath(QStringLiteral("activity-logs")),
                      QDir(storageRoot).filePath(QStringLiteral("activity-logs")));
    copyPreviousFiles(QDir(oldLocal).filePath(QStringLiteral("recording-recovery")),
                      QDir(storageRoot).filePath(QStringLiteral("recording-recovery")));
    settings.setValue(QStringLiteral("portableMigration/dataV1"), true);
    settings.sync();
}
}

bool initialize(QString* warning) {
    // The .exe/AppImage may be launched from ANY disk or directory; resolve
    // relative to the executable, never to the process working directory.
    const QString root = preferredRoot();
    QDir().mkpath(root);
    QTemporaryFile probe(QDir(root).filePath(QStringLiteral(".write-test-XXXXXX")));
    portable = probe.open();
    probe.close();
    if (portable) {
        storageRoot = QDir(root).absolutePath();
        const QString config = QDir(storageRoot).filePath(QStringLiteral("config"));
        QDir().mkpath(config);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config);
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, config);
#ifdef _WIN32
        // One-time migration of legacy per-user Windows registry preferences;
        // never overwrite the settings carried with an existing portable copy.
        QSettings current(QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
        if (current.allKeys().isEmpty()) {
            QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                             QStringLiteral("ClickNova"), QStringLiteral("ClickNova"));
            for (const QString& key : legacy.allKeys()) current.setValue(key, legacy.value(key));
            current.sync();
        }
#endif
        migrateLegacyData();
    } else {
        storageRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (warning) *warning = QStringLiteral(
            "Папка приложения недоступна для записи. Данные будут храниться в профиле пользователя. "
            "Для полностью переносимого режима переместите ClickNova в доступную для записи папку.");
    }
    return portable;
}
QString dataRoot() {
    if (!storageRoot.isEmpty()) return storageRoot;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}
QString filePath(const QString& relativePath) {
    // Internal file paths only: callers must never supply absolute or parent paths.
    if (QDir::isAbsolutePath(relativePath) || relativePath.split('/').contains(QStringLiteral("..")) ||
        relativePath.split('\\').contains(QStringLiteral(".."))) return {};
    return QDir(dataRoot()).filePath(relativePath);
}
bool isPortable() { return portable; }
}
