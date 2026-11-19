// SPDX-License-Identifier: MIT
#pragma once

// The only read path for content-addressed screenshots in ClickNova's
// Widgets editor and review. Images are local only; no network or paths from
// untrusted recording metadata are ever used as filesystem paths.
#include "portable_storage.hpp"
#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPixmap>
#include <QRegularExpression>
#include <QSize>
#include <QString>
#include <algorithm>
#include <string>

namespace clicknova::gui::step_images {

inline bool validId(const QString& id) {
    static const QRegularExpression rule(QStringLiteral("^[0-9a-f]{64}$"));
    return rule.match(id).hasMatch();
}

inline QString path(const std::string& id) {
    const QString name = QString::fromStdString(id);
    if (!validId(name)) return {};
    return clicknova::portable::filePath(QStringLiteral("step-images/") +
                                          name + QStringLiteral(".png"));
}

// Required dimensions are checked BEFORE image decoding; malicious PNGs may
// announce huge dimensions even though the compressed file looks small.
inline bool acceptable(const QFileInfo& file) {
    return file.isFile() && !file.isSymLink() && file.size() > 0 &&
           file.size() <= 32LL * 1024 * 1024;
}

// filePath is an explicit parameter only to support isolated regression tests.
// Production callers derive it exclusively from the validated SHA-256 ID.
inline QImage loadVerifiedFile(const QString& filePath, const QString& expectedId,
                               const QSize& requested = {}) {
    if (!validId(expectedId)) return {};
    const QFileInfo info(filePath);
    if (!acceptable(info)) return {};
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray png = file.read(32 * 1024 * 1024 + 1);
    if (png.isEmpty() || png.size() > 32 * 1024 * 1024 ||
        QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex() !=
            expectedId.toLatin1()) return {};
    QBuffer buffer;
    buffer.setData(png);
    if (!buffer.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    if (reader.format().toLower() != QByteArrayLiteral("png")) return {};
    const QSize full = reader.size();
    if (!full.isValid() || full.width() > 12000 || full.height() > 12000 ||
        qint64(full.width()) * full.height() > 50000000) return {};
    if (requested.isValid() && requested.width() > 0 && requested.height() > 0)
        reader.setScaledSize(full.scaled(requested, Qt::KeepAspectRatio));
    QImage result = reader.read();
    // Some image plugins may not honor setScaledSize. Limit rendered thumbnail
    // dimensions explicitly without ever modifying the original on disk.
    if (!result.isNull() && requested.isValid() &&
        (result.width() > requested.width() || result.height() > requested.height()))
        result = result.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return result;
}

inline QImage load(const std::string& id, const QSize& requested = {}) {
    const QString filename = path(id);
    if (filename.isEmpty()) return {};
    return loadVerifiedFile(filename, QString::fromStdString(id), requested);
}

// Maximum of 8 MiB of small thumbnails. Evicted automatically by QCache.
// Cache entries contain only in-memory pixmaps; original PNGs remain on disk.
// The file size and modification time are part of the key, so replacing an
// image on disk does not keep an outdated preview indefinitely.
inline QPixmap thumbnail(const std::string& id, const QSize& size = QSize(96, 68)) {
    if (!size.isValid() || size.width() < 8 || size.height() < 8 ||
        size.width() > 256 || size.height() > 256) return {};
    const QString filename = path(id);
    if (filename.isEmpty()) return {};
    const QFileInfo info(filename);
    if (!acceptable(info)) return {};
    static QCache<QString, QPixmap> cache(8 * 1024); // cost unit: KiB
    const QString key = QString::fromStdString(id) + QLatin1Char('/') +
        QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height()) +
        QLatin1Char('/') + QString::number(info.size()) + QLatin1Char('/') +
        QString::number(info.lastModified().toMSecsSinceEpoch());
    if (const QPixmap* hit = cache.object(key)) return *hit;
    const QImage decoded = loadVerifiedFile(filename, QString::fromStdString(id), size);
    if (decoded.isNull()) return {};
    const QPixmap result = QPixmap::fromImage(decoded);
    if (!result.isNull()) {
        const int costKiB = std::max(1, (result.width() * result.height() * 4 + 1023) / 1024);
        cache.insert(key, new QPixmap(result), costKiB);
    }
    return result;
}

} // namespace clicknova::gui::step_images
