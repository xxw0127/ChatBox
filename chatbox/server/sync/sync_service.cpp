#include "server/sync/sync_service.h"

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "common/session_protocol.h"
#include "server/history/history_codec.h"
#include "server/util/logging.h"

namespace chatbox {

namespace {

constexpr std::size_t kMaxSyncBody = 480 * 1024;
constexpr int kMaxConversationMessages = 50;

Frame makeError(uint32_t seq, uint16_t code, const std::string& message) {
    Frame response;
    response.header.flags = kFlagResponse | kFlagError;
    response.header.cmd = kCmdError;
    response.header.seq = seq;
    ByteWriter writer;
    writer.putU16(code);
    writer.putString(message);
    response.body = writer.data();
    return response;
}

Frame makeSuccess(uint16_t command, uint32_t seq) {
    Frame response;
    response.header.flags = kFlagResponse;
    response.header.cmd = command;
    response.header.seq = seq;
    return response;
}

bool loadFileMetas(MysqlConnection& database,
                   const std::vector<HistoryMessage>& messages,
                   std::vector<IndexedFileMeta>& metas, std::string& error) {
    metas.clear();
    for (std::size_t i = 0; i < messages.size(); ++i) {
        if (messages[i].file_id == 0) {
            continue;
        }
        FileEntry entry;
        bool found = false;
        if (!database.findFileRecord(messages[i].file_id, entry, found, error) ||
            !found) {
            continue;
        }
        IndexedFileMeta indexed;
        indexed.index = static_cast<uint16_t>(i);
        indexed.meta.file_id = entry.file_id;
        indexed.meta.name = entry.name;
        indexed.meta.size = entry.size;
        indexed.meta.mime = entry.mime;
        indexed.meta.sha256 = entry.sha256;
        indexed.meta.thumb_available = entry.thumb_available;
        indexed.meta.width = entry.width;
        indexed.meta.height = entry.height;
        indexed.meta.duration_ms = entry.duration_ms;
        metas.push_back(std::move(indexed));
    }
    return true;
}

}  // namespace

SyncService::SyncService(MysqlConnection& database, RoomManager& rooms)
    : database_(database), rooms_(rooms) {}

Frame SyncService::handle(const Frame& request, const RoomMember& caller) {
    if (caller.user_id == 0) {
        return makeError(request.header.seq, 1002, "not authenticated");
    }
    ByteReader reader(request.body.data(), request.body.size());
    std::vector<SyncCursor> cursors;
    uint16_t start_index = 0;
    if (!readSyncPull(reader, cursors, start_index)) {
        return makeError(request.header.seq, 1001, "invalid sync body");
    }

    ByteWriter writer;
    const std::size_t count_offset = writer.data().size();
    writer.putU16(0);
    const std::size_t next_index_offset = writer.data().size();
    writer.putU16(start_index);
    const std::size_t has_more_offset = writer.data().size();
    writer.putU8(0);

    uint16_t next_index = start_index;
    bool has_more = false;

    for (std::size_t i = start_index; i < cursors.size(); ++i) {
        const SyncCursor& cursor = cursors[i];
        HistoryPage page;
        std::string error;
        uint64_t after_id = cursor.last_message_id;
        bool stale = false;

        if (cursor.kind == 1) {
            if (cursor.target_id == 0 ||
                cursor.target_id >
                    static_cast<uint64_t>(std::numeric_limits<int>::max())) {
                return makeError(request.header.seq, 1001, "invalid room id");
            }
            const int room_id = static_cast<int>(cursor.target_id);
            bool member = false;
            if (!database_.isRoomMember(room_id, caller.user_id, member, error)) {
                logMessage(LogLevel::kError, "sync", "room membership failed: " + error);
                return makeError(request.header.seq, 2001, "internal error");
            }
            if (!member) {
                // The user left or was removed; return an empty conversation so
                // one stale cursor does not block the rest of the batch.
                logMessage(LogLevel::kInfo, "sync",
                           "skip stale room cursor room=" + std::to_string(room_id) +
                               " user=" + std::to_string(caller.user_id));
                stale = true;
            }
            GroupInfo group;
            bool group_found = false;
            if (!stale &&
                !database_.findGroupById(cursor.target_id, group, group_found, error)) {
                logMessage(LogLevel::kError, "sync", "room status failed: " + error);
                return makeError(request.header.seq, 2001, "internal error");
            }
            if (!stale && (!group_found || group.status != GroupStatus::kActive)) {
                logMessage(LogLevel::kInfo, "sync",
                           "skip inactive room cursor room=" + std::to_string(room_id));
                stale = true;
            }
            if (!stale) {
                uint64_t join_cursor = 0;
                if (!database_.roomJoinMessageId(room_id, caller.user_id, join_cursor,
                                                 error)) {
                    logMessage(LogLevel::kError, "sync", "join cursor failed: " + error);
                    return makeError(request.header.seq, 2001, "internal error");
                }
                after_id = std::max(after_id, join_cursor);
                if (!database_.listRoomHistoryAfter(room_id, after_id,
                                                    kMaxConversationMessages, page,
                                                    error)) {
                    logMessage(LogLevel::kError, "sync", "room sync failed: " + error);
                    return makeError(request.header.seq, 2001, "internal error");
                }
            }
        } else {
            if (cursor.target_id == 0 || cursor.target_id == caller.user_id) {
                return makeError(request.header.seq, 1001, "invalid peer id");
            }
            if (!database_.listPrivateHistoryAfter(caller.user_id, cursor.target_id,
                                                   after_id,
                                                   kMaxConversationMessages, page,
                                                   error)) {
                logMessage(LogLevel::kError, "sync", "private sync failed: " + error);
                return makeError(request.header.seq, 2001, "internal error");
            }
        }

        if (stale) {
            const std::size_t conversation_size = 1 + 8 + 2 + 1 + 8 + 2;
            if (i > start_index &&
                writer.data().size() + conversation_size > kMaxSyncBody) {
                next_index = static_cast<uint16_t>(i);
                has_more = true;
                break;
            }
            writer.putU8(cursor.kind);
            writer.putU64(cursor.target_id);
            writer.putU16(0);
            writer.putU8(0);
            writer.putU64(after_id);
            writer.putU16(0);
            next_index = static_cast<uint16_t>(i + 1);
            continue;
        }

        std::vector<IndexedFileMeta> metas;
        if (!loadFileMetas(database_, page.messages, metas, error)) {
            return makeError(request.header.seq, 2001, "internal error");
        }
        EncodedHistory encoded;
        if (!encodeHistoryMessages(page.messages, metas, kMaxSyncBody, encoded, error)) {
            return makeError(request.header.seq, 2001, "internal error");
        }

        const std::size_t conversation_size =
            1 + 8 + 2 + 1 + 8 + encoded.body.size();
        if (i > start_index &&
            writer.data().size() + conversation_size > kMaxSyncBody) {
            next_index = static_cast<uint16_t>(i);
            has_more = true;
            break;
        }

        writer.putU8(cursor.kind);
        writer.putU64(cursor.target_id);
        writer.putU16(encoded.count);
        writer.putU8((encoded.has_more || page.has_more) ? 1 : 0);
        writer.putU64(encoded.next_after == 0 ? after_id : encoded.next_after);
        writer.putRaw(encoded.body);
        next_index = static_cast<uint16_t>(i + 1);
    }

    if (next_index < cursors.size()) {
        has_more = true;
    }
    std::string body = writer.data();
    const uint16_t written_count =
        static_cast<uint16_t>(next_index - start_index);
    body[count_offset] = static_cast<char>((written_count >> 8) & 0xFF);
    body[count_offset + 1] = static_cast<char>(written_count & 0xFF);
    body[next_index_offset] = static_cast<char>((next_index >> 8) & 0xFF);
    body[next_index_offset + 1] = static_cast<char>(next_index & 0xFF);
    body[has_more_offset] = has_more ? 1 : 0;

    Frame response = makeSuccess(kCmdSyncResp, request.header.seq);
    response.body = std::move(body);
    return response;
}

}  // namespace chatbox
