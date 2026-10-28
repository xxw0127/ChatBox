#ifndef CHATBOX_SERVER_ROOM_ROOM_SERVICE_H
#define CHATBOX_SERVER_ROOM_ROOM_SERVICE_H

#include <string>
#include <utility>
#include <vector>

#include "common/protocol.h"
#include "server/db/mysql_connection.h"
#include "server/room/room_manager.h"

namespace chatbox {

struct RoomResult {
    Frame response;
    bool broadcast = false;
    std::string broadcast_frame;
    std::vector<RoomMember> targets;

    RoomResult() = default;
    explicit RoomResult(Frame frame) : response(std::move(frame)) {}
};

// Builds the same member-list payload used by ROOM_MEMBERS responses and by
// presence pushes after join/leave/logout/disconnect.
Frame makeRoomMembersFrame(int room_id, const std::vector<RoomMember>& members);

class RoomService {
public:
    RoomService(MysqlConnection& database, RoomManager& rooms);

    RoomResult handle(const Frame& request, const RoomMember& caller);

private:
    RoomResult handleRoomList(const Frame& request, const RoomMember& caller);
    RoomResult handleJoinRoom(const Frame& request, const RoomMember& caller);
    RoomResult handleLeaveRoom(const Frame& request, const RoomMember& caller);
    RoomResult handleRoomMembers(const Frame& request, const RoomMember& caller);
    RoomResult handleChatSend(const Frame& request, const RoomMember& caller);
    RoomResult handleRecall(const Frame& request, const RoomMember& caller);

    MysqlConnection& database_;
    RoomManager& rooms_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_ROOM_ROOM_SERVICE_H
