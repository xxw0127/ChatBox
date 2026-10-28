#ifndef CHATBOX_CLIENT_CORE_FILE_TYPES_H
#define CHATBOX_CLIENT_CORE_FILE_TYPES_H

#include <QString>

#include "common/file_types.h"
#include "core/chat_message.h"

namespace chatbox {
namespace client {

struct FileEntryView {
    quint64 file_id = 0;
    quint64 uploader_id = 0;
    QString uploader_name;
    quint64 message_id = 0;
    QString name;
    quint64 size = 0;
    QString mime;
    QString sha256;
    bool thumb_available = false;
    quint32 width = 0;
    quint32 height = 0;
    quint32 duration_ms = 0;
    QString created_at;
    bool can_delete = false;
};

FileMeta fileMetaFromWire(const chatbox::FileMeta& wire);
FileEntryView fileEntryFromWire(const chatbox::FileEntry& wire);
QString formatFileSize(quint64 bytes);

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_FILE_TYPES_H
