#include "server/room/room_manager.h"

#include <algorithm>
#include <utility>

namespace chatbox {

RoomManager::RoomManager(std::vector<RoomInfo> rooms) {
    for (auto& room : rooms) {
        rooms_[room.id].info = std::move(room);
    }
}

std::vector<RoomView> RoomManager::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RoomView> result;
    result.reserve(rooms_.size());
    for (const auto& entry : rooms_) {
        RoomView view;
        view.info = entry.second.info;
        view.online = static_cast<int>(entry.second.members.size());
        result.push_back(std::move(view));
    }
    std::sort(result.begin(), result.end(),
              [](const RoomView& left, const RoomView& right) {
                  return left.info.id < right.info.id;
              });
    return result;
}

bool RoomManager::join(int room_id, const RoomMember& member, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        error = "room not found";
        return false;
    }

    auto member_it = room_it->second.members.find(member.user_id);
    if (member_it != room_it->second.members.end()) {
        const uint64_t old_connection = member_it->second.connection_id;
        member_it->second = member;
        if (old_connection != member.connection_id) {
            removeRoomFromConnection(old_connection, room_id);
            auto& room_ids = connection_rooms_[member.connection_id];
            if (std::find(room_ids.begin(), room_ids.end(), room_id) == room_ids.end()) {
                room_ids.push_back(room_id);
            }
        }
        error.clear();
        return true;
    }

    if (room_it->second.info.max_members > 0 &&
        static_cast<int>(room_it->second.members.size()) >=
            room_it->second.info.max_members) {
        error = "room full";
        return false;
    }

    room_it->second.members.emplace(member.user_id, member);
    connection_rooms_[member.connection_id].push_back(room_id);
    error.clear();
    return true;
}

bool RoomManager::leave(int room_id, uint64_t user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return false;
    }
    auto member_it = room_it->second.members.find(user_id);
    if (member_it == room_it->second.members.end()) {
        return false;
    }
    const uint64_t connection_id = member_it->second.connection_id;
    room_it->second.members.erase(member_it);
    removeRoomFromConnection(connection_id, room_id);
    return true;
}

bool RoomManager::isMember(int room_id, uint64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return false;
    }
    return room_it->second.members.find(user_id) != room_it->second.members.end();
}

bool RoomManager::snapshot(int room_id, std::vector<RoomMember>& members) const {
    std::lock_guard<std::mutex> lock(mutex_);
    members.clear();
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return false;
    }
    members.reserve(room_it->second.members.size());
    for (const auto& entry : room_it->second.members) {
        members.push_back(entry.second);
    }
    return true;
}

int RoomManager::onlineCount(int room_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return 0;
    }
    return static_cast<int>(room_it->second.members.size());
}

void RoomManager::updateNickname(uint64_t user_id, const std::string& nickname) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& room : rooms_) {
        auto member_it = room.second.members.find(user_id);
        if (member_it != room.second.members.end()) {
            member_it->second.nickname = nickname;
        }
    }
}

std::vector<int> RoomManager::removeConnection(uint64_t connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<int> changed_rooms;
    auto index_it = connection_rooms_.find(connection_id);
    if (index_it == connection_rooms_.end()) {
        return changed_rooms;
    }
    changed_rooms = index_it->second;
    connection_rooms_.erase(index_it);
    for (const int room_id : changed_rooms) {
        auto room_it = rooms_.find(room_id);
        if (room_it == rooms_.end()) {
            continue;
        }
        for (auto member_it = room_it->second.members.begin();
             member_it != room_it->second.members.end();) {
            if (member_it->second.connection_id == connection_id) {
                member_it = room_it->second.members.erase(member_it);
            } else {
                ++member_it;
            }
        }
    }
    return changed_rooms;
}

void RoomManager::removeRoomFromConnection(uint64_t connection_id, int room_id) {
    auto index_it = connection_rooms_.find(connection_id);
    if (index_it == connection_rooms_.end()) {
        return;
    }
    auto& room_ids = index_it->second;
    room_ids.erase(std::remove(room_ids.begin(), room_ids.end(), room_id), room_ids.end());
    if (room_ids.empty()) {
        connection_rooms_.erase(index_it);
    }
}

}  // namespace chatbox
