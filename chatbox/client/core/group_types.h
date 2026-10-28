#ifndef CHATBOX_CLIENT_CORE_GROUP_TYPES_H
#define CHATBOX_CLIENT_CORE_GROUP_TYPES_H

#include <QString>
#include <QtGlobal>

#include "common/group_types.h"

namespace chatbox {
namespace client {

struct GroupInfo {
    quint64 room_id = 0;
    QString name;
    QString group_no;
    quint64 owner_id = 0;
    int member_count = 0;
    int role = 0;  // 0 member, 1 admin, 2 owner
    int unread = 0;
    bool is_default = false;
    int status = 0;  // 0 active, 1 dissolved
};

struct GroupMemberInfo {
    quint64 user_id = 0;
    QString nickname;
    int role = 0;
    bool online = false;
};

struct GroupRequestInfo {
    quint64 request_id = 0;
    quint64 room_id = 0;
    quint64 user_id = 0;
    QString nickname;
    QString room_name;
    QString message;
    int status = 0;
    QString created_at;
};

GroupInfo groupInfoFromWire(const chatbox::GroupInfo& wire);
GroupMemberInfo groupMemberFromWire(const chatbox::GroupMemberInfo& wire);
GroupRequestInfo groupRequestFromWire(const chatbox::GroupRequestInfo& wire);

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_GROUP_TYPES_H
