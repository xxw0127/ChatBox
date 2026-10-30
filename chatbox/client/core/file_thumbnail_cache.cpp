#include "core/file_thumbnail_cache.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>

namespace chatbox {
namespace client {

QString thumbnailCacheDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
           QStringLiteral("/preview/thumbs");
}

namespace {

// Bump this when the server-side thumbnail size or encoding changes so every
// client re-fetches instead of showing a stale low-resolution cache entry.
constexpr int kThumbnailCacheVersion = 2;

}  // namespace

QString thumbnailCachePath(quint64 file_id) {
    return QDir(thumbnailCacheDirectory())
        .filePath(QString::number(file_id) + QStringLiteral("-v%1.jpg")
                                           .arg(kThumbnailCacheVersion));
}

bool saveThumbnail(quint64 file_id, const QByteArray& data) {
    if (file_id == 0 || data.isEmpty()) {
        return false;
    }
    const QString directory = thumbnailCacheDirectory();
    if (!QDir().mkpath(directory)) {
        return false;
    }
    // Drop the pre-versioning cache entry once the replacement is available.
    QFile::remove(QDir(directory).filePath(QString::number(file_id) +
                                           QStringLiteral(".jpg")));
    QFile file(thumbnailCachePath(file_id));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(data) == data.size();
}

}  // namespace client
}  // namespace chatbox
