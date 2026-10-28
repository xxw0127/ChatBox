#include "core/group_store.h"

#include <algorithm>

namespace chatbox {
namespace client {

void GroupStore::clear() {
    groups_.clear();
    members_.clear();
}

void GroupStore::replaceAll(const QVector<GroupInfo>& groups) {
    groups_.clear();
    for (const GroupInfo& group : groups) {
        groups_.insert(group.room_id, group);
    }
    for (auto it = members_.begin(); it != members_.end();) {
        if (!groups_.contains(it.key())) {
            it = members_.erase(it);
        } else {
            ++it;
        }
    }
}

void GroupStore::upsert(const GroupInfo& group) {
    groups_.insert(group.room_id, group);
}

const GroupInfo* GroupStore::find(quint64 room_id) const {
    const auto it = groups_.constFind(room_id);
    return it == groups_.constEnd() ? nullptr : &it.value();
}

GroupInfo* GroupStore::find(quint64 room_id) {
    const auto it = groups_.find(room_id);
    return it == groups_.end() ? nullptr : &it.value();
}

QVector<GroupInfo> GroupStore::active() const {
    QVector<GroupInfo> result;
    for (const GroupInfo& group : groups_) {
        if (group.status == 0) {
            result.push_back(group);
        }
    }
    std::sort(result.begin(), result.end(), [](const GroupInfo& a, const GroupInfo& b) {
        if (a.is_default != b.is_default) {
            return a.is_default;
        }
        return a.room_id < b.room_id;
    });
    return result;
}

QVector<GroupInfo> GroupStore::dissolved() const {
    QVector<GroupInfo> result;
    for (const GroupInfo& group : groups_) {
        if (group.status != 0) {
            result.push_back(group);
        }
    }
    std::sort(result.begin(), result.end(), [](const GroupInfo& a, const GroupInfo& b) {
        return a.room_id < b.room_id;
    });
    return result;
}

void GroupStore::setMembers(quint64 room_id,
                            const QVector<GroupMemberInfo>& members) {
    members_.insert(room_id, members);
}

QVector<GroupMemberInfo> GroupStore::members(quint64 room_id) const {
    return members_.value(room_id);
}

void GroupStore::setUnread(quint64 room_id, int unread) {
    GroupInfo* group = find(room_id);
    if (group != nullptr) {
        group->unread = unread;
    }
}

}  // namespace client
}  // namespace chatbox
