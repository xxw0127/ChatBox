#include "server/private/private_service.h"

#include <cctype>
#include <ctime>
#include <string>

#include "common/text_utils.h"
#include "server/util/logging.h"

namespace chatbox {

namespace {

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

bool validClientMessageId(const std::string& value) {
    if (value.empty() || value.size() > 36) {
        return false;
    }
    for (const unsigned char ch : value) {
        if (!std::isalnum(ch) && ch != '-') {
            return false;
        }
    }
    return true;
}

std::string utcNow() {
    const std::time_t now = std::time(nullptr);
    std::tm utc_time{};
    gmtime_r(&now, &utc_time);
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &utc_time);
    return buffer;
}

}  // namespace

PrivateService::PrivateService(MysqlConnection& database, SessionManager& sessions)
    : database_(database), sessions_(sessions) {}

PrivateResult PrivateService::handle(const Frame& request, const RoomMember& caller) {
    if (caller.user_id == 0) {
        return PrivateResult{makeError(request.header.seq, 1002, "not authenticated")};
    }
    switch (request.header.cmd) {
        case kCmdPrivateSend:
            return handlePrivateSend(request, caller);
        case kCmdPrivateConversations:
            return handleConversations(request, caller);
        case kCmdPrivateMarkRead:
            return handleMarkRead(request, caller);
        case kCmdUserSearch:
            return handleUserSearch(request, caller);
        case kCmdMessageRecall:
            return handleRecall(request, caller);
        default:
            return PrivateResult{makeError(request.header.seq, 1001, "unknown command")};
    }
}

PrivateResult PrivateService::handlePrivateSend(const Frame& request,
                                                const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint64_t receiver_id = 0;
    std::string client_msg_id;
    std::string content;
    if (!reader.getU64(receiver_id) || !reader.getString(client_msg_id) ||
        !reader.getString(content)) {
        return PrivateResult{makeError(request.header.seq, 1001, "invalid private body")};
    }
    uint64_t reply_to_id = 0;
    std::string reply_sender_name;
    std::string reply_content;
    if (reader.remaining() >= 8) {
        if (!reader.getU64(reply_to_id)) {
            return PrivateResult{makeError(request.header.seq, 1001, "invalid reply body")};
        }
        if (reader.remaining() >= 2 && !reader.getString(reply_sender_name)) {
            return PrivateResult{makeError(request.header.seq, 1001, "invalid reply body")};
        }
        if (reader.remaining() >= 2 && !reader.getString(reply_content)) {
            return PrivateResult{makeError(request.header.seq, 1001, "invalid reply body")};
        }
    }
    if (reply_to_id == 0) {
        reply_sender_name.clear();
        reply_content.clear();
    }
    if (receiver_id == 0 || receiver_id == caller.user_id) {
        return PrivateResult{makeError(request.header.seq, 1001, "invalid receiver")};
    }
    if (!validClientMessageId(client_msg_id)) {
        return PrivateResult{makeError(request.header.seq, 1001, "invalid client_msg_id")};
    }
    if (content.empty() || content.size() > 2000) {
        return PrivateResult{makeError(request.header.seq, 1011, "message length invalid")};
    }
    if (chatbox::utf8CodePointCount(reply_sender_name) > 32 ||
        reply_content.size() > 2000) {
        return PrivateResult{makeError(request.header.seq, 1001, "invalid reply content")};
    }

    UserRecord receiver;
    bool found = false;
    std::string error;
    if (!database_.findUserById(receiver_id, receiver, found, error)) {
        logMessage(LogLevel::kError, "private", "find receiver failed: " + error);
        return PrivateResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!found || receiver.status != 0) {
        return PrivateResult{makeError(request.header.seq, 1008, "receiver not found")};
    }

    const std::string created_at = utcNow();
    uint64_t message_id = 0;
    bool duplicate = false;
    if (!database_.insertPrivateMessage(caller.user_id, receiver_id, client_msg_id, content,
                                        reply_to_id, reply_sender_name, reply_content,
                                        created_at, message_id, duplicate, error)) {
        logMessage(LogLevel::kError, "private", "insert private message failed: " + error);
        return PrivateResult{makeError(request.header.seq, 2001, "internal error")};
    }

    PrivateResult result;
    result.response = makeSuccess(kCmdPrivateSendResp, request.header.seq);
    ByteWriter response_writer;
    response_writer.putU64(message_id);
    response_writer.putString(created_at);
    response_writer.putU64(reply_to_id);
    response_writer.putString(reply_sender_name);
    response_writer.putString(reply_content);
    result.response.body = response_writer.data();

    if (duplicate) {
        return result;
    }

    if (!sessions_.isOnline(receiver_id)) {
        return result;
    }

    Frame push;
    push.header.cmd = kCmdPrivatePush;
    push.header.seq = 0;
    ByteWriter push_writer;
    push_writer.putU64(caller.user_id);
    push_writer.putU64(message_id);
    push_writer.putString(caller.nickname);
    push_writer.putString(content);
    push_writer.putString(created_at);
    push_writer.putU64(reply_to_id);
    push_writer.putString(reply_sender_name);
    push_writer.putString(reply_content);
    push.body = push_writer.data();

    result.push = true;
    result.target_user_id = receiver_id;
    result.push_frame = encodeFrame(push);
    return result;
}

PrivateResult PrivateService::handleConversations(const Frame& request,
                                                   const RoomMember& caller) {
    std::vector<ConversationRow> conversations;
    std::string error;
    if (!database_.listConversations(caller.user_id, conversations, error)) {
        logMessage(LogLevel::kError, "private", "list conversations failed: " + error);
        return PrivateResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (conversations.size() > 0xFFFF) {
        conversations.resize(0xFFFF);
    }

    PrivateResult result;
    result.response = makeSuccess(kCmdPrivateConversations, request.header.seq);
    ByteWriter writer;
    writer.putU16(static_cast<uint16_t>(conversations.size()));
    for (const ConversationRow& row : conversations) {
        writer.putU64(row.peer_id);
        writer.putString(row.nickname);
        writer.putU64(row.last_message_id);
        writer.putString(row.last_content);
        writer.putString(row.last_time);
        writer.putU8(sessions_.isOnline(row.peer_id) ? 1 : 0);
        writer.putU32(row.unread);
    }
    result.response.body = writer.data();
    return result;
}

PrivateResult PrivateService::handleMarkRead(const Frame& request,
                                             const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint64_t peer_id = 0;
    uint64_t message_id = 0;
    if (!reader.getU64(peer_id) || !reader.getU64(message_id)) {
        return PrivateResult{makeError(request.header.seq, 1001, "invalid mark read body")};
    }
    if (peer_id == 0 || peer_id == caller.user_id) {
        return PrivateResult{makeError(request.header.seq, 1001, "invalid peer id")};
    }
    std::string error;
    if (!database_.markPrivateRead(caller.user_id, peer_id, message_id, error)) {
        logMessage(LogLevel::kError, "private", "mark read failed: " + error);
        return PrivateResult{makeError(request.header.seq, 2001, "internal error")};
    }
    PrivateResult result;
    result.response = makeSuccess(kCmdPrivateMarkRead, request.header.seq);
    return result;
}

PrivateResult PrivateService::handleUserSearch(const Frame& request,
                                               const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    std::string keyword;
    if (!reader.getString(keyword) || keyword.empty() || keyword.size() > 32) {
        return PrivateResult{makeError(request.header.seq, 1001, "invalid search keyword")};
    }

    std::vector<UserSearchRow> users;
    std::string error;
    if (!database_.searchUsers(keyword, users, error)) {
        logMessage(LogLevel::kError, "private", "user search failed: " + error);
        return PrivateResult{makeError(request.header.seq, 2001, "internal error")};
    }

    PrivateResult result;
    result.response = makeSuccess(kCmdUserSearch, request.header.seq);
    ByteWriter writer;
    uint16_t visible_count = 0;
    for (const UserSearchRow& user : users) {
        if (user.id != caller.user_id) {
            ++visible_count;
        }
    }
    writer.putU16(visible_count);
    for (const UserSearchRow& user : users) {
        if (user.id == caller.user_id) {
            continue;
        }
        writer.putU64(user.id);
        writer.putString(user.username);
        writer.putString(user.nickname);
        writer.putU8(sessions_.isOnline(user.id) ? 1 : 0);
    }
    result.response.body = writer.data();
    return result;
}

PrivateResult PrivateService::handleRecall(const Frame& request,
                                           const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint8_t kind = 0;
    uint64_t target_id = 0;
    uint64_t message_id = 0;
    if (!reader.getU8(kind) || !reader.getU64(target_id) || !reader.getU64(message_id) ||
        kind != 2 || target_id == 0 || target_id == caller.user_id) {
        return PrivateResult{makeError(request.header.seq, 1001, "invalid recall body")};
    }
    std::string error;
    if (!database_.recallPrivateMessage(caller.user_id, target_id, message_id, error)) {
        return PrivateResult{makeError(request.header.seq, 1006, "message cannot be recalled")};
    }

    Frame recall;
    recall.header.cmd = kCmdMessageRecall;
    recall.header.seq = 0;
    ByteWriter writer;
    writer.putU8(2);
    writer.putU64(target_id);
    writer.putU64(message_id);
    recall.body = writer.data();

    PrivateResult result;
    result.response = makeSuccess(kCmdMessageRecall, request.header.seq);
    result.response.body = recall.body;

    if (sessions_.isOnline(target_id)) {
        result.push = true;
        result.target_user_id = target_id;
        result.push_frame = encodeFrame(recall);
    }
    return result;
}

}  // namespace chatbox
