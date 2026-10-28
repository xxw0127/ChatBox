#ifndef CHATBOX_SERVER_GROUP_GROUP_SERVICE_H
#define CHATBOX_SERVER_GROUP_GROUP_SERVICE_H

#include <cstdint>
#include <string>
#include <vector>

#include "common/group_types.h"
#include "common/protocol.h"
#include "server/db/mysql_connection.h"
#include "server/room/room_manager.h"

namespace chatbox {

struct GroupNotice {
    uint64_t user_id = 0;
    std::string frame;
};

struct GroupResult {
    Frame response;
    bool broadcast = false;
    std::string broadcast_frame;
    std::vector<RoomMember> targets;
    std::vector<GroupNotice> notices;
};

class GroupService {
public:
    GroupService(MysqlConnection& database, RoomManager& rooms,
                 std::string files_dir);

    GroupResult handle(const Frame& request, const RoomMember& caller);

private:
    void cleanupRoomFiles(uint64_t room_id);
    bool loadGroupForMember(uint64_t room_id, uint64_t user_id, GroupInfo& group,
                            GroupRole& role, std::string& error);
    bool canJoinMoreGroups(uint64_t user_id, std::string& error);
    GroupResult handleList(const Frame& request, const RoomMember& caller);
    GroupResult handleCreate(const Frame& request, const RoomMember& caller);
    GroupResult handleUpdate(const Frame& request, const RoomMember& caller);
    GroupResult handleDisband(const Frame& request, const RoomMember& caller);
    GroupResult handleSearch(const Frame& request, const RoomMember& caller);
    GroupResult handleJoinRequest(const Frame& request, const RoomMember& caller);
    GroupResult handleJoinRespond(const Frame& request, const RoomMember& caller);
    GroupResult handleInvite(const Frame& request, const RoomMember& caller);
    GroupResult handleInviteRespond(const Frame& request, const RoomMember& caller);
    GroupResult handleMemberList(const Frame& request, const RoomMember& caller);
    GroupResult handleMemberUpdate(const Frame& request, const RoomMember& caller);
    GroupResult handleRequestList(const Frame& request, const RoomMember& caller);
    GroupResult handleMarkRead(const Frame& request, const RoomMember& caller);
    GroupResult handleLeave(const Frame& request, const RoomMember& caller);

    MysqlConnection& database_;
    RoomManager& rooms_;
    std::string files_dir_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_GROUP_GROUP_SERVICE_H
