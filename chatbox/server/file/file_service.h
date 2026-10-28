#ifndef CHATBOX_SERVER_FILE_FILE_SERVICE_H
#define CHATBOX_SERVER_FILE_FILE_SERVICE_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/protocol.h"
#include "server/db/mysql_connection.h"
#include "server/file/file_thumbnailer.h"
#include "server/net/connection.h"
#include "server/room/room_manager.h"

namespace chatbox {

struct FileNotice {
    uint64_t user_id = 0;
    std::string frame;
};

struct FileResult {
    std::vector<Frame> responses;
    bool broadcast = false;
    std::string broadcast_frame;
    std::vector<RoomMember> targets;
    std::vector<FileNotice> notices;
};

class FileService {
public:
    FileService(MysqlConnection& database, RoomManager& rooms,
                std::string files_dir,
                std::string ffmpeg_path = "/usr/bin/ffmpeg",
                std::string ffprobe_path = "/usr/bin/ffprobe");
    ~FileService();

    FileResult handle(const Frame& request, const RoomMember& caller,
                      const std::shared_ptr<Connection>& connection);

private:
    struct UploadCache {
        UploadState state;
        int fd = -1;
        uint64_t last_persisted_offset = 0;
    };

    FileResult handleAuth(const Frame& request, const RoomMember& caller,
                          const std::shared_ptr<Connection>& connection);
    FileResult handleUploadInit(const Frame& request, const RoomMember& caller);
    FileResult handleUploadChunk(const Frame& request, const RoomMember& caller);
    FileResult handleUploadStatus(const Frame& request, const RoomMember& caller);
    FileResult handleUploadCommit(const Frame& request, const RoomMember& caller);
    FileResult handleUploadCancel(const Frame& request, const RoomMember& caller);
    FileResult handleOpen(const Frame& request, const RoomMember& caller);
    FileResult handleRead(const Frame& request, const RoomMember& caller);
    FileResult handleReadAck(const Frame& request, const RoomMember& caller);
    FileResult handleList(const Frame& request, const RoomMember& caller);
    FileResult handleInfo(const Frame& request, const RoomMember& caller);
    FileResult handleThumb(const Frame& request, const RoomMember& caller);
    FileResult handleDelete(const Frame& request, const RoomMember& caller);

    bool canAccess(FileKind kind, uint64_t target_id, uint64_t user_id,
                   uint64_t uploader_id, bool write, std::string& error);
    bool checkRoom(const UploadState& upload, const RoomMember& caller,
                   std::string& error);
    bool checkPrivate(const UploadState& upload, const RoomMember& caller,
                      std::string& error);
    UploadCache* loadUpload(const std::string& upload_id, uint64_t user_id,
                            std::string& error);
    bool persistOffset(UploadCache& cache, std::string& error);
    std::string finalPath(uint64_t file_id) const;
    std::string thumbPath(uint64_t file_id) const;
    bool ensureStorage(std::string& error);
    FileResult errorResult(const Frame& request, uint16_t code,
                           const std::string& message) const;
    FileResult successResult(const Frame& request, std::string body = {}) const;
    std::string buildRoomPush(const UploadState& upload, uint64_t message_id,
                              uint64_t file_id, const std::string& created_at,
                              const FileMeta& meta,
                              const RoomMember& caller) const;
    std::string buildPrivatePush(const UploadState& upload, uint64_t message_id,
                                 uint64_t file_id,
                                 const std::string& created_at,
                                 const FileMeta& meta,
                                 const RoomMember& caller) const;
    std::string buildFileDeletedFrame(uint64_t file_id,
                                      uint64_t message_id) const;

    MysqlConnection& database_;
    RoomManager& rooms_;
    std::string files_dir_;
    FileThumbnailer thumbnailer_;
    std::unordered_map<std::string, UploadCache> uploads_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_FILE_FILE_SERVICE_H
