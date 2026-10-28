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
    GroupService(MysqlConnection& database, RoomManager& rooms);

    GroupResult handle(const Frame& request, const RoomMember& caller);

private:
    GroupResult notImplemented(const Frame& request);

    MysqlConnection& database_;
    RoomManager& rooms_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_GROUP_GROUP_SERVICE_H
