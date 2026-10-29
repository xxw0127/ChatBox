#ifndef CHATBOX_SERVER_PRIVATE_PRIVATE_SERVICE_H
#define CHATBOX_SERVER_PRIVATE_PRIVATE_SERVICE_H

#include <string>
#include <utility>

#include "common/protocol.h"
#include "common/session_protocol.h"
#include "server/auth/session_manager.h"
#include "server/db/mysql_connection.h"
#include "server/room/room_manager.h"

namespace chatbox {

struct PrivateResult {
    Frame response;
    bool push = false;
    uint64_t target_user_id = 0;
    std::string push_frame;
    bool notify_self = false;
    ReadStateEvent read_state;
    std::string echo_frame;

    PrivateResult() = default;
    explicit PrivateResult(Frame frame) : response(std::move(frame)) {}
};

class PrivateService {
public:
    PrivateService(MysqlConnection& database, SessionManager& sessions);

    PrivateResult handle(const Frame& request, const RoomMember& caller);

private:
    PrivateResult handlePrivateSend(const Frame& request, const RoomMember& caller);
    PrivateResult handleConversations(const Frame& request, const RoomMember& caller);
    PrivateResult handleMarkRead(const Frame& request, const RoomMember& caller);
    PrivateResult handleUserSearch(const Frame& request, const RoomMember& caller);
    PrivateResult handleRecall(const Frame& request, const RoomMember& caller);

    MysqlConnection& database_;
    SessionManager& sessions_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_PRIVATE_PRIVATE_SERVICE_H
