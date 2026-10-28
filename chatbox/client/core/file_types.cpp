#include "core/file_types.h"

#include <QStringList>

namespace chatbox {
namespace client {

FileMeta fileMetaFromWire(const chatbox::FileMeta& wire) {
    FileMeta meta;
    meta.file_id = wire.file_id;
    meta.name = QString::fromUtf8(wire.name.data(), static_cast<int>(wire.name.size()));
    meta.size = wire.size;
    meta.mime = QString::fromUtf8(wire.mime.data(), static_cast<int>(wire.mime.size()));
    meta.sha256 =
        QString::fromUtf8(wire.sha256.data(), static_cast<int>(wire.sha256.size()));
    meta.thumb_available = wire.thumb_available;
    meta.width = wire.width;
    meta.height = wire.height;
    meta.duration_ms = wire.duration_ms;
    return meta;
}

FileEntryView fileEntryFromWire(const chatbox::FileEntry& wire) {
    FileEntryView entry;
    entry.file_id = wire.file_id;
    entry.uploader_id = wire.uploader_id;
    entry.uploader_name = QString::fromUtf8(
        wire.uploader_name.data(), static_cast<int>(wire.uploader_name.size()));
    entry.message_id = wire.message_id;
    entry.name = QString::fromUtf8(wire.name.data(),
                                   static_cast<int>(wire.name.size()));
    entry.size = wire.size;
    entry.mime = QString::fromUtf8(wire.mime.data(),
                                   static_cast<int>(wire.mime.size()));
    entry.sha256 = QString::fromUtf8(wire.sha256.data(),
                                     static_cast<int>(wire.sha256.size()));
    entry.thumb_available = wire.thumb_available;
    entry.width = wire.width;
    entry.height = wire.height;
    entry.duration_ms = wire.duration_ms;
    entry.created_at = QString::fromUtf8(
        wire.created_at.data(), static_cast<int>(wire.created_at.size()));
    entry.can_delete = wire.can_delete;
    return entry;
}

QString formatFileSize(quint64 bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return QString("%1 B").arg(bytes);
    }
    return QString("%1 %2").arg(value, 0, 'f', value >= 10 ? 0 : 1).arg(units[unit]);
}

}  // namespace client
}  // namespace chatbox
