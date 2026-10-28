#ifndef CHATBOX_SERVER_GROUP_GROUP_RULES_H
#define CHATBOX_SERVER_GROUP_GROUP_RULES_H

#include <cstdint>
#include <string>

#include "server/group/group_types.h"

namespace chatbox {

bool validGroupName(const std::string& name);
bool validGroupNumber(const std::string& group_no);
bool canManageGroup(GroupRole actor, GroupRole target);
bool canDisband(GroupRole actor, bool is_default);

uint64_t defaultGroupId();
std::string defaultGroupNumber();
bool isDefaultGroupProtected(uint64_t room_id);
uint64_t computeJoinCursor(uint64_t max_message_id);
uint64_t unreadLowerBound(uint64_t join_message_id, uint64_t last_read_message_id);

}  // namespace chatbox

#endif  // CHATBOX_SERVER_GROUP_GROUP_RULES_H
