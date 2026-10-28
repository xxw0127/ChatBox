#ifndef CHATBOX_CLIENT_CORE_GROUP_STORE_H
#define CHATBOX_CLIENT_CORE_GROUP_STORE_H

#include <QHash>
#include <QVector>

#include "core/group_types.h"

namespace chatbox {
namespace client {

class GroupStore {
public:
    void clear();
    void replaceAll(const QVector<GroupInfo>& groups);
    void upsert(const GroupInfo& group);
    const GroupInfo* find(quint64 room_id) const;
    GroupInfo* find(quint64 room_id);
    QVector<GroupInfo> active() const;
    QVector<GroupInfo> dissolved() const;

    void setMembers(quint64 room_id, const QVector<GroupMemberInfo>& members);
    QVector<GroupMemberInfo> members(quint64 room_id) const;
    void setUnread(quint64 room_id, int unread);

private:
    QHash<quint64, GroupInfo> groups_;
    QHash<quint64, QVector<GroupMemberInfo>> members_;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_GROUP_STORE_H
