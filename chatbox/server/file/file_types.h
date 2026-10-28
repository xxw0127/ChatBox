#ifndef CHATBOX_SERVER_FILE_FILE_TYPES_H
#define CHATBOX_SERVER_FILE_FILE_TYPES_H

#include <cstdint>
#include <string>

#include "common/file_types.h"

namespace chatbox {

struct FileAccess {
    uint64_t file_id = 0;
    uint64_t uploader_id = 0;
    FileKind kind = FileKind::kRoom;
    uint64_t room_id = 0;
    uint64_t private_sender_id = 0;
    uint64_t private_receiver_id = 0;
    uint64_t message_id = 0;
    std::string name;
    uint64_t size = 0;
    std::string mime;
    std::string sha256;
    std::string storage_path;
    std::string thumb_path;
    int status = 0;
};

struct UploadState {
    std::string upload_id;
    uint64_t uploader_id = 0;
    FileKind kind = FileKind::kRoom;
    uint64_t target_id = 0;
    std::string name;
    uint64_t size = 0;
    std::string mime;
    std::string sha256;
    std::string temp_path;
    uint64_t offset = 0;
    int status = 0;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_FILE_FILE_TYPES_H
