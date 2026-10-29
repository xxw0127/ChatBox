#include "server/friend/friend_service.h"

#include <string>
#include <vector>

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

void attachPush(FriendResult& result, uint64_t target_user_id,
                const Frame& push) {
    result.push = true;
    result.target_user_id = target_user_id;
    result.push_frame = encodeFrame(push);
}

std::string trimAscii(const std::string& value) {
    std::size_t begin = 0;
    while (begin < value.size() &&
           (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r' ||
            value[begin] == '\n')) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin &&
           (value[end - 1] == ' ' || value[end - 1] == '\t' || value[end - 1] == '\r' ||
            value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::size_t utf8CharacterCount(const std::string& value) {
    std::size_t count = 0;
    for (const unsigned char ch : value) {
        if ((ch & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

}  // namespace

FriendService::FriendService(MysqlConnection& database, SessionManager& sessions)
    : database_(database), sessions_(sessions) {}

FriendResult FriendService::handle(const Frame& request, const RoomMember& caller) {
    if (caller.user_id == 0) {
        return FriendResult{makeError(request.header.seq, 1002, "not authenticated")};
    }
    switch (request.header.cmd) {
        case kCmdFriendRequest:
            return handleRequest(request, caller);
        case kCmdFriendRespond:
            return handleRespond(request, caller);
        case kCmdFriendList:
            return handleList(request, caller);
        case kCmdFriendRequests:
            return handleRequests(request, caller);
        case kCmdFriendRemove:
            return handleRemove(request, caller);
        case kCmdFriendSetRemark:
            return handleSetRemark(request, caller);
        default:
            return FriendResult{makeError(request.header.seq, 1001, "unknown command")};
    }
}

FriendResult FriendService::handleRequest(const Frame& request,
                                          const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint64_t target_id = 0;
    if (!reader.getU64(target_id) || target_id == 0 || target_id == caller.user_id) {
        return FriendResult{makeError(request.header.seq, 1001, "invalid friend target")};
    }

    UserRecord target_user;
    bool found = false;
    std::string error;
    if (!database_.findUserById(target_id, target_user, found, error)) {
        logMessage(LogLevel::kError, "friend", "find target failed: " + error);
        return FriendResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!found || target_user.status != 0) {
        return FriendResult{makeError(request.header.seq, 1008, "user not found")};
    }

    bool already_pending = false;
    bool reverse_pending = false;
    if (!database_.createFriendRequest(caller.user_id, target_id, already_pending,
                                       reverse_pending, error)) {
        if (error == "already friends") {
            return FriendResult{makeError(request.header.seq, 1012, "already friends")};
        }
        logMessage(LogLevel::kError, "friend", "create request failed: " + error);
        return FriendResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (already_pending) {
        return FriendResult{makeError(request.header.seq, 1013,
                                      "friend request already sent")};
    }
    if (reverse_pending) {
        return FriendResult{makeError(request.header.seq, 1014,
                                      "incoming friend request pending")};
    }

    FriendResult result;
    result.response = makeSuccess(kCmdFriendRequest, request.header.seq);

    UserRecord requester;
    bool requester_found = false;
    if (!database_.findUserById(caller.user_id, requester, requester_found, error)) {
        return result;
    }
    if (!requester_found) {
        return result;
    }
    if (sessions_.isOnline(target_id)) {
        Frame push;
        push.header.cmd = kCmdFriendRequestPush;
        push.header.seq = 0;
        ByteWriter writer;
        writer.putU64(caller.user_id);
        writer.putString(requester.username);
        writer.putString(requester.nickname);
        push.body = writer.data();
        attachPush(result, target_id, push);
    }
    return result;
}

FriendResult FriendService::handleRespond(const Frame& request,
                                          const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint64_t requester_id = 0;
    uint8_t accept = 0;
    if (!reader.getU64(requester_id) || !reader.getU8(accept) || requester_id == 0 ||
        requester_id == caller.user_id) {
        return FriendResult{makeError(request.header.seq, 1001, "invalid friend response")};
    }

    std::string error;
    if (!database_.respondFriendRequest(requester_id, caller.user_id, accept != 0, error)) {
        if (error == "friend request is not pending") {
            return FriendResult{makeError(request.header.seq, 1015,
                                          "friend request is not pending")};
        }
        logMessage(LogLevel::kError, "friend", "respond request failed: " + error);
        return FriendResult{makeError(request.header.seq, 2001, "internal error")};
    }

    FriendResult result;
    result.response = makeSuccess(kCmdFriendRespond, request.header.seq);
    if (accept == 0) {
        return result;
    }

    UserRecord requester;
    UserRecord responder;
    bool requester_found = false;
    bool responder_found = false;
    if (!database_.findUserById(requester_id, requester, requester_found, error) ||
        !requester_found) {
        return result;
    }
    if (!database_.findUserById(caller.user_id, responder, responder_found, error) ||
        !responder_found) {
        return result;
    }
    if (sessions_.isOnline(requester_id)) {
        Frame push;
        push.header.cmd = kCmdFriendAcceptPush;
        push.header.seq = 0;
        ByteWriter writer;
        writer.putU64(caller.user_id);
        writer.putString(responder.username);
        writer.putString(responder.nickname);
        writer.putU8(sessions_.isOnline(caller.user_id) ? 1 : 0);
        push.body = writer.data();
        attachPush(result, requester_id, push);
    }
    return result;
}

FriendResult FriendService::handleList(const Frame& request,
                                       const RoomMember& caller) {
    std::vector<UserSearchRow> friends;
    std::string error;
    if (!database_.listFriends(caller.user_id, friends, error)) {
        logMessage(LogLevel::kError, "friend", "list friends failed: " + error);
        return FriendResult{makeError(request.header.seq, 2001, "internal error")};
    }
    FriendResult result;
    result.response = makeSuccess(kCmdFriendList, request.header.seq);
    ByteWriter writer;
    writer.putU16(static_cast<uint16_t>(friends.size()));
    for (const UserSearchRow& friend_row : friends) {
        writer.putU64(friend_row.id);
        writer.putString(friend_row.username);
        writer.putString(friend_row.nickname);
        writer.putU8(sessions_.isOnline(friend_row.id) ? 1 : 0);
        writer.putString(friend_row.remark);
        writer.putU32(friend_row.avatar_version);
    }
    result.response.body = writer.data();
    return result;
}

FriendResult FriendService::handleRequests(const Frame& request,
                                           const RoomMember& caller) {
    std::vector<FriendRequestRow> requests;
    std::string error;
    if (!database_.listFriendRequests(caller.user_id, requests, error)) {
        logMessage(LogLevel::kError, "friend", "list requests failed: " + error);
        return FriendResult{makeError(request.header.seq, 2001, "internal error")};
    }
    FriendResult result;
    result.response = makeSuccess(kCmdFriendRequests, request.header.seq);
    ByteWriter writer;
    writer.putU16(static_cast<uint16_t>(requests.size()));
    for (const FriendRequestRow& row : requests) {
        writer.putU64(row.id);
        writer.putString(row.username);
        writer.putString(row.nickname);
        writer.putString(row.created_at);
    }
    result.response.body = writer.data();
    return result;
}

FriendResult FriendService::handleRemove(const Frame& request,
                                         const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint64_t friend_id = 0;
    if (!reader.getU64(friend_id) || friend_id == 0 || friend_id == caller.user_id) {
        return FriendResult{makeError(request.header.seq, 1001, "invalid friend target")};
    }
    std::string error;
    if (!database_.removeFriend(caller.user_id, friend_id, error)) {
        logMessage(LogLevel::kError, "friend", "remove friend failed: " + error);
        return FriendResult{makeError(request.header.seq, 2001, "internal error")};
    }
    FriendResult result;
    result.response = makeSuccess(kCmdFriendRemove, request.header.seq);
    return result;
}

FriendResult FriendService::handleSetRemark(const Frame& request,
                                            const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint64_t friend_id = 0;
    std::string remark;
    if (!reader.getU64(friend_id) || !reader.getString(remark) ||
        friend_id == 0 || friend_id == caller.user_id) {
        return FriendResult{makeError(request.header.seq, 1001, "invalid remark")};
    }
    remark = trimAscii(remark);
    if (utf8CharacterCount(remark) > 32) {
        return FriendResult{makeError(request.header.seq, 1001, "invalid remark")};
    }
    std::string error;
    if (!database_.setFriendRemark(caller.user_id, friend_id, remark, error)) {
        if (error == "not friends") {
            return FriendResult{makeError(request.header.seq, 1016, "not friends")};
        }
        logMessage(LogLevel::kError, "friend", "set remark failed: " + error);
        return FriendResult{makeError(request.header.seq, 2001, "internal error")};
    }
    FriendResult result;
    result.response = makeSuccess(kCmdFriendSetRemark, request.header.seq);
    return result;
}

}  // namespace chatbox
