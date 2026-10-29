#ifndef CHATBOX_SERVER_ROOM_ROOM_MANAGER_H
#define CHATBOX_SERVER_ROOM_ROOM_MANAGER_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/net/connection.h"

namespace chatbox {

struct RoomInfo {
    int id = 0;
    std::string name;
    int max_members = 0;
};

struct RoomView {
    RoomInfo info;
    int online = 0;
};

struct RoomMember {
    uint64_t user_id = 0;
    uint64_t connection_id = 0;
    int io_thread_id = -1;
    std::string nickname;
    std::weak_ptr<Connection> connection;
};

class RoomManager {
public:
    explicit RoomManager(std::vector<RoomInfo> rooms);

    std::vector<RoomView> list() const;
    bool addRoom(const RoomInfo& room, std::string& error);
    void removeRoom(int room_id);
    bool join(int room_id, const RoomMember& member, std::string& error);
    bool leave(int room_id, uint64_t user_id);
    bool isMember(int room_id, uint64_t user_id) const;
    // One entry per user, for member-list frames.
    bool snapshot(int room_id, std::vector<RoomMember>& members) const;
    // One entry per connection, for broadcasting to every device.
    bool targets(int room_id, std::vector<RoomMember>& members) const;
    // Number of distinct users, not connections.
    int onlineCount(int room_id) const;
    void updateNickname(uint64_t user_id, const std::string& nickname);
    // Removes every room membership for the connection and returns the room
    // ids that changed, so callers can broadcast a fresh member list.
    std::vector<int> removeConnection(uint64_t connection_id);

private:
    struct RoomState {
        RoomInfo info;
        std::unordered_map<uint64_t, RoomMember> members;  // connection_id -> member
    };

    void removeRoomFromConnection(uint64_t connection_id, int room_id);
    int distinctUserCount(const RoomState& room) const;

    mutable std::mutex mutex_;
    std::unordered_map<int, RoomState> rooms_;
    std::unordered_map<uint64_t, std::vector<int>> connection_rooms_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_ROOM_ROOM_MANAGER_H
