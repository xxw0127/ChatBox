#include "server/group/group_service.h"

#include <algorithm>
#include <unordered_set>

#include "common/group_protocol.h"
#include "common/text_utils.h"
#include "server/group/group_rules.h"
#include "server/util/logging.h"

namespace chatbox {

namespace {

constexpr uint16_t kGroupInvalid = 2002;
constexpr uint16_t kGroupNotMember = 2003;
constexpr uint16_t kGroupPermission = 2004;
constexpr uint16_t kGroupNotFound = 2005;
constexpr uint16_t kGroupConflict = 2006;

constexpr uint8_t kPushCreated = 1;
constexpr uint8_t kPushUpdated = 2;
constexpr uint8_t kPushDissolved = 3;
constexpr uint8_t kPushMemberJoined = 4;
constexpr uint8_t kPushMemberLeft = 5;
constexpr uint8_t kPushRoleChanged = 6;
constexpr uint8_t kPushRequest = 7;
constexpr uint8_t kPushInvite = 8;

GroupResult errorResult(const Frame& request, uint16_t code,
                        const std::string& message) {
    Frame response;
    response.header.flags = kFlagResponse | kFlagError;
    response.header.cmd = request.header.cmd;
    response.header.seq = request.header.seq;
    ByteWriter writer;
    writer.putU16(code);
    writer.putString(message);
    response.body = writer.data();
    GroupResult result;
    result.response = std::move(response);
    return result;
}

GroupResult responseResult(const Frame& request, std::string body = {}) {
    Frame response;
    response.header.flags = kFlagResponse;
    response.header.cmd = request.header.cmd;
    response.header.seq = request.header.seq;
    response.body = std::move(body);
    GroupResult result;
    result.response = std::move(response);
    return result;
}

Frame makePush(uint8_t event, uint32_t room_id, uint64_t user_id,
               uint8_t role = 0) {
    Frame push;
    push.header.cmd = kCmdGroupPush;
    push.body = encodeGroupPush(event, room_id, user_id, role);
    return push;
}

void addRoomBroadcast(GroupResult& result, RoomManager& rooms, int room_id,
                      const Frame& push) {
    std::vector<RoomMember> members;
    if (!rooms.snapshot(room_id, members)) {
        return;
    }
    result.broadcast = true;
    result.broadcast_frame = encodeFrame(push);
    result.targets = std::move(members);
}

void addNotice(GroupResult& result, uint64_t user_id, const Frame& push) {
    GroupNotice notice;
    notice.user_id = user_id;
    notice.frame = encodeFrame(push);
    result.notices.push_back(std::move(notice));
}

}  // namespace

GroupService::GroupService(MysqlConnection& database, RoomManager& rooms)
    : database_(database), rooms_(rooms) {}

GroupResult GroupService::handle(const Frame& request,
                                 const RoomMember& caller) {
    if (caller.user_id == 0) {
        return errorResult(request, 1002, "not logged in");
    }
    switch (request.header.cmd) {
        case kCmdGroupList:
            return handleList(request, caller);
        case kCmdGroupCreate:
            return handleCreate(request, caller);
        case kCmdGroupUpdate:
            return handleUpdate(request, caller);
        case kCmdGroupDisband:
            return handleDisband(request, caller);
        case kCmdGroupSearch:
            return handleSearch(request, caller);
        case kCmdGroupJoinRequest:
            return handleJoinRequest(request, caller);
        case kCmdGroupJoinRespond:
            return handleJoinRespond(request, caller);
        case kCmdGroupInvite:
            return handleInvite(request, caller);
        case kCmdGroupInviteRespond:
            return handleInviteRespond(request, caller);
        case kCmdGroupMemberList:
            return handleMemberList(request, caller);
        case kCmdGroupMemberUpdate:
            return handleMemberUpdate(request, caller);
        case kCmdGroupRequestList:
            return handleRequestList(request, caller);
        case kCmdGroupMarkRead:
            return handleMarkRead(request, caller);
        case kCmdGroupLeave:
            return handleLeave(request, caller);
        default:
            return errorResult(request, kGroupInvalid, "unknown group command");
    }
}

bool GroupService::loadGroupForMember(uint64_t room_id, uint64_t user_id,
                                      GroupInfo& group, GroupRole& role,
                                      std::string& error) {
    bool found = false;
    if (!database_.findGroupById(room_id, group, found, error)) {
        return false;
    }
    if (!found) {
        error = "group not found";
        return false;
    }
    if (group.status != GroupStatus::kActive) {
        error = "group has been dissolved";
        return false;
    }
    bool member = false;
    if (!database_.getRoomMemberRole(room_id, user_id, role, member, error)) {
        return false;
    }
    if (!member) {
        error = "not a group member";
        return false;
    }
    group.role = role;
    if (!database_.unreadCount(room_id, user_id, group.unread, error)) {
        return false;
    }
    return true;
}

bool GroupService::canJoinMoreGroups(uint64_t user_id, std::string& error) {
    std::vector<GroupInfo> groups;
    if (!database_.listGroupsForUser(user_id, groups, error)) {
        return false;
    }
    const auto active_count = std::count_if(
        groups.begin(), groups.end(),
        [](const GroupInfo& group) { return group.status == GroupStatus::kActive; });
    if (active_count >= 20) {
        error = "group limit reached (20)";
        return false;
    }
    return true;
}

GroupResult GroupService::handleList(const Frame& request,
                                     const RoomMember& caller) {
    std::vector<GroupInfo> groups;
    std::string error;
    if (!database_.listGroupsForUser(caller.user_id, groups, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    return responseResult(request, encodeGroupList(groups));
}

GroupResult GroupService::handleCreate(const Frame& request,
                                       const RoomMember& caller) {
    std::string name;
    if (!decodeGroupCreateRequest(request.body, name)) {
        return errorResult(request, kGroupInvalid, "invalid group name payload");
    }
    name = trimAsciiWhitespace(name);
    if (!validGroupName(name)) {
        return errorResult(request, kGroupInvalid, "group name must be 1-32 characters");
    }
    std::vector<GroupInfo> groups;
    std::string error;
    if (!database_.listGroupsForUser(caller.user_id, groups, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    const auto active_count = std::count_if(
        groups.begin(), groups.end(),
        [](const GroupInfo& group) { return group.status == GroupStatus::kActive; });
    if (active_count >= 20) {
        return errorResult(request, kGroupConflict, "group limit reached (20)");
    }
    uint32_t created_today = 0;
    if (!database_.countOwnedGroupsToday(caller.user_id, created_today, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (created_today >= 5) {
        return errorResult(request, kGroupConflict, "daily group creation limit reached");
    }
    GroupInfo group;
    if (!database_.createGroup(name, caller.user_id, group, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    RoomInfo room_info;
    room_info.id = static_cast<int>(group.room_id);
    room_info.name = group.name;
    room_info.max_members = 500;
    std::string room_error;
    if (!rooms_.addRoom(room_info, room_error)) {
        logMessage(LogLevel::kWarn, "group",
                   "new group room registration failed: " + room_error);
    }
    return responseResult(request, encodeGroupInfo(group));
}

GroupResult GroupService::handleUpdate(const Frame& request,
                                       const RoomMember& caller) {
    uint32_t room_id = 0;
    std::string name;
    if (!decodeGroupUpdateRequest(request.body, room_id, name)) {
        return errorResult(request, kGroupInvalid, "invalid group update payload");
    }
    name = trimAsciiWhitespace(name);
    if (!validGroupName(name)) {
        return errorResult(request, kGroupInvalid, "group name must be 1-32 characters");
    }
    GroupInfo group;
    GroupRole role = GroupRole::kMember;
    std::string error;
    if (!loadGroupForMember(room_id, caller.user_id, group, role, error)) {
        return errorResult(request, role == GroupRole::kMember ? kGroupNotMember
                                                              : kGroupPermission,
                           error);
    }
    if (role != GroupRole::kOwner && role != GroupRole::kAdmin) {
        return errorResult(request, kGroupPermission, "admin permission required");
    }
    if (!database_.updateGroupName(room_id, name, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    GroupResult result = responseResult(request);
    addRoomBroadcast(result, rooms_, static_cast<int>(room_id),
                     makePush(kPushUpdated, room_id, caller.user_id,
                              static_cast<uint8_t>(role)));
    return result;
}

GroupResult GroupService::handleDisband(const Frame& request,
                                        const RoomMember& caller) {
    uint32_t room_id = 0;
    if (!decodeGroupRoomRequest(request.body, room_id)) {
        return errorResult(request, kGroupInvalid, "invalid group disband payload");
    }
    GroupInfo group;
    GroupRole role = GroupRole::kMember;
    std::string error;
    if (!loadGroupForMember(room_id, caller.user_id, group, role, error)) {
        return errorResult(request, kGroupNotFound, error);
    }
    if (role != GroupRole::kOwner) {
        return errorResult(request, kGroupPermission, "only the owner can disband");
    }
    if (group.is_default || isDefaultGroupProtected(room_id)) {
        return errorResult(request, kGroupPermission, "default group cannot be disbanded");
    }
    if (!database_.disbandGroup(room_id, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    GroupResult result = responseResult(request);
    addRoomBroadcast(result, rooms_, static_cast<int>(room_id),
                     makePush(kPushDissolved, room_id, caller.user_id,
                              static_cast<uint8_t>(role)));
    rooms_.removeRoom(static_cast<int>(room_id));
    return result;
}

GroupResult GroupService::handleSearch(const Frame& request,
                                       const RoomMember& caller) {
    std::string group_no;
    if (!decodeGroupSearchRequest(request.body, group_no)) {
        return errorResult(request, kGroupInvalid, "invalid group search payload");
    }
    GroupInfo group;
    bool found = false;
    std::string error;
    if (!database_.findGroupByNumber(group_no, group, found, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (!found) {
        return responseResult(request, encodeGroupSearchNotFound());
    }
    bool member = false;
    if (!database_.isRoomMember(group.room_id, caller.user_id, member, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    uint8_t membership = 0;
    if (member) {
        membership = 1;
    } else {
        bool pending = false;
        if (!database_.hasPendingJoinRequest(group.room_id, caller.user_id,
                                             pending, error)) {
            return errorResult(request, kGroupConflict, error);
        }
        membership = pending ? 2 : 0;
    }
    return responseResult(request, encodeGroupSearch(group, membership));
}

GroupResult GroupService::handleJoinRequest(const Frame& request,
                                            const RoomMember& caller) {
    std::string group_no;
    std::string message;
    if (!decodeGroupJoinRequest(request.body, group_no, message)) {
        return errorResult(request, kGroupInvalid, "invalid join request payload");
    }
    if (!validGroupNumber(group_no)) {
        return errorResult(request, kGroupInvalid, "invalid group number");
    }
    GroupInfo group;
    bool found = false;
    std::string error;
    if (!database_.findGroupByNumber(group_no, group, found, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (!found || group.status != GroupStatus::kActive) {
        return errorResult(request, kGroupNotFound, "group not found");
    }
    if (group.is_default) {
        return errorResult(request, kGroupConflict, "default group joins automatically");
    }
    bool member = false;
    if (!database_.isRoomMember(group.room_id, caller.user_id, member, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (member) {
        return errorResult(request, kGroupConflict, "already a member");
    }
    uint32_t requests_today = 0;
    if (!database_.countJoinRequestsToday(caller.user_id, requests_today, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (requests_today >= 20) {
        return errorResult(request, kGroupConflict, "daily join request limit reached");
    }
    uint64_t request_id = 0;
    if (!database_.createJoinRequest(group.room_id, caller.user_id,
                                     trimAsciiWhitespace(message), request_id,
                                     error)) {
        return errorResult(request, kGroupConflict, error);
    }
    GroupResult result = responseResult(request, encodeGroupIdResponse(request_id));
    std::vector<GroupMemberInfo> members;
    if (database_.listRoomMembers(group.room_id, members, error)) {
        const Frame push =
            makePush(kPushRequest, static_cast<uint32_t>(group.room_id),
                     caller.user_id, static_cast<uint8_t>(GroupRole::kMember));
        for (const GroupMemberInfo& member_info : members) {
            if (member_info.role == GroupRole::kOwner ||
                member_info.role == GroupRole::kAdmin) {
                addNotice(result, member_info.user_id, push);
            }
        }
    }
    return result;
}

GroupResult GroupService::handleJoinRespond(const Frame& request,
                                            const RoomMember& caller) {
    uint64_t request_id = 0;
    bool accept = false;
    if (!decodeGroupRespondRequest(request.body, request_id, accept)) {
        return errorResult(request, kGroupInvalid, "invalid join response payload");
    }
    uint64_t room_id = 0;
    uint64_t applicant_id = 0;
    int status = 0;
    bool found = false;
    std::string error;
    if (!database_.findJoinRequest(request_id, room_id, applicant_id, status,
                                   found, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (!found) {
        return errorResult(request, kGroupNotFound, "join request not found");
    }
    GroupInfo group;
    GroupRole role = GroupRole::kMember;
    if (!loadGroupForMember(room_id, caller.user_id, group, role, error)) {
        return errorResult(request, kGroupNotMember, error);
    }
    if (role != GroupRole::kOwner && role != GroupRole::kAdmin) {
        return errorResult(request, kGroupPermission, "admin permission required");
    }
    if (!group.is_default && group.member_count >= 500) {
        return errorResult(request, kGroupConflict, "group is full");
    }
    if (accept && !canJoinMoreGroups(applicant_id, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (!database_.respondJoinRequest(request_id, caller.user_id, accept, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    GroupResult result = responseResult(request);
    if (accept) {
        addRoomBroadcast(result, rooms_, static_cast<int>(room_id),
                         makePush(kPushMemberJoined, room_id, applicant_id,
                                  static_cast<uint8_t>(GroupRole::kMember)));
        addNotice(result, applicant_id,
                  makePush(kPushMemberJoined, room_id, applicant_id,
                           static_cast<uint8_t>(GroupRole::kMember)));
    } else {
        addNotice(result, applicant_id,
                  makePush(kPushRequest, room_id, applicant_id,
                           static_cast<uint8_t>(GroupRole::kMember)));
    }
    return result;
}

GroupResult GroupService::handleInvite(const Frame& request,
                                       const RoomMember& caller) {
    uint32_t room_id = 0;
    uint64_t invitee_id = 0;
    if (!decodeGroupInviteRequest(request.body, room_id, invitee_id)) {
        return errorResult(request, kGroupInvalid, "invalid invite payload");
    }
    if (invitee_id == 0 || invitee_id == caller.user_id) {
        return errorResult(request, kGroupInvalid, "invalid invitee");
    }
    GroupInfo group;
    GroupRole role = GroupRole::kMember;
    std::string error;
    if (!loadGroupForMember(room_id, caller.user_id, group, role, error)) {
        return errorResult(request, kGroupNotMember, error);
    }
    UserRecord invitee;
    bool found = false;
    if (!database_.findUserById(invitee_id, invitee, found, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (!found || invitee.status != 0) {
        return errorResult(request, kGroupNotFound, "user not found");
    }
    bool member = false;
    if (!database_.isRoomMember(room_id, invitee_id, member, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (member) {
        return errorResult(request, kGroupConflict, "user is already a member");
    }
    uint32_t invites_today = 0;
    if (!database_.countInvitesToday(caller.user_id, invites_today, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (invites_today >= 20) {
        return errorResult(request, kGroupConflict, "daily invite limit reached");
    }
    uint64_t invite_id = 0;
    if (!database_.createInvite(room_id, caller.user_id, invitee_id, invite_id,
                                error)) {
        return errorResult(request, kGroupConflict, error);
    }
    GroupResult result = responseResult(request, encodeGroupIdResponse(invite_id));
    addNotice(result, invitee_id,
              makePush(kPushInvite, room_id, caller.user_id,
                       static_cast<uint8_t>(role)));
    return result;
}

GroupResult GroupService::handleInviteRespond(const Frame& request,
                                              const RoomMember& caller) {
    uint64_t invite_id = 0;
    bool accept = false;
    if (!decodeGroupInviteRespond(request.body, invite_id, accept)) {
        return errorResult(request, kGroupInvalid, "invalid invite response payload");
    }
    uint64_t room_id = 0;
    uint64_t invitee_id = 0;
    int status = 0;
    bool found = false;
    std::string error;
    if (!database_.findInvite(invite_id, room_id, invitee_id, status, found,
                              error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (!found || invitee_id != caller.user_id) {
        return errorResult(request, kGroupNotFound, "invite not found");
    }
    // The invitee is not a member yet, so do not use loadGroupForMember here.
    bool member = false;
    if (!database_.isRoomMember(room_id, caller.user_id, member, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (member) {
        return errorResult(request, kGroupConflict, "already a member");
    }
    GroupInfo group;
    bool group_found = false;
    if (!database_.findGroupById(room_id, group, group_found, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (!group_found || group.status != GroupStatus::kActive) {
        return errorResult(request, kGroupNotFound, "group not found");
    }
    if (!group.is_default && group.member_count >= 500) {
        return errorResult(request, kGroupConflict, "group is full");
    }
    if (accept && !canJoinMoreGroups(caller.user_id, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (!database_.respondInvite(invite_id, caller.user_id, accept, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    GroupResult result = responseResult(request);
    if (accept) {
        addRoomBroadcast(result, rooms_, static_cast<int>(room_id),
                         makePush(kPushMemberJoined, room_id, caller.user_id,
                                  static_cast<uint8_t>(GroupRole::kMember)));
    }
    return result;
}

GroupResult GroupService::handleMemberList(const Frame& request,
                                           const RoomMember& caller) {
    uint32_t room_id = 0;
    if (!decodeGroupRoomRequest(request.body, room_id)) {
        return errorResult(request, kGroupInvalid, "invalid member list payload");
    }
    GroupInfo group;
    GroupRole role = GroupRole::kMember;
    std::string error;
    if (!loadGroupForMember(room_id, caller.user_id, group, role, error)) {
        return errorResult(request, kGroupNotMember, error);
    }
    std::vector<GroupMemberInfo> members;
    if (!database_.listRoomMembers(room_id, members, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    std::vector<RoomMember> online;
    rooms_.snapshot(static_cast<int>(room_id), online);
    std::unordered_set<uint64_t> online_ids;
    for (const RoomMember& member : online) {
        online_ids.insert(member.user_id);
    }
    for (GroupMemberInfo& member : members) {
        member.online = online_ids.count(member.user_id) != 0;
    }
    return responseResult(request, encodeGroupMembers(room_id, members));
}

GroupResult GroupService::handleMemberUpdate(const Frame& request,
                                             const RoomMember& caller) {
    uint32_t room_id = 0;
    uint64_t target_id = 0;
    uint8_t action = 0;
    if (!decodeGroupMemberUpdate(request.body, room_id, target_id, action)) {
        return errorResult(request, kGroupInvalid, "invalid member update payload");
    }
    GroupInfo group;
    GroupRole actor_role = GroupRole::kMember;
    std::string error;
    if (!loadGroupForMember(room_id, caller.user_id, group, actor_role, error)) {
        return errorResult(request, kGroupNotMember, error);
    }
    if (target_id == caller.user_id) {
        return errorResult(request, kGroupPermission, "cannot manage yourself");
    }
    GroupRole target_role = GroupRole::kMember;
    bool target_found = false;
    if (!database_.getRoomMemberRole(room_id, target_id, target_role,
                                     target_found, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    if (!target_found) {
        return errorResult(request, kGroupNotFound, "member not found");
    }
    if (action == 1) {
        if (group.is_default || isDefaultGroupProtected(room_id)) {
            return errorResult(request, kGroupPermission,
                               "default group members cannot be removed");
        }
        if (!canManageGroup(actor_role, target_role)) {
            return errorResult(request, kGroupPermission, "cannot remove this member");
        }
        if (!database_.removeRoomMember(room_id, target_id, error)) {
            return errorResult(request, kGroupConflict, error);
        }
        rooms_.leave(static_cast<int>(room_id), target_id);
        GroupResult result = responseResult(request);
        addRoomBroadcast(result, rooms_, static_cast<int>(room_id),
                         makePush(kPushMemberLeft, room_id, target_id,
                                  static_cast<uint8_t>(target_role)));
        addNotice(result, target_id,
                  makePush(kPushMemberLeft, room_id, target_id,
                           static_cast<uint8_t>(target_role)));
        return result;
    }
    if (action == 2 || action == 3) {
        if (actor_role != GroupRole::kOwner) {
            return errorResult(request, kGroupPermission, "owner permission required");
        }
        if (action == 2 && target_role != GroupRole::kMember) {
            return errorResult(request, kGroupConflict, "target is already an admin");
        }
        if (action == 3 && target_role != GroupRole::kAdmin) {
            return errorResult(request, kGroupConflict, "target is not an admin");
        }
        const GroupRole next =
            action == 2 ? GroupRole::kAdmin : GroupRole::kMember;
        if (!database_.setRoomMemberRole(room_id, target_id, next, error)) {
            return errorResult(request, kGroupConflict, error);
        }
        GroupResult result = responseResult(request);
        addRoomBroadcast(result, rooms_, static_cast<int>(room_id),
                         makePush(kPushRoleChanged, room_id, target_id,
                                  static_cast<uint8_t>(next)));
        addNotice(result, target_id,
                  makePush(kPushRoleChanged, room_id, target_id,
                           static_cast<uint8_t>(next)));
        return result;
    }
    if (action == 4) {
        if (actor_role != GroupRole::kOwner) {
            return errorResult(request, kGroupPermission, "owner permission required");
        }
        if (!database_.transferRoomOwner(room_id, target_id, error)) {
            return errorResult(request, kGroupConflict, error);
        }
        GroupResult result = responseResult(request);
        addRoomBroadcast(result, rooms_, static_cast<int>(room_id),
                         makePush(kPushRoleChanged, room_id, target_id,
                                  static_cast<uint8_t>(GroupRole::kOwner)));
        addNotice(result, target_id,
                  makePush(kPushRoleChanged, room_id, target_id,
                           static_cast<uint8_t>(GroupRole::kOwner)));
        return result;
    }
    return errorResult(request, kGroupInvalid, "unknown member action");
}

GroupResult GroupService::handleRequestList(const Frame& request,
                                            const RoomMember& caller) {
    uint32_t room_id = 0;
    if (!decodeGroupRoomRequest(request.body, room_id)) {
        return errorResult(request, kGroupInvalid, "invalid request list payload");
    }
    std::string error;
    if (room_id == 0) {
        std::vector<GroupRequestInfo> invites;
        if (!database_.listInvitesForUser(caller.user_id, invites, error)) {
            return errorResult(request, kGroupConflict, error);
        }
        return responseResult(request, encodeGroupRequests(0, invites));
    }
    GroupInfo group;
    GroupRole role = GroupRole::kMember;
    if (!loadGroupForMember(room_id, caller.user_id, group, role, error)) {
        return errorResult(request, kGroupNotMember, error);
    }
    if (role != GroupRole::kOwner && role != GroupRole::kAdmin) {
        return errorResult(request, kGroupPermission, "admin permission required");
    }
    std::vector<GroupRequestInfo> requests;
    if (!database_.listJoinRequests(room_id, requests, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    return responseResult(request, encodeGroupRequests(room_id, requests));
}

GroupResult GroupService::handleLeave(const Frame& request,
                                      const RoomMember& caller) {
    uint32_t room_id = 0;
    if (!decodeGroupRoomRequest(request.body, room_id)) {
        return errorResult(request, kGroupInvalid, "invalid group leave payload");
    }
    GroupInfo group;
    GroupRole role = GroupRole::kMember;
    std::string error;
    if (!loadGroupForMember(room_id, caller.user_id, group, role, error)) {
        return errorResult(request, kGroupNotMember, error);
    }
    if (group.is_default || isDefaultGroupProtected(room_id)) {
        return errorResult(request, kGroupPermission, "default group cannot be left");
    }
    if (role == GroupRole::kOwner) {
        return errorResult(request, kGroupPermission,
                           "transfer ownership before leaving");
    }
    if (!database_.removeRoomMember(room_id, caller.user_id, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    rooms_.leave(static_cast<int>(room_id), caller.user_id);
    GroupResult result = responseResult(request);
    addRoomBroadcast(result, rooms_, static_cast<int>(room_id),
                     makePush(kPushMemberLeft, room_id, caller.user_id,
                              static_cast<uint8_t>(role)));
    addNotice(result, caller.user_id,
              makePush(kPushMemberLeft, room_id, caller.user_id,
                       static_cast<uint8_t>(role)));
    return result;
}

GroupResult GroupService::handleMarkRead(const Frame& request,
                                         const RoomMember& caller) {
    uint32_t room_id = 0;
    uint64_t message_id = 0;
    if (!decodeGroupMarkRead(request.body, room_id, message_id)) {
        return errorResult(request, kGroupInvalid, "invalid mark read payload");
    }
    GroupInfo group;
    GroupRole role = GroupRole::kMember;
    std::string error;
    if (!loadGroupForMember(room_id, caller.user_id, group, role, error)) {
        return errorResult(request, kGroupNotMember, error);
    }
    if (!database_.markRoomRead(room_id, caller.user_id, message_id, error)) {
        return errorResult(request, kGroupConflict, error);
    }
    return responseResult(request);
}

}  // namespace chatbox
