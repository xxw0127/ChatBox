#include "server/file/file_service.h"

#include <fcntl.h>
#include <openssl/sha.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "common/file_protocol.h"
#include "common/text_utils.h"
#include "server/auth/token.h"
#include "server/file/file_rules.h"
#include "server/group/group_rules.h"
#include "server/util/logging.h"

namespace chatbox {

namespace {

std::string randomHex(std::size_t bytes) {
    std::random_device device;
    std::mt19937_64 generator(device());
    static const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        const unsigned value = generator() & 0xFF;
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0F]);
    }
    return result;
}

std::string utcNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

std::string sha256File(const std::string& path, bool& ok) {
    ok = false;
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return {};
    }
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    std::vector<char> buffer(64 * 1024);
    while (true) {
        const ssize_t read = ::read(fd, buffer.data(), buffer.size());
        if (read < 0) {
            ::close(fd);
            return {};
        }
        if (read == 0) {
            break;
        }
        SHA256_Update(&ctx, buffer.data(), static_cast<std::size_t>(read));
    }
    ::close(fd);
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256_Final(digest, &ctx);
    static const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve(SHA256_DIGEST_LENGTH * 2);
    for (const unsigned char byte : digest) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0F]);
    }
    ok = true;
    return result;
}

bool readFile(const std::string& path, std::string& content) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }
    content.clear();
    char buffer[8192];
    while (true) {
        const ssize_t read = ::read(fd, buffer, sizeof(buffer));
        if (read < 0) {
            ::close(fd);
            return false;
        }
        if (read == 0) {
            break;
        }
        content.append(buffer, static_cast<std::size_t>(read));
    }
    ::close(fd);
    return true;
}

}  // namespace

FileService::FileService(MysqlConnection& database, RoomManager& rooms,
                         std::string files_dir, std::string ffmpeg_path,
                         std::string ffprobe_path)
    : database_(database),
      rooms_(rooms),
      files_dir_(std::move(files_dir)),
      thumbnailer_(std::move(ffmpeg_path), std::move(ffprobe_path)) {}

FileService::~FileService() {
    for (auto& entry : uploads_) {
        if (entry.second.fd >= 0) {
            ::close(entry.second.fd);
        }
    }
}

FileResult FileService::handle(const Frame& request, const RoomMember& caller,
                               const std::shared_ptr<Connection>& connection) {
    if (request.header.cmd == kCmdFileAuth) {
        return handleAuth(request, caller, connection);
    }
    if (caller.user_id == 0 ||
        (connection != nullptr && !connection->authenticated())) {
        return errorResult(request, 1002, "not authenticated");
    }
    switch (request.header.cmd) {
        case kCmdFileUploadInit:
            return handleUploadInit(request, caller);
        case kCmdFileUploadChunk:
            return handleUploadChunk(request, caller);
        case kCmdFileUploadStatus:
            return handleUploadStatus(request, caller);
        case kCmdFileUploadCommit:
            return handleUploadCommit(request, caller);
        case kCmdFileUploadCancel:
            return handleUploadCancel(request, caller);
        case kCmdFileOpen:
            return handleOpen(request, caller);
        case kCmdFileRead:
            return handleRead(request, caller);
        case kCmdFileReadAck:
            return handleReadAck(request, caller);
        case kCmdFileList:
            return handleList(request, caller);
        case kCmdFileInfo:
            return handleInfo(request, caller);
        case kCmdFileThumb:
            return handleThumb(request, caller);
        case kCmdFileDelete:
            return handleDelete(request, caller);
        default:
            return errorResult(request, 1001, "unknown file command");
    }
}

FileResult FileService::handleAuth(
    const Frame& request, const RoomMember&,
    const std::shared_ptr<Connection>& connection) {
    chatbox::ByteReader reader(request.body.data(), request.body.size());
    std::string token;
    if (!reader.getString(token) || token.size() != 64) {
        return errorResult(request, 1001, "invalid file auth");
    }
    const std::string token_hash = hashToken(token);
    SessionRecord session;
    bool found = false;
    std::string error;
    if (!database_.findSession(token_hash, session, found, error) || !found ||
        !session.valid || session.revoked != 0) {
        return errorResult(request, 1002, "session expired");
    }
    UserRecord user;
    bool user_found = false;
    if (!database_.findUserById(session.user_id, user, user_found, error) ||
        !user_found || user.status != 0) {
        return errorResult(request, 1002, "user unavailable");
    }
    if (connection != nullptr) {
        connection->setAuth(user.id, user.nickname, token_hash);
    }
    error.clear();
    return successResult(request, encodeFileIdResponse(user.id));
}

FileResult FileService::handleUploadInit(const Frame& request,
                                         const RoomMember& caller) {
    UploadInitRequest init;
    if (!decodeUploadInit(request.body, init)) {
        return errorResult(request, 1001, "invalid upload init");
    }
    if (!validFileSize(init.size) || !validFileName(init.name) ||
        (!init.sha256.empty() && init.sha256.size() != 64)) {
        return errorResult(request, 1001, "invalid file metadata");
    }
    std::string error;
    if (init.kind == FileKind::kRoom) {
        UploadState check;
        check.kind = init.kind;
        check.target_id = init.target_id;
        if (!checkRoom(check, caller, error)) {
            return errorResult(request, 1003, error);
        }
    } else if (init.kind == FileKind::kPrivate) {
        UploadState check;
        check.kind = init.kind;
        check.target_id = init.target_id;
        if (!checkPrivate(check, caller, error)) {
            return errorResult(request, 1003, error);
        }
    } else {
        return errorResult(request, 1001, "invalid file kind");
    }

    FileMeta meta;
    meta.name = sanitizeFileName(init.name);
    meta.size = init.size;
    meta.mime = init.mime.substr(0, 128);
    meta.sha256 = init.sha256;
    if (init.kind == FileKind::kRoom) {
        std::vector<FileEntry> entries;
        uint32_t total = 0;
        uint64_t used = 0;
        if (!database_.listFileRecords(FileKind::kRoom, caller.user_id, init.target_id, 0, 1,
                                       entries, total, used, error)) {
            return errorResult(request, 2001, "internal error");
        }
        if (used + init.size > kMaxGroupFileBytes) {
            return errorResult(request, 1004, "group file quota exceeded");
        }
    }
    std::error_code ec;
    const auto space = std::filesystem::space(files_dir_, ec);
    if (!ec && space.available < kMinFreeDiskBytes + init.size) {
        return errorResult(request, 1004, "server disk is full");
    }
    if (!ensureStorage(error)) {
        return errorResult(request, 2001, error);
    }

    UploadState upload;
    upload.upload_id = randomHex(16);
    upload.uploader_id = caller.user_id;
    upload.kind = init.kind;
    upload.target_id = init.target_id;
    upload.name = meta.name;
    upload.size = init.size;
    upload.mime = meta.mime;
    upload.sha256 = init.sha256;
    upload.temp_path = std::string("tmp/") + upload.upload_id + ".part";
    upload.offset = 0;
    if (!database_.createFileUpload(upload, error)) {
        return errorResult(request, 2001, error);
    }
    int fd = ::open((files_dir_ + "/" + upload.temp_path).c_str(),
                    O_RDWR | O_CREAT | O_TRUNC, 0640);
    if (fd < 0) {
        return errorResult(request, 2001, "cannot create temp file");
    }
    UploadCache cache;
    cache.state = upload;
    cache.fd = fd;
    uploads_[upload.upload_id] = std::move(cache);
    return successResult(
        request, encodeUploadInitResponse(upload.upload_id, upload.offset));
}

FileResult FileService::handleUploadChunk(const Frame& request,
                                          const RoomMember& caller) {
    std::string upload_id;
    uint64_t offset = 0;
    std::string data;
    if (!decodeUploadChunk(request.body, upload_id, offset, data) ||
        data.size() > 60 * 1024) {
        return errorResult(request, 1001, "invalid upload chunk");
    }
    std::string error;
    UploadCache* cache = loadUpload(upload_id, caller.user_id, error);
    if (cache == nullptr) {
        return errorResult(request, 1004, error);
    }
    if (offset < cache->state.offset) {
        return successResult(request, encodeUploadAck(cache->state.offset));
    }
    if (offset > cache->state.offset) {
        return errorResult(request, 1004, "upload offset jumped");
    }
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t result =
            ::pwrite(cache->fd, data.data() + written, data.size() - written,
                     static_cast<off_t>(cache->state.offset + written));
        if (result <= 0) {
            return errorResult(request, 2001, "write failed");
        }
        written += static_cast<std::size_t>(result);
    }
    cache->state.offset += data.size();
    if (cache->state.offset - cache->last_persisted_offset >= 1024 * 1024 ||
        cache->state.offset == cache->state.size) {
        if (!persistOffset(*cache, error)) {
            return errorResult(request, 2001, error);
        }
    }
    return successResult(request, encodeUploadAck(cache->state.offset));
}

FileResult FileService::handleUploadStatus(const Frame& request,
                                           const RoomMember& caller) {
    std::string upload_id;
    chatbox::ByteReader reader(request.body.data(), request.body.size());
    if (!reader.getString(upload_id)) {
        return errorResult(request, 1001, "invalid upload status");
    }
    std::string error;
    UploadCache* cache = loadUpload(upload_id, caller.user_id, error);
    if (cache == nullptr) {
        return errorResult(request, 1004, error);
    }
    return successResult(request,
                         encodeUploadStatusResponse(cache->state.offset,
                                                    static_cast<uint8_t>(
                                                        cache->state.status)));
}

FileResult FileService::handleUploadCommit(const Frame& request,
                                           const RoomMember& caller) {
    std::string upload_id;
    chatbox::ByteReader reader(request.body.data(), request.body.size());
    if (!reader.getString(upload_id)) {
        return errorResult(request, 1001, "invalid upload commit");
    }
    std::string error;
    UploadCache* cache = loadUpload(upload_id, caller.user_id, error);
    if (cache == nullptr) {
        return errorResult(request, 1004, error);
    }
    if (cache->state.status == 1) {
        return errorResult(request, 1004, "upload already committed");
    }
    if (cache->state.offset != cache->state.size) {
        return errorResult(request, 1004, "upload is incomplete");
    }
    if (cache->fd >= 0) {
        ::fsync(cache->fd);
    }
    const std::string temp_path = files_dir_ + "/" + cache->state.temp_path;
    bool hash_ok = false;
    const std::string actual_sha = sha256File(temp_path, hash_ok);
    if (!hash_ok ||
        (!cache->state.sha256.empty() &&
         actual_sha != cache->state.sha256)) {
        return errorResult(request, 1004, "sha256 mismatch");
    }
    cache->state.sha256 = actual_sha;

    FileMeta meta;
    meta.name = cache->state.name;
    meta.size = cache->state.size;
    meta.mime = cache->state.mime;
    meta.sha256 = actual_sha;
    FileEntry entry;
    if (!database_.insertFileRecord(caller.user_id, cache->state.kind,
                                    cache->state.target_id, meta, "", entry,
                                    error)) {
        return errorResult(request, 2001, error);
    }
    std::error_code ec;
    const std::string relative = finalPath(entry.file_id);
    const std::string final_path = files_dir_ + "/" + relative;
    std::filesystem::create_directories(
        std::filesystem::path(final_path).parent_path(), ec);
    std::filesystem::rename(temp_path, final_path, ec);
    if (ec) {
        database_.markFileDeleted(entry.file_id, error);
        return errorResult(request, 2001, "cannot store file");
    }
    if (!database_.setFileStoragePath(entry.file_id, relative, error)) {
        database_.markFileDeleted(entry.file_id, error);
        return errorResult(request, 2001, error);
    }
    if (isThumbnailMime(cache->state.mime)) {
        const std::string thumb_relative = thumbPath(entry.file_id);
        const ThumbnailResult thumb = thumbnailer_.generate(
            final_path, files_dir_ + "/" + thumb_relative, cache->state.mime);
        if (thumb.ok) {
            meta.thumb_available = true;
            meta.width = thumb.width;
            meta.height = thumb.height;
            meta.duration_ms = thumb.duration_ms;
        }
        database_.setFileThumbnail(entry.file_id,
                                   thumb.ok ? thumb_relative : std::string(),
                                   thumb.width, thumb.height,
                                   thumb.duration_ms, error);
    }
    const std::string content = "[文件] " + cache->state.name;
    const std::string created_at = utcNow();
    uint64_t message_id = 0;
    if (cache->state.kind == FileKind::kRoom) {
        if (!database_.insertRoomFileMessage(
                static_cast<int>(cache->state.target_id), caller.user_id,
                cache->state.upload_id, content, entry.file_id, created_at,
                message_id, error)) {
            database_.markFileDeleted(entry.file_id, error);
            return errorResult(request, 2001, error);
        }
        if (!database_.setFileMessageId(entry.file_id, message_id, error)) {
            return errorResult(request, 2001, error);
        }
        database_.updateFileUploadStatus(upload_id, 1, error);
        FileResult result = successResult(
            request, encodeUploadCommitResponse(message_id, entry.file_id,
                                                created_at));
        FileMeta push_meta = meta;
        push_meta.file_id = entry.file_id;
        result.broadcast = true;
        result.broadcast_frame = buildRoomPush(
            cache->state, message_id, entry.file_id, created_at, push_meta,
            caller);
        rooms_.snapshot(static_cast<int>(cache->state.target_id), result.targets);
        return result;
    }
    if (!database_.insertPrivateFileMessage(
            caller.user_id, cache->state.target_id, cache->state.upload_id,
            content, entry.file_id, created_at, message_id, error)) {
        database_.markFileDeleted(entry.file_id, error);
        return errorResult(request, 2001, error);
    }
    if (!database_.setFileMessageId(entry.file_id, message_id, error)) {
        return errorResult(request, 2001, error);
    }
    database_.updateFileUploadStatus(upload_id, 1, error);
    FileResult result = successResult(
        request, encodeUploadCommitResponse(message_id, entry.file_id,
                                            created_at));
    FileNotice notice;
    notice.user_id = cache->state.target_id;
    FileMeta push_meta = meta;
    push_meta.file_id = entry.file_id;
    notice.frame = buildPrivatePush(cache->state, message_id, entry.file_id,
                                    created_at, push_meta, caller);
    result.notices.push_back(std::move(notice));
    return result;
}

FileResult FileService::handleUploadCancel(const Frame& request,
                                           const RoomMember& caller) {
    std::string upload_id;
    chatbox::ByteReader reader(request.body.data(), request.body.size());
    if (!reader.getString(upload_id)) {
        return errorResult(request, 1001, "invalid upload cancel");
    }
    std::string error;
    UploadCache* cache = loadUpload(upload_id, caller.user_id, error);
    if (cache == nullptr) {
        return errorResult(request, 1004, error);
    }
    if (cache->fd >= 0) {
        ::close(cache->fd);
        cache->fd = -1;
    }
    std::filesystem::remove(files_dir_ + "/" + cache->state.temp_path);
    database_.updateFileUploadStatus(upload_id, 2, error);
    uploads_.erase(upload_id);
    return successResult(request, encodeFileIdResponse(0));
}

FileResult FileService::handleOpen(const Frame& request,
                                   const RoomMember& caller) {
    uint64_t file_id = 0;
    if (!decodeFileIdResponse(request.body, file_id)) {
        return errorResult(request, 1001, "invalid file open");
    }
    FileAccess access;
    bool found = false;
    std::string error;
    if (!database_.findFileAccess(file_id, access, found, error) || !found) {
        return errorResult(request, 1004, "file not found");
    }
    if (access.status != 0 ||
        !canAccess(access.kind, access.room_id == 0 ? access.private_receiver_id
                                                    : access.room_id,
                   caller.user_id, access.uploader_id, false, error)) {
        if (access.kind == FileKind::kPrivate &&
            (access.private_sender_id == caller.user_id ||
             access.private_receiver_id == caller.user_id)) {
            error.clear();
        } else {
            return errorResult(request, 1003, error.empty() ? "forbidden" : error);
        }
    }
    return successResult(
        request, encodeFileOpenResponse(access.size,
                                        std::to_string(file_id) + ":" +
                                            randomHex(8)));
}

FileResult FileService::handleRead(const Frame& request,
                                   const RoomMember& caller) {
    std::string token;
    uint64_t offset = 0;
    uint32_t length = 0;
    if (!decodeFileRead(request.body, token, offset, length) || length == 0) {
        return errorResult(request, 1001, "invalid file read");
    }
    const std::size_t colon = token.find(':');
    if (colon == std::string::npos) {
        return errorResult(request, 1001, "invalid range token");
    }
    uint64_t file_id = 0;
    try {
        file_id = std::stoull(token.substr(0, colon));
    } catch (...) {
        return errorResult(request, 1001, "invalid range token");
    }
    FileAccess access;
    bool found = false;
    std::string error;
    if (!database_.findFileAccess(file_id, access, found, error) || !found ||
        access.status != 0) {
        return errorResult(request, 1004, "file not found");
    }
    const bool allowed =
        (access.kind == FileKind::kRoom &&
         database_.isRoomMember(static_cast<int>(access.room_id), caller.user_id,
                                found, error) &&
         found) ||
        (access.kind == FileKind::kPrivate &&
         (access.private_sender_id == caller.user_id ||
          access.private_receiver_id == caller.user_id));
    if (!allowed) {
        return errorResult(request, 1003, "forbidden");
    }
    if (offset >= access.size) {
        return errorResult(request, 1004, "offset out of range");
    }
    const std::string path = files_dir_ + "/" + access.storage_path;
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return errorResult(request, 2001, "file missing");
    }
    const std::size_t want = std::min<std::size_t>(
        {static_cast<std::size_t>(length), 60 * 1024,
         static_cast<std::size_t>(access.size - offset)});
    std::string data(want, '\0');
    const ssize_t read = ::pread(fd, data.data(), want, static_cast<off_t>(offset));
    ::close(fd);
    if (read <= 0) {
        return errorResult(request, 2001, "read failed");
    }
    data.resize(static_cast<std::size_t>(read));
    Frame chunk;
    chunk.header.flags = kFlagResponse;
    chunk.header.cmd = kCmdFileChunk;
    chunk.header.seq = request.header.seq;
    chunk.body = encodeFileChunk(file_id, offset, data);
    FileResult result;
    result.responses.push_back(std::move(chunk));
    return result;
}

FileResult FileService::handleReadAck(const Frame& request,
                                      const RoomMember&) {
    return successResult(request);
}

FileResult FileService::handleList(const Frame& request,
                                   const RoomMember& caller) {
    FileKind kind = FileKind::kRoom;
    uint64_t target_id = 0;
    uint64_t before = 0;
    uint16_t limit = 0;
    if (!decodeFileListRequest(request.body, kind, target_id, before, limit)) {
        return errorResult(request, 1001, "invalid file list");
    }
    std::string error;
    bool member = false;
    if (kind == FileKind::kRoom) {
        if (!database_.isRoomMember(static_cast<int>(target_id), caller.user_id,
                                    member, error) ||
            !member) {
            return errorResult(request, 1003, "not a group member");
        }
    } else if (target_id != caller.user_id) {
        // Private list target is the peer; both participants see the shared list.
    }
    std::vector<FileEntry> entries;
    uint32_t total = 0;
    uint64_t used = 0;
    if (!database_.listFileRecords(kind, caller.user_id, target_id, before, limit, entries, total,
                                   used, error)) {
        return errorResult(request, 2001, error);
    }
    GroupRole role = GroupRole::kMember;
    bool role_found = false;
    if (kind == FileKind::kRoom) {
        database_.getRoomMemberRole(target_id, caller.user_id, role, role_found,
                                    error);
    }
    for (FileEntry& entry : entries) {
        entry.can_delete =
            entry.uploader_id == caller.user_id ||
            (kind == FileKind::kRoom &&
             (role == GroupRole::kOwner || role == GroupRole::kAdmin));
    }
    return successResult(request,
                         encodeFileListResponse(entries, total,
                                                kind == FileKind::kRoom
                                                    ? used
                                                    : 0));
}

FileResult FileService::handleInfo(const Frame& request,
                                   const RoomMember& caller) {
    uint64_t file_id = 0;
    if (!decodeFileIdResponse(request.body, file_id)) {
        return errorResult(request, 1001, "invalid file info");
    }
    FileAccess access;
    bool found = false;
    std::string error;
    if (!database_.findFileAccess(file_id, access, found, error) || !found) {
        return errorResult(request, 1004, "file not found");
    }
    bool member = false;
    if (access.kind == FileKind::kRoom) {
        database_.isRoomMember(static_cast<int>(access.room_id), caller.user_id,
                               member, error);
    } else {
        member = access.private_sender_id == caller.user_id ||
                 access.private_receiver_id == caller.user_id;
    }
    if (!member) {
        return errorResult(request, 1003, "forbidden");
    }
    FileEntry entry;
    if (!database_.findFileRecord(file_id, entry, found, error) || !found) {
        return errorResult(request, 1004, "file not found");
    }
    return successResult(request, encodeFileInfo(entry));
}

FileResult FileService::handleThumb(const Frame& request,
                                    const RoomMember& caller) {
    uint64_t file_id = 0;
    if (!decodeFileIdResponse(request.body, file_id)) {
        return errorResult(request, 1001, "invalid file thumb");
    }
    FileAccess access;
    bool found = false;
    std::string error;
    if (!database_.findFileAccess(file_id, access, found, error) || !found) {
        return errorResult(request, 1004, "file not found");
    }
    bool member = false;
    if (access.kind == FileKind::kRoom) {
        database_.isRoomMember(static_cast<int>(access.room_id), caller.user_id,
                               member, error);
    } else {
        member = access.private_sender_id == caller.user_id ||
                 access.private_receiver_id == caller.user_id;
    }
    if (!member || access.status != 0) {
        return errorResult(request, 1003, "forbidden");
    }
    if (access.thumb_path.empty()) {
        return errorResult(request, 1004, "thumbnail unavailable");
    }
    std::string bytes;
    if (!readFile(files_dir_ + "/" + access.thumb_path, bytes)) {
        return errorResult(request, 2001, "thumbnail missing");
    }
    FileResult result;
    Frame response;
    response.header.flags = kFlagResponse;
    response.header.cmd = kCmdFileThumb;
    response.header.seq = request.header.seq;
    response.body = std::move(bytes);
    result.responses.push_back(std::move(response));
    return result;
}

FileResult FileService::handleDelete(const Frame& request,
                                     const RoomMember& caller) {
    uint64_t file_id = 0;
    if (!decodeFileIdResponse(request.body, file_id)) {
        return errorResult(request, 1001, "invalid file delete");
    }
    FileAccess access;
    bool found = false;
    std::string error;
    if (!database_.findFileAccess(file_id, access, found, error) || !found) {
        return errorResult(request, 1004, "file not found");
    }
    bool allowed = access.uploader_id == caller.user_id;
    if (!allowed && access.kind == FileKind::kRoom) {
        GroupRole role = GroupRole::kMember;
        bool role_found = false;
        database_.getRoomMemberRole(access.room_id, caller.user_id, role,
                                    role_found, error);
        allowed = role_found &&
                  (role == GroupRole::kOwner || role == GroupRole::kAdmin);
    }
    if (!allowed) {
        return errorResult(request, 1003, "forbidden");
    }
    std::filesystem::remove(files_dir_ + "/" + access.storage_path);
    if (!database_.markFileDeleted(file_id, error)) {
        return errorResult(request, 2001, error);
    }
    database_.updateFileMessageDeleted(access.message_id,
                                       access.kind == FileKind::kRoom, error);
    FileResult result = successResult(request, encodeFileIdResponse(file_id));
    const std::string frame =
        buildFileDeletedFrame(file_id, access.message_id);
    if (access.kind == FileKind::kRoom) {
        result.broadcast = true;
        result.broadcast_frame = frame;
        rooms_.snapshot(static_cast<int>(access.room_id), result.targets);
    } else {
        FileNotice to_sender;
        to_sender.user_id = access.private_sender_id;
        to_sender.frame = frame;
        result.notices.push_back(std::move(to_sender));
        if (access.private_receiver_id != access.private_sender_id) {
            FileNotice to_receiver;
            to_receiver.user_id = access.private_receiver_id;
            to_receiver.frame = frame;
            result.notices.push_back(std::move(to_receiver));
        }
    }
    return result;
}

bool FileService::canAccess(FileKind kind, uint64_t target_id, uint64_t user_id,
                            uint64_t, bool,
                            std::string& error) {
    if (kind == FileKind::kRoom) {
        bool member = false;
        return database_.isRoomMember(static_cast<int>(target_id), user_id,
                                      member, error) &&
               member;
    }
    (void)target_id;
    return true;
}

bool FileService::checkRoom(const UploadState& upload,
                            const RoomMember& caller, std::string& error) {
    GroupInfo group;
    bool found = false;
    if (!database_.findGroupById(upload.target_id, group, found, error) ||
        !found || group.status != GroupStatus::kActive) {
        error = "group not found";
        return false;
    }
    bool member = false;
    if (!database_.isRoomMember(upload.target_id, caller.user_id, member,
                                error) ||
        !member) {
        error = "not a group member";
        return false;
    }
    return true;
}

bool FileService::checkPrivate(const UploadState& upload,
                               const RoomMember& caller, std::string& error) {
    if (upload.target_id == 0 || upload.target_id == caller.user_id) {
        error = "invalid private target";
        return false;
    }
    UserRecord user;
    bool found = false;
    if (!database_.findUserById(upload.target_id, user, found, error) || !found) {
        error = "user not found";
        return false;
    }
    return true;
}

FileService::UploadCache* FileService::loadUpload(const std::string& upload_id,
                                                  uint64_t user_id,
                                                  std::string& error) {
    auto it = uploads_.find(upload_id);
    if (it != uploads_.end()) {
        if (it->second.state.uploader_id != user_id) {
            error = "upload does not belong to user";
            return nullptr;
        }
        return &it->second;
    }
    UploadState state;
    bool found = false;
    if (!database_.findFileUpload(upload_id, state, found, error) || !found) {
        error = "upload not found";
        return nullptr;
    }
    if (state.uploader_id != user_id) {
        error = "upload does not belong to user";
        return nullptr;
    }
    if (state.status != 0) {
        error = "upload is closed";
        return nullptr;
    }
    int fd = ::open((files_dir_ + "/" + state.temp_path).c_str(),
                    O_RDWR | O_CREAT, 0640);
    if (fd < 0) {
        error = "cannot open temp file";
        return nullptr;
    }
    struct stat stat_buffer {};
    if (::fstat(fd, &stat_buffer) == 0 &&
        static_cast<uint64_t>(stat_buffer.st_size) > state.offset) {
        state.offset = static_cast<uint64_t>(stat_buffer.st_size);
    }
    UploadCache cache;
    cache.state = state;
    cache.fd = fd;
    cache.last_persisted_offset = state.offset;
    auto inserted = uploads_.emplace(upload_id, std::move(cache));
    return &inserted.first->second;
}

bool FileService::persistOffset(UploadCache& cache, std::string& error) {
    if (!database_.updateFileUploadOffset(cache.state.upload_id,
                                          cache.state.offset, error)) {
        return false;
    }
    cache.last_persisted_offset = cache.state.offset;
    return true;
}

std::string FileService::finalPath(uint64_t file_id) const {
    char prefix[3] = {0};
    std::snprintf(prefix, sizeof(prefix), "%02x",
                  static_cast<unsigned>(file_id & 0xFF));
    return std::string(prefix) + "/" + std::to_string(file_id);
}

std::string FileService::thumbPath(uint64_t file_id) const {
    char name[64] = {0};
    std::snprintf(name, sizeof(name), "thumbs/%llu.jpg",
                  static_cast<unsigned long long>(file_id));
    return name;
}

bool FileService::ensureStorage(std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(files_dir_ + "/tmp", ec);
    std::filesystem::create_directories(files_dir_ + "/thumbs", ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

FileResult FileService::errorResult(const Frame& request, uint16_t code,
                                    const std::string& message) const {
    Frame response;
    response.header.flags = kFlagResponse | kFlagError;
    response.header.cmd = request.header.cmd;
    response.header.seq = request.header.seq;
    ByteWriter writer;
    writer.putU16(code);
    writer.putString(message);
    response.body = writer.data();
    FileResult result;
    result.responses.push_back(std::move(response));
    return result;
}

FileResult FileService::successResult(const Frame& request,
                                      std::string body) const {
    Frame response;
    response.header.flags = kFlagResponse;
    response.header.cmd = request.header.cmd;
    response.header.seq = request.header.seq;
    response.body = std::move(body);
    FileResult result;
    result.responses.push_back(std::move(response));
    return result;
}

std::string FileService::buildFileDeletedFrame(uint64_t file_id,
                                               uint64_t message_id) const {
    Frame push;
    push.header.cmd = kCmdFileDeleted;
    ByteWriter writer;
    writer.putU64(file_id);
    writer.putU64(message_id);
    push.body = writer.data();
    return encodeFrame(push);
}

std::string FileService::buildRoomPush(const UploadState& upload,
                                       uint64_t message_id, uint64_t file_id,
                                       const std::string& created_at,
                                       const FileMeta& meta,
                                       const RoomMember& caller) const {
    Frame push;
    push.header.cmd = kCmdChatPush;
    ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(upload.target_id));
    writer.putU64(message_id);
    writer.putU64(caller.user_id);
    writer.putString(caller.nickname);
    writer.putString("[文件] " + upload.name);
    writer.putString(created_at);
    writer.putU64(0);
    writer.putString("");
    writer.putString("");
    appendFileMeta(writer, meta);
    push.body = writer.data();
    return encodeFrame(push);
}

std::string FileService::buildPrivatePush(const UploadState& upload,
                                          uint64_t message_id,
                                          uint64_t file_id,
                                          const std::string& created_at,
                                          const FileMeta& meta,
                                          const RoomMember& caller) const {
    Frame push;
    push.header.cmd = kCmdPrivatePush;
    ByteWriter writer;
    writer.putU64(caller.user_id);
    writer.putU64(message_id);
    writer.putString(caller.nickname);
    writer.putString("[文件] " + upload.name);
    writer.putString(created_at);
    writer.putU64(0);
    writer.putString("");
    writer.putString("");
    appendFileMeta(writer, meta);
    push.body = writer.data();
    return encodeFrame(push);
}

}  // namespace chatbox
