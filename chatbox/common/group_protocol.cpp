#include "common/group_protocol.h"

#include <algorithm>

#include "common/protocol.h"

namespace chatbox {

namespace {

void putUtf8Limited(ByteWriter& writer, const std::string& value,
                    std::size_t max_bytes) {
    std::size_t length = std::min(max_bytes, value.size());
    while (length > 0 && length < value.size() &&
           (static_cast<unsigned char>(value[length]) & 0xC0) == 0x80) {
        --length;
    }
    writer.putString(value.substr(0, length));
}

void putGroupInfo(ByteWriter& writer, const GroupInfo& group) {
    writer.putU32(static_cast<uint32_t>(group.room_id));
    writer.putString(group.name);
    writer.putString(group.group_no);
    writer.putU64(group.owner_id);
    writer.putU32(group.member_count);
    writer.putU8(static_cast<uint8_t>(group.role));
    writer.putU32(group.unread);
    writer.putU8(group.is_default ? 1 : 0);
    writer.putU8(static_cast<uint8_t>(group.status));
}

bool getGroupInfo(ByteReader& reader, GroupInfo& group) {
    uint32_t room_id = 0;
    uint64_t owner_id = 0;
    uint32_t member_count = 0;
    uint32_t unread = 0;
    uint8_t role = 0;
    uint8_t is_default = 0;
    uint8_t status = 0;
    if (!reader.getU32(room_id) || !reader.getString(group.name) ||
        !reader.getString(group.group_no) || !reader.getU64(owner_id) ||
        !reader.getU32(member_count) || !reader.getU8(role) ||
        !reader.getU32(unread) || !reader.getU8(is_default) ||
        !reader.getU8(status)) {
        return false;
    }
    group.room_id = room_id;
    group.owner_id = owner_id;
    group.member_count = member_count;
    group.role = static_cast<GroupRole>(role);
    group.unread = unread;
    group.is_default = is_default != 0;
    group.status = static_cast<GroupStatus>(status);
    return true;
}

}  // namespace

std::string encodeGroupInfo(const GroupInfo& group) {
    ByteWriter writer;
    putGroupInfo(writer, group);
    return writer.data();
}

bool decodeGroupInfo(const std::string& body, GroupInfo& group) {
    ByteReader reader(body.data(), body.size());
    return getGroupInfo(reader, group);
}

std::string encodeGroupList(const std::vector<GroupInfo>& groups) {
    ByteWriter writer;
    writer.putU16(static_cast<uint16_t>(groups.size()));
    for (const GroupInfo& group : groups) {
        putGroupInfo(writer, group);
    }
    return writer.data();
}

bool decodeGroupList(const std::string& body, std::vector<GroupInfo>& groups) {
    ByteReader reader(body.data(), body.size());
    uint16_t count = 0;
    if (!reader.getU16(count)) {
        return false;
    }
    groups.clear();
    groups.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        GroupInfo group;
        if (!getGroupInfo(reader, group)) {
            return false;
        }
        groups.push_back(std::move(group));
    }
    return true;
}

std::string encodeGroupMembers(uint32_t room_id,
                               const std::vector<GroupMemberInfo>& members) {
    ByteWriter writer;
    writer.putU32(room_id);
    writer.putU16(static_cast<uint16_t>(members.size()));
    for (const GroupMemberInfo& member : members) {
        writer.putU64(member.user_id);
        // 500 members must fit the 64 KiB inbound frame; keep the common
        // nickname short enough while preserving UTF-8 boundaries.
        putUtf8Limited(writer, member.nickname, 96);
        writer.putU8(static_cast<uint8_t>(member.role));
        writer.putU8(member.online ? 1 : 0);
    }
    return writer.data();
}

bool decodeGroupMembers(const std::string& body, uint32_t& room_id,
                        std::vector<GroupMemberInfo>& members) {
    ByteReader reader(body.data(), body.size());
    uint16_t count = 0;
    if (!reader.getU32(room_id) || !reader.getU16(count)) {
        return false;
    }
    members.clear();
    members.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        GroupMemberInfo member;
        uint8_t role = 0;
        uint8_t online = 0;
        if (!reader.getU64(member.user_id) || !reader.getString(member.nickname) ||
            !reader.getU8(role) || !reader.getU8(online)) {
            return false;
        }
        member.role = static_cast<GroupRole>(role);
        member.online = online != 0;
        members.push_back(std::move(member));
    }
    return true;
}

std::string encodeGroupSearch(const GroupInfo& group, uint8_t membership) {
    ByteWriter writer;
    writer.putU8(1);
    putGroupInfo(writer, group);
    writer.putU8(membership);
    return writer.data();
}

std::string encodeGroupSearchNotFound() {
    ByteWriter writer;
    writer.putU8(0);
    return writer.data();
}

bool decodeGroupSearch(const std::string& body, GroupInfo& group,
                       uint8_t& membership) {
    ByteReader reader(body.data(), body.size());
    uint8_t found = 0;
    if (!reader.getU8(found)) {
        return false;
    }
    if (found == 0) {
        group = GroupInfo{};
        membership = 0;
        return true;
    }
    return getGroupInfo(reader, group) && reader.getU8(membership);
}

std::string encodeGroupRequests(uint32_t room_id,
                                const std::vector<GroupRequestInfo>& requests) {
    ByteWriter writer;
    writer.putU32(room_id);
    writer.putU16(static_cast<uint16_t>(requests.size()));
    for (const GroupRequestInfo& request : requests) {
        writer.putU64(request.request_id);
        writer.putU32(static_cast<uint32_t>(request.room_id));
        writer.putU64(request.user_id);
        writer.putString(request.nickname);
        writer.putString(request.room_name);
        writer.putString(request.message);
        writer.putU8(static_cast<uint8_t>(request.status));
        writer.putString(request.created_at);
    }
    return writer.data();
}

bool decodeGroupRequests(const std::string& body, uint32_t& room_id,
                         std::vector<GroupRequestInfo>& requests) {
    ByteReader reader(body.data(), body.size());
    uint16_t count = 0;
    if (!reader.getU32(room_id) || !reader.getU16(count)) {
        return false;
    }
    requests.clear();
    requests.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        GroupRequestInfo request;
        uint32_t request_room = 0;
        uint8_t status = 0;
        if (!reader.getU64(request.request_id) ||
            !reader.getU32(request_room) || !reader.getU64(request.user_id) ||
            !reader.getString(request.nickname) ||
            !reader.getString(request.room_name) ||
            !reader.getString(request.message) || !reader.getU8(status) ||
            !reader.getString(request.created_at)) {
            return false;
        }
        request.room_id = request_room;
        request.status = status;
        requests.push_back(std::move(request));
    }
    return true;
}

bool decodeGroupCreateRequest(const std::string& body, std::string& name) {
    ByteReader reader(body.data(), body.size());
    return reader.getString(name);
}

bool decodeGroupSearchRequest(const std::string& body, std::string& group_no) {
    ByteReader reader(body.data(), body.size());
    return reader.getString(group_no);
}

bool decodeGroupJoinRequest(const std::string& body, std::string& group_no,
                            std::string& message) {
    ByteReader reader(body.data(), body.size());
    return reader.getString(group_no) && reader.getString(message);
}

bool decodeGroupRespondRequest(const std::string& body, uint64_t& request_id,
                               bool& accept) {
    ByteReader reader(body.data(), body.size());
    uint8_t accepted = 0;
    if (!reader.getU64(request_id) || !reader.getU8(accepted)) {
        return false;
    }
    accept = accepted != 0;
    return true;
}

bool decodeGroupInviteRequest(const std::string& body, uint32_t& room_id,
                              uint64_t& invitee_id) {
    ByteReader reader(body.data(), body.size());
    uint64_t invitee = 0;
    if (!reader.getU32(room_id) || !reader.getU64(invitee)) {
        return false;
    }
    invitee_id = invitee;
    return true;
}

bool decodeGroupInviteRespond(const std::string& body, uint64_t& invite_id,
                              bool& accept) {
    ByteReader reader(body.data(), body.size());
    uint8_t accepted = 0;
    if (!reader.getU64(invite_id) || !reader.getU8(accepted)) {
        return false;
    }
    accept = accepted != 0;
    return true;
}

bool decodeGroupRoomRequest(const std::string& body, uint32_t& room_id) {
    ByteReader reader(body.data(), body.size());
    return reader.getU32(room_id);
}

bool decodeGroupUpdateRequest(const std::string& body, uint32_t& room_id,
                              std::string& name) {
    ByteReader reader(body.data(), body.size());
    return reader.getU32(room_id) && reader.getString(name);
}

bool decodeGroupMemberUpdate(const std::string& body, uint32_t& room_id,
                             uint64_t& user_id, uint8_t& action) {
    ByteReader reader(body.data(), body.size());
    uint64_t user = 0;
    uint8_t act = 0;
    if (!reader.getU32(room_id) || !reader.getU64(user) || !reader.getU8(act)) {
        return false;
    }
    user_id = user;
    action = act;
    return true;
}

bool decodeGroupMarkRead(const std::string& body, uint32_t& room_id,
                         uint64_t& message_id) {
    ByteReader reader(body.data(), body.size());
    uint64_t message = 0;
    if (!reader.getU32(room_id) || !reader.getU64(message)) {
        return false;
    }
    message_id = message;
    return true;
}

std::string encodeGroupIdResponse(uint64_t id) {
    ByteWriter writer;
    writer.putU64(id);
    return writer.data();
}

std::string encodeGroupPush(uint8_t event, uint32_t room_id, uint64_t user_id,
                            uint8_t role) {
    ByteWriter writer;
    writer.putU8(event);
    writer.putU32(room_id);
    writer.putU64(user_id);
    writer.putU8(role);
    return writer.data();
}

}  // namespace chatbox
