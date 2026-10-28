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

QString thumbnailCachePath(quint64 file_id) {
    return QDir(thumbnailCacheDirectory())
        .filePath(QString::number(file_id) + QStringLiteral(".jpg"));
}

bool saveThumbnail(quint64 file_id, const QByteArray& data) {
    if (file_id == 0 || data.isEmpty()) {
        return false;
    }
    const QString directory = thumbnailCacheDirectory();
    if (!QDir().mkpath(directory)) {
        return false;
    }
    QFile file(thumbnailCachePath(file_id));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(data) == data.size();
}

}  // namespace client
}  // namespace chatbox
