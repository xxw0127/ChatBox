#ifndef CHATBOX_SERVER_HISTORY_HISTORY_SERVICE_H
#define CHATBOX_SERVER_HISTORY_HISTORY_SERVICE_H

#include "common/protocol.h"
#include "common/session_protocol.h"
#include "server/db/mysql_connection.h"
#include "server/room/room_manager.h"

namespace chatbox {

struct HistoryResult {
    Frame response;
    bool notify_self = false;
    ReadStateEvent read_state;
};

class HistoryService {
public:
    HistoryService(MysqlConnection& database, RoomManager& rooms);

    HistoryResult handle(const Frame& request, const RoomMember& caller);

private:
    Frame handleHistory(const Frame& request, const RoomMember& caller,
                        bool& notify_self, ReadStateEvent& read_state);
    Frame handleSearch(const Frame& request, const RoomMember& caller);

    MysqlConnection& database_;
    RoomManager& rooms_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_HISTORY_HISTORY_SERVICE_H
