#ifndef CHATBOX_SERVER_SYNC_SYNC_SERVICE_H
#define CHATBOX_SERVER_SYNC_SYNC_SERVICE_H

#include "common/protocol.h"
#include "server/db/mysql_connection.h"
#include "server/room/room_manager.h"

namespace chatbox {

class SyncService {
public:
    SyncService(MysqlConnection& database, RoomManager& rooms);

    Frame handle(const Frame& request, const RoomMember& caller);

private:
    MysqlConnection& database_;
    RoomManager& rooms_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_SYNC_SYNC_SERVICE_H
