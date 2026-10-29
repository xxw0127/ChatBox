#ifndef CHATBOX_SERVER_FRIEND_FRIEND_SERVICE_H
#define CHATBOX_SERVER_FRIEND_FRIEND_SERVICE_H

#include <string>
#include <utility>

#include "common/protocol.h"
#include "server/auth/session_manager.h"
#include "server/db/mysql_connection.h"
#include "server/room/room_manager.h"

namespace chatbox {

struct FriendResult {
    Frame response;
    bool push = false;
    uint64_t target_user_id = 0;
    std::string push_frame;

    FriendResult() = default;
    explicit FriendResult(Frame frame) : response(std::move(frame)) {}
};

class FriendService {
public:
    FriendService(MysqlConnection& database, SessionManager& sessions);

    FriendResult handle(const Frame& request, const RoomMember& caller);

private:
    FriendResult handleRequest(const Frame& request, const RoomMember& caller);
    FriendResult handleRespond(const Frame& request, const RoomMember& caller);
    FriendResult handleList(const Frame& request, const RoomMember& caller);
    FriendResult handleRequests(const Frame& request, const RoomMember& caller);
    FriendResult handleRemove(const Frame& request, const RoomMember& caller);
    FriendResult handleSetRemark(const Frame& request, const RoomMember& caller);

    MysqlConnection& database_;
    SessionManager& sessions_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_FRIEND_FRIEND_SERVICE_H
