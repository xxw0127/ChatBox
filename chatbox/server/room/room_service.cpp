#include "server/room/room_service.h"

#include "server/group/group_rules.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <limits>
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

bool readRoomId(ByteReader& reader, int& room_id) {
    uint32_t value = 0;
    if (!reader.getU32(value) || value == 0 ||
        value > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    room_id = static_cast<int>(value);
    return true;
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

RoomService::RoomService(MysqlConnection& database, RoomManager& rooms)
    : database_(database), rooms_(rooms) {}

Frame makeRoomMembersFrame(int room_id, const std::vector<RoomMember>& members) {
    Frame frame;
    frame.header.flags = kFlagResponse;
    frame.header.cmd = kCmdRoomMembers;
    ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    // 0.2.5 only needs presence; cap the legacy frame well below 64 KiB.
    const std::size_t count = std::min<std::size_t>(members.size(), 200);
    writer.putU16(static_cast<uint16_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
        writer.putU64(members[i].user_id);
        writer.putString(members[i].nickname);
    }
    frame.body = writer.data();
    return frame;
}

RoomResult RoomService::handle(const Frame& request, const RoomMember& caller) {
    if (caller.user_id == 0) {
        return RoomResult{makeError(request.header.seq, 1002, "not authenticated")};
    }

    switch (request.header.cmd) {
        case kCmdRoomList:
            return handleRoomList(request, caller);
        case kCmdJoinRoom:
            return handleJoinRoom(request, caller);
        case kCmdLeaveRoom:
            return handleLeaveRoom(request, caller);
        case kCmdRoomMembers:
            return handleRoomMembers(request, caller);
        case kCmdChatSend:
            return handleChatSend(request, caller);
        case kCmdMessageRecall:
            return handleRecall(request, caller);
        default:
            return RoomResult{makeError(request.header.seq, 1001, "unknown command")};
    }
}

RoomResult RoomService::handleRoomList(const Frame& request,
                                       const RoomMember& caller) {
    std::string error;
    GroupInfo group;
    bool found = false;
    if (!database_.findGroupById(static_cast<uint64_t>(defaultGroupId()), group,
                                 found, error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    bool member = false;
    if (found && !database_.isRoomMember(static_cast<uint64_t>(defaultGroupId()),
                                         caller.user_id, member, error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    const bool visible = found && member && group.status == GroupStatus::kActive;

    RoomResult result;
    result.response = makeSuccess(kCmdRoomList, request.header.seq);
    ByteWriter writer;
    writer.putU16(visible ? 1 : 0);
    if (visible) {
        writer.putU32(static_cast<uint32_t>(group.room_id));
        writer.putString(group.name);
        // 0.2.5 renders this field as capacity. The default hall is unlimited
        // in the new model; keep its legacy display value.
        writer.putU32(1000);
        writer.putU32(static_cast<uint32_t>(
            rooms_.onlineCount(static_cast<int>(group.room_id))));
    }
    result.response.body = writer.data();
    return result;
}

RoomResult RoomService::handleJoinRoom(const Frame& request, const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    int room_id = 0;
    if (!readRoomId(reader, room_id)) {
        return RoomResult{makeError(request.header.seq, 1001, "invalid room id")};
    }

    std::string error;
    GroupInfo group;
    bool found = false;
    if (!database_.findGroupById(static_cast<uint64_t>(room_id), group, found,
                                 error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!found || group.status != GroupStatus::kActive) {
        return RoomResult{makeError(request.header.seq, 1006, "room not found")};
    }
    bool member = false;
    if (!database_.isRoomMember(room_id, caller.user_id, member, error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!member) {
        return RoomResult{makeError(request.header.seq, 1007, "not a group member")};
    }
    if (!rooms_.join(room_id, caller, error)) {
        return RoomResult{makeError(request.header.seq, 1006, error)};
    }

    RoomResult result;
    result.response = makeSuccess(kCmdJoinRoom, request.header.seq);
    std::vector<RoomMember> members;
    std::vector<RoomMember> targets;
    if (rooms_.snapshot(room_id, members) && rooms_.targets(room_id, targets)) {
        result.broadcast = true;
        result.targets = std::move(targets);
        result.broadcast_frame = encodeFrame(makeRoomMembersFrame(room_id, members));
    }
    return result;
}

RoomResult RoomService::handleLeaveRoom(const Frame& request, const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    int room_id = 0;
    if (!readRoomId(reader, room_id)) {
        return RoomResult{makeError(request.header.seq, 1001, "invalid room id")};
    }
    if (!rooms_.leave(room_id, caller.user_id)) {
        return RoomResult{makeError(request.header.seq, 1007, "not in room")};
    }

    RoomResult result;
    result.response = makeSuccess(kCmdLeaveRoom, request.header.seq);
    std::vector<RoomMember> members;
    std::vector<RoomMember> targets;
    if (rooms_.snapshot(room_id, members) && rooms_.targets(room_id, targets) &&
        !members.empty()) {
        result.broadcast = true;
        result.targets = std::move(targets);
        result.broadcast_frame = encodeFrame(makeRoomMembersFrame(room_id, members));
    }
    return result;
}

RoomResult RoomService::handleRoomMembers(const Frame& request, const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    int room_id = 0;
    if (!readRoomId(reader, room_id)) {
        return RoomResult{makeError(request.header.seq, 1001, "invalid room id")};
    }
    std::string error;
    GroupInfo group;
    bool found = false;
    if (!database_.findGroupById(static_cast<uint64_t>(room_id), group, found,
                                 error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    bool member = false;
    if (found && !database_.isRoomMember(room_id, caller.user_id, member, error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!found || !member || group.status != GroupStatus::kActive) {
        return RoomResult{makeError(request.header.seq, 1006, "room not found")};
    }

    std::vector<RoomMember> members;
    if (!rooms_.snapshot(room_id, members)) {
        return RoomResult{makeError(request.header.seq, 1006, "room not found")};
    }
    RoomResult result;
    result.response = makeRoomMembersFrame(room_id, members);
    result.response.header.seq = request.header.seq;
    return result;
}

RoomResult RoomService::handleChatSend(const Frame& request, const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    int room_id = 0;
    std::string client_msg_id;
    std::string content;
    if (!readRoomId(reader, room_id) || !reader.getString(client_msg_id) ||
        !reader.getString(content)) {
        return RoomResult{makeError(request.header.seq, 1001, "invalid chat body")};
    }
    uint64_t reply_to_id = 0;
    std::string reply_sender_name;
    std::string reply_content;
    if (reader.remaining() >= 8) {
        if (!reader.getU64(reply_to_id)) {
            return RoomResult{makeError(request.header.seq, 1001, "invalid reply body")};
        }
        if (reader.remaining() >= 2 && !reader.getString(reply_sender_name)) {
            return RoomResult{makeError(request.header.seq, 1001, "invalid reply body")};
        }
        if (reader.remaining() >= 2 && !reader.getString(reply_content)) {
            return RoomResult{makeError(request.header.seq, 1001, "invalid reply body")};
        }
    }
    if (reply_to_id == 0) {
        reply_sender_name.clear();
        reply_content.clear();
    }
    if (!validClientMessageId(client_msg_id)) {
        return RoomResult{makeError(request.header.seq, 1001, "invalid client_msg_id")};
    }
    if (content.empty() || content.size() > 2000) {
        return RoomResult{makeError(request.header.seq, 1011, "message length invalid")};
    }
    if (chatbox::utf8CodePointCount(reply_sender_name) > 32 ||
        reply_content.size() > 2000) {
        return RoomResult{makeError(request.header.seq, 1001, "invalid reply content")};
    }
    bool member = false;
    std::string membership_error;
    if (!database_.isRoomMember(room_id, caller.user_id, member,
                                membership_error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!member) {
        return RoomResult{makeError(request.header.seq, 1007, "not a group member")};
    }
    GroupInfo group;
    bool group_found = false;
    if (!database_.findGroupById(static_cast<uint64_t>(room_id), group,
                                 group_found, membership_error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!group_found || group.status != GroupStatus::kActive) {
        return RoomResult{makeError(request.header.seq, 1006, "room not found")};
    }

    const std::string created_at = utcNow();
    uint64_t message_id = 0;
    bool duplicate = false;
    std::string error;
    if (!database_.insertMessage(room_id, caller.user_id, client_msg_id, content,
                                 reply_to_id, reply_sender_name, reply_content, created_at,
                                 message_id, duplicate, error)) {
        logMessage(LogLevel::kError, "room", "insert message failed: " + error);
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }

    RoomResult result;
    result.response = makeSuccess(kCmdChatSendResp, request.header.seq);
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

    Frame push;
    push.header.cmd = kCmdChatPush;
    push.header.seq = 0;
    ByteWriter push_writer;
    push_writer.putU32(static_cast<uint32_t>(room_id));
    push_writer.putU64(message_id);
    push_writer.putU64(caller.user_id);
    push_writer.putString(caller.nickname);
    push_writer.putString(content);
    push_writer.putString(created_at);
    push_writer.putU64(reply_to_id);
    push_writer.putString(reply_sender_name);
    push_writer.putString(reply_content);
    push.body = push_writer.data();

    if (!rooms_.snapshot(room_id, result.targets)) {
        result.broadcast = false;
        return result;
    }
    result.broadcast = true;
    result.broadcast_frame = encodeFrame(push);
    return result;
}

RoomResult RoomService::handleRecall(const Frame& request, const RoomMember& caller) {
    ByteReader reader(request.body.data(), request.body.size());
    uint8_t kind = 0;
    uint64_t target_id = 0;
    uint64_t message_id = 0;
    if (!reader.getU8(kind) || !reader.getU64(target_id) || !reader.getU64(message_id) ||
        kind != 1) {
        return RoomResult{makeError(request.header.seq, 1001, "invalid recall body")};
    }
    const int room_id = static_cast<int>(target_id);
    bool member = false;
    std::string membership_error;
    if (!database_.isRoomMember(room_id, caller.user_id, member,
                                membership_error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!member) {
        return RoomResult{makeError(request.header.seq, 1007, "not a group member")};
    }
    GroupInfo group;
    bool group_found = false;
    if (!database_.findGroupById(static_cast<uint64_t>(room_id), group,
                                 group_found, membership_error)) {
        return RoomResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!group_found || group.status != GroupStatus::kActive) {
        return RoomResult{makeError(request.header.seq, 1006, "room not found")};
    }
    std::string error;
    if (!database_.recallRoomMessage(room_id, message_id, caller.user_id, error)) {
        return RoomResult{makeError(request.header.seq, 1006, "message cannot be recalled")};
    }

    Frame recall;
    recall.header.cmd = kCmdMessageRecall;
    recall.header.seq = 0;
    ByteWriter writer;
    writer.putU8(1);
    writer.putU64(target_id);
    writer.putU64(message_id);
    recall.body = writer.data();

    RoomResult result;
    result.response = makeSuccess(kCmdMessageRecall, request.header.seq);
    result.response.body = recall.body;
    std::vector<RoomMember> members;
    if (rooms_.snapshot(room_id, members)) {
        for (const RoomMember& member : members) {
            if (member.user_id != caller.user_id) {
                result.targets.push_back(member);
            }
        }
    }
    result.broadcast = !result.targets.empty();
    result.broadcast_frame = encodeFrame(recall);
    return result;
}

}  // namespace chatbox
