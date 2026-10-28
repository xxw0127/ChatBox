#ifndef CHATBOX_COMMON_GROUP_TYPES_H
#define CHATBOX_COMMON_GROUP_TYPES_H

#include <cstdint>
#include <string>

namespace chatbox {

enum class GroupRole : uint8_t {
    kMember = 0,
    kAdmin = 1,
    kOwner = 2,
};

enum class GroupStatus : uint8_t {
    kActive = 0,
    kDissolved = 1,
};

struct GroupInfo {
    uint64_t room_id = 0;
    std::string name;
    std::string group_no;
    uint64_t owner_id = 0;
    uint32_t member_count = 0;
    GroupRole role = GroupRole::kMember;
    uint32_t unread = 0;
    bool is_default = false;
    GroupStatus status = GroupStatus::kActive;
};

struct GroupMemberInfo {
    uint64_t user_id = 0;
    std::string nickname;
    GroupRole role = GroupRole::kMember;
    bool online = false;
};

struct GroupRequestInfo {
    uint64_t request_id = 0;
    uint64_t room_id = 0;
    uint64_t user_id = 0;
    std::string nickname;
    std::string message;
    int status = 0;  // 0 pending, 1 approved, 2 rejected
    std::string created_at;
};

}  // namespace chatbox

#endif  // CHATBOX_COMMON_GROUP_TYPES_H
