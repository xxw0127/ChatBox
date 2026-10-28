#ifndef CHATBOX_CLIENT_CORE_FILE_THUMBNAIL_CACHE_H
#define CHATBOX_CLIENT_CORE_FILE_THUMBNAIL_CACHE_H

#include <QByteArray>
#include <QString>
#include <QtGlobal>

namespace chatbox {
namespace client {

QString thumbnailCacheDirectory();
QString thumbnailCachePath(quint64 file_id);
bool saveThumbnail(quint64 file_id, const QByteArray& data);

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_FILE_THUMBNAIL_CACHE_H
