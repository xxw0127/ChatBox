#include "server/room/room_manager.h"

#include <algorithm>
#include <unordered_set>
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
        view.online = distinctUserCount(entry.second);
        result.push_back(std::move(view));
    }
    std::sort(result.begin(), result.end(),
              [](const RoomView& left, const RoomView& right) {
                  return left.info.id < right.info.id;
              });
    return result;
}

bool RoomManager::addRoom(const RoomInfo& room, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rooms_.find(room.id);
    if (it != rooms_.end()) {
        it->second.info = room;
        error.clear();
        return true;
    }
    RoomState state;
    state.info = room;
    rooms_.emplace(room.id, std::move(state));
    error.clear();
    return true;
}

void RoomManager::removeRoom(int room_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    rooms_.erase(room_id);
    for (auto& entry : connection_rooms_) {
        auto& ids = entry.second;
        ids.erase(std::remove(ids.begin(), ids.end(), room_id), ids.end());
    }
}

bool RoomManager::join(int room_id, const RoomMember& member, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        error = "room not found";
        return false;
    }

    auto existing = room_it->second.members.find(member.connection_id);
    if (existing != room_it->second.members.end()) {
        existing->second = member;
        error.clear();
        return true;
    }

    bool already_member = false;
    for (const auto& entry : room_it->second.members) {
        if (entry.second.user_id == member.user_id) {
            already_member = true;
            break;
        }
    }
    if (!already_member && room_it->second.info.max_members > 0 &&
        distinctUserCount(room_it->second) >= room_it->second.info.max_members) {
        error = "room full";
        return false;
    }

    room_it->second.members.emplace(member.connection_id, member);
    auto& room_ids = connection_rooms_[member.connection_id];
    if (std::find(room_ids.begin(), room_ids.end(), room_id) == room_ids.end()) {
        room_ids.push_back(room_id);
    }
    error.clear();
    return true;
}

bool RoomManager::leave(int room_id, uint64_t user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return false;
    }
    bool removed = false;
    for (auto it = room_it->second.members.begin();
         it != room_it->second.members.end();) {
        if (it->second.user_id != user_id) {
            ++it;
            continue;
        }
        removeRoomFromConnection(it->second.connection_id, room_id);
        it = room_it->second.members.erase(it);
        removed = true;
    }
    return removed;
}

bool RoomManager::isMember(int room_id, uint64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return false;
    }
    for (const auto& entry : room_it->second.members) {
        if (entry.second.user_id == user_id) {
            return true;
        }
    }
    return false;
}

bool RoomManager::snapshot(int room_id, std::vector<RoomMember>& members) const {
    std::lock_guard<std::mutex> lock(mutex_);
    members.clear();
    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        return false;
    }
    std::unordered_set<uint64_t> seen;
    members.reserve(room_it->second.members.size());
    for (const auto& entry : room_it->second.members) {
        if (!seen.insert(entry.second.user_id).second) {
            continue;
        }
        members.push_back(entry.second);
    }
    return true;
}

bool RoomManager::targets(int room_id, std::vector<RoomMember>& members) const {
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
    return distinctUserCount(room_it->second);
}

void RoomManager::updateNickname(uint64_t user_id, const std::string& nickname) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& room : rooms_) {
        for (auto& entry : room.second.members) {
            if (entry.second.user_id == user_id) {
                entry.second.nickname = nickname;
            }
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
        room_it->second.members.erase(connection_id);
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

int RoomManager::distinctUserCount(const RoomState& room) const {
    std::unordered_set<uint64_t> users;
    users.reserve(room.members.size());
    for (const auto& entry : room.members) {
        users.insert(entry.second.user_id);
    }
    return static_cast<int>(users.size());
}

}  // namespace chatbox
