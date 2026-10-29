#include "core/group_types.h"

namespace chatbox {
namespace client {

namespace {

QString fromStd(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

}  // namespace

QString groupListTitle(const GroupInfo& group) {
    return group.name;
}

GroupInfo groupInfoFromWire(const chatbox::GroupInfo& wire) {
    GroupInfo group;
    group.room_id = wire.room_id;
    group.name = fromStd(wire.name);
    group.group_no = fromStd(wire.group_no);
    group.owner_id = wire.owner_id;
    group.member_count = static_cast<int>(wire.member_count);
    group.role = static_cast<int>(wire.role);
    group.unread = static_cast<int>(wire.unread);
    group.is_default = wire.is_default;
    group.status = static_cast<int>(wire.status);
    return group;
}

GroupMemberInfo groupMemberFromWire(const chatbox::GroupMemberInfo& wire) {
    GroupMemberInfo member;
    member.user_id = wire.user_id;
    member.nickname = fromStd(wire.nickname);
    member.role = static_cast<int>(wire.role);
    member.online = wire.online;
    return member;
}

GroupRequestInfo groupRequestFromWire(const chatbox::GroupRequestInfo& wire) {
    GroupRequestInfo request;
    request.request_id = wire.request_id;
    request.room_id = wire.room_id;
    request.user_id = wire.user_id;
    request.nickname = fromStd(wire.nickname);
    request.room_name = fromStd(wire.room_name);
    request.message = fromStd(wire.message);
    request.status = wire.status;
    request.created_at = fromStd(wire.created_at);
    return request;
}

}  // namespace client
}  // namespace chatbox
