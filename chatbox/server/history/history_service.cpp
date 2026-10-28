#include "server/history/history_service.h"

#include <limits>
#include <string>

#include "common/file_protocol.h"
#include "server/util/logging.h"

namespace chatbox {

namespace {

constexpr std::size_t kMaxHistoryBody = 36 * 1024;

std::size_t stringSize(const std::string& value) {
    return 2 + value.size();
}

std::size_t historyMessageSize(const HistoryMessage& message) {
    return 8 + 8 + stringSize(message.nickname) + stringSize(message.content) +
           stringSize(message.created_at) + 8 +
           stringSize(message.reply_sender_name) +
           stringSize(message.reply_content) + 1;
}

std::size_t searchRowSize(const SearchResultRow& row) {
    return 8 + 8 + stringSize(row.nickname) + stringSize(row.content) +
           stringSize(row.created_at) + 1;
}

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

}  // namespace

HistoryService::HistoryService(MysqlConnection& database, RoomManager& rooms)
    : database_(database), rooms_(rooms) {}

Frame HistoryService::handle(const Frame& request, const RoomMember& caller) {
    if (caller.user_id == 0) {
        return makeError(request.header.seq, 1002, "not authenticated");
    }
    if (request.header.cmd == kCmdMessageSearch) {
        return handleSearch(request, caller);
    }
    if (request.header.cmd != kCmdHistory) {
        return makeError(request.header.seq, 1001, "unknown command");
    }

    ByteReader reader(request.body.data(), request.body.size());
    uint8_t kind = 0;
    uint64_t target_id = 0;
    uint64_t before_id = 0;
    uint16_t requested_limit = 0;
    if (!reader.getU8(kind) || !reader.getU64(target_id) || !reader.getU64(before_id) ||
        !reader.getU16(requested_limit)) {
        return makeError(request.header.seq, 1001, "invalid history body");
    }
    if (kind != 1 && kind != 2) {
        return makeError(request.header.seq, 1001, "invalid history kind");
    }
    int limit = requested_limit == 0 ? 50 : static_cast<int>(requested_limit);
    if (limit > 50) {
        limit = 50;
    }

    HistoryPage page;
    std::string error;
    if (kind == 1) {
        if (target_id == 0 ||
            target_id > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return makeError(request.header.seq, 1001, "invalid room id");
        }
        const int room_id = static_cast<int>(target_id);
        bool member = false;
        if (!database_.isRoomMember(room_id, caller.user_id, member, error)) {
            logMessage(LogLevel::kError, "history", "room membership failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
        if (!member) {
            return makeError(request.header.seq, 1007, "not in room");
        }
        GroupInfo group;
        bool group_found = false;
        if (!database_.findGroupById(static_cast<uint64_t>(room_id), group,
                                     group_found, error)) {
            logMessage(LogLevel::kError, "history", "room status failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
        if (!group_found || group.status != GroupStatus::kActive) {
            return makeError(request.header.seq, 1006, "room not found");
        }
        uint64_t join_cursor = 0;
        if (!database_.roomJoinMessageId(room_id, caller.user_id, join_cursor,
                                         error)) {
            logMessage(LogLevel::kError, "history", "join cursor failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
        if (!database_.listRoomHistory(room_id, join_cursor, before_id, limit,
                                       page, error)) {
            logMessage(LogLevel::kError, "history", "room history failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
    } else {
        if (target_id == 0 || target_id == caller.user_id) {
            return makeError(request.header.seq, 1001, "invalid peer id");
        }
        if (!database_.listPrivateHistory(caller.user_id, target_id, before_id, limit, page,
                                          error)) {
            logMessage(LogLevel::kError, "history", "private history failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
        if (before_id == 0 && !page.messages.empty()) {
            std::string read_error;
            if (!database_.markPrivateRead(caller.user_id, target_id,
                                           page.messages.back().id, read_error)) {
                logMessage(LogLevel::kWarn, "history",
                           "mark private read failed: " + read_error);
            }
        }
    }

    std::vector<uint16_t> meta_indices;
    std::vector<FileMeta> meta_values;
    for (std::size_t i = 0; i < page.messages.size(); ++i) {
        if (page.messages[i].file_id == 0) {
            continue;
        }
        FileEntry entry;
        bool file_found = false;
        if (!database_.findFileRecord(page.messages[i].file_id, entry, file_found,
                                      error) ||
            !file_found) {
            continue;
        }
        FileMeta meta;
        meta.file_id = entry.file_id;
        meta.name = entry.name;
        meta.size = entry.size;
        meta.mime = entry.mime;
        meta.sha256 = entry.sha256;
        meta.thumb_available = entry.thumb_available;
        meta.width = entry.width;
        meta.height = entry.height;
        meta.duration_ms = entry.duration_ms;
        meta_indices.push_back(static_cast<uint16_t>(i));
        meta_values.push_back(std::move(meta));
    }

    std::size_t body_size = 8 + 8 + 2 + 1 + 8;
    std::size_t start = page.messages.size();
    for (std::size_t i = page.messages.size(); i > 0; --i) {
        const std::size_t message_size = historyMessageSize(page.messages[i - 1]);
        if (start < page.messages.size() &&
            body_size + message_size > kMaxHistoryBody) {
            break;
        }
        body_size += message_size;
        start = i - 1;
    }
    const std::size_t send_count = page.messages.size() - start;
    const bool has_more = page.has_more || start > 0;
    const uint64_t next_before =
        send_count > 0 ? page.messages[start].id : page.next_before_id;

    Frame response = makeSuccess(kCmdHistoryResp, request.header.seq);
    ByteWriter writer;
    writer.putU8(kind);
    writer.putU64(target_id);
    writer.putU16(static_cast<uint16_t>(send_count));
    writer.putU8(has_more ? 1 : 0);
    writer.putU64(next_before);
    for (std::size_t i = start; i < page.messages.size(); ++i) {
        const HistoryMessage& message = page.messages[i];
        writer.putU64(message.id);
        writer.putU64(message.sender_id);
        writer.putString(message.nickname);
        writer.putString(message.content);
        writer.putString(message.created_at);
        writer.putU64(message.reply_to_id);
        writer.putString(message.reply_sender_name);
        writer.putString(message.reply_content);
        writer.putU8(message.recalled ? 1 : 0);
    }
    uint16_t history_meta_count = 0;
    for (const uint16_t index : meta_indices) {
        if (index >= start) {
            ++history_meta_count;
        }
    }
    writer.putU16(history_meta_count);
    for (std::size_t i = 0; i < meta_values.size(); ++i) {
        if (meta_indices[i] < start) {
            continue;
        }
        writer.putU16(static_cast<uint16_t>(meta_indices[i] - start));
        appendFileMeta(writer, meta_values[i]);
    }
    response.body = writer.data();
    return response;
}

Frame HistoryService::handleSearch(const Frame& request, const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint8_t kind = 0;
    uint64_t target_id = 0;
    std::string keyword;
    uint16_t requested_limit = 0;
    if (!reader.getU8(kind) || !reader.getU64(target_id) || !reader.getString(keyword) ||
        !reader.getU16(requested_limit)) {
        return makeError(request.header.seq, 1001, "invalid search body");
    }
    if (keyword.empty() || keyword.size() > 64) {
        return makeError(request.header.seq, 1001, "invalid search keyword");
    }
    int limit = requested_limit == 0 ? 20 : requested_limit;
    if (limit > 50) {
        limit = 50;
    }

    std::vector<SearchResultRow> results;
    std::string error;
    if (kind == 1) {
        if (target_id == 0 ||
            target_id > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
            return makeError(request.header.seq, 1001, "invalid room id");
        }
        const int room_id = static_cast<int>(target_id);
        bool member = false;
        if (!database_.isRoomMember(room_id, caller.user_id, member, error)) {
            logMessage(LogLevel::kError, "history", "room membership failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
        if (!member) {
            return makeError(request.header.seq, 1007, "not in room");
        }
        GroupInfo group;
        bool group_found = false;
        if (!database_.findGroupById(static_cast<uint64_t>(room_id), group,
                                     group_found, error)) {
            logMessage(LogLevel::kError, "history", "room status failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
        if (!group_found || group.status != GroupStatus::kActive) {
            return makeError(request.header.seq, 1006, "room not found");
        }
        uint64_t join_cursor = 0;
        if (!database_.roomJoinMessageId(room_id, caller.user_id, join_cursor,
                                         error)) {
            logMessage(LogLevel::kError, "history", "join cursor failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
        if (!database_.searchRoomMessages(room_id, join_cursor, keyword, limit,
                                          results, error)) {
            logMessage(LogLevel::kError, "history", "room search failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
    } else if (kind == 2) {
        if (target_id == 0 || target_id == caller.user_id) {
            return makeError(request.header.seq, 1001, "invalid search target");
        }
        if (!database_.searchPrivateMessages(caller.user_id, target_id, keyword, limit,
                                             results, error)) {
            logMessage(LogLevel::kError, "history", "private search failed: " + error);
            return makeError(request.header.seq, 2001, "internal error");
        }
    } else {
        return makeError(request.header.seq, 1001, "invalid search kind");
    }

    std::vector<uint16_t> meta_indices;
    std::vector<FileMeta> meta_values;
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (results[i].file_id == 0) {
            continue;
        }
        FileEntry entry;
        bool file_found = false;
        if (!database_.findFileRecord(results[i].file_id, entry, file_found,
                                      error) ||
            !file_found) {
            continue;
        }
        FileMeta meta;
        meta.file_id = entry.file_id;
        meta.name = entry.name;
        meta.size = entry.size;
        meta.mime = entry.mime;
        meta.sha256 = entry.sha256;
        meta.thumb_available = entry.thumb_available;
        meta.width = entry.width;
        meta.height = entry.height;
        meta.duration_ms = entry.duration_ms;
        meta_indices.push_back(static_cast<uint16_t>(i));
        meta_values.push_back(std::move(meta));
    }

    std::size_t body_size = 2;
    std::size_t send_count = 0;
    for (const SearchResultRow& row : results) {
        const std::size_t row_size = searchRowSize(row);
        if (send_count > 0 && body_size + row_size > kMaxHistoryBody) {
            break;
        }
        body_size += row_size;
        ++send_count;
    }

    Frame response = makeSuccess(kCmdMessageSearch, request.header.seq);
    ByteWriter writer;
    writer.putU16(static_cast<uint16_t>(send_count));
    for (std::size_t i = 0; i < send_count; ++i) {
        const SearchResultRow& row = results[i];
        writer.putU64(row.id);
        writer.putU64(row.sender_id);
        writer.putString(row.nickname);
        writer.putString(row.content);
        writer.putString(row.created_at);
        writer.putU8(row.recalled ? 1 : 0);
    }
    uint16_t meta_count = 0;
    for (const uint16_t index : meta_indices) {
        if (index < send_count) {
            ++meta_count;
        }
    }
    writer.putU16(meta_count);
    for (std::size_t i = 0; i < meta_values.size(); ++i) {
        if (meta_indices[i] >= send_count) {
            continue;
        }
        writer.putU16(meta_indices[i]);
        appendFileMeta(writer, meta_values[i]);
    }
    response.body = writer.data();
    return response;
}

}  // namespace chatbox
