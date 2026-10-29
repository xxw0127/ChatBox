#ifndef CHATBOX_SERVER_PROFILE_PROFILE_SERVICE_H
#define CHATBOX_SERVER_PROFILE_PROFILE_SERVICE_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/protocol.h"
#include "server/auth/session_manager.h"
#include "server/db/mysql_connection.h"
#include "server/room/room_manager.h"

namespace chatbox {

struct ProfileResult {
    ProfileResult() = default;
    explicit ProfileResult(Frame value) : response(std::move(value)) {}

    Frame response;
    bool nickname_changed = false;
    std::string new_nickname;
    std::string push_frame;
    std::vector<uint64_t> push_user_ids;
};

class ProfileService {
public:
    ProfileService(MysqlConnection& database, SessionManager& sessions);

    ProfileResult handle(const Frame& request, const RoomMember& caller);

private:
    ProfileResult handleProfileGet(const Frame& request, const RoomMember& caller);
    ProfileResult handleAvatarGet(const Frame& request, const RoomMember& caller);
    ProfileResult handleProfileUpdate(const Frame& request, const RoomMember& caller);

    MysqlConnection& database_;
    SessionManager& sessions_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_PROFILE_PROFILE_SERVICE_H
