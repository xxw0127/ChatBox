#ifndef CHATBOX_SERVER_FILE_FILE_TYPES_H
#define CHATBOX_SERVER_FILE_FILE_TYPES_H

#include <cstdint>
#include <string>

namespace chatbox {

constexpr uint64_t kMaxFileSize = 2ull * 1024 * 1024 * 1024;
constexpr uint64_t kMaxGroupFileBytes = 20ull * 1024 * 1024 * 1024;
constexpr uint64_t kMinFreeDiskBytes = 5ull * 1024 * 1024 * 1024;

enum class FileKind : uint8_t {
    kRoom = 1,
    kPrivate = 2,
};

enum class FileStatus : uint8_t {
    kReady = 0,
    kDeleted = 1,
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

struct FileMeta {
    uint64_t file_id = 0;
    std::string name;
    uint64_t size = 0;
    std::string mime;
    std::string sha256;
    bool thumb_available = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t duration_ms = 0;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_FILE_FILE_TYPES_H
