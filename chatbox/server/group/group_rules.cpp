#include "server/group/group_rules.h"

#include <algorithm>
#include <cctype>

#include "common/text_utils.h"

namespace chatbox {

namespace {

constexpr std::size_t kMaxGroupNameCodePoints = 32;
constexpr std::size_t kGroupNumberLength = 8;
constexpr uint64_t kDefaultGroupId = 1;
constexpr const char* kDefaultGroupNumber = "10000001";

}  // namespace

bool validGroupName(const std::string& name) {
    const std::string trimmed = trimAsciiWhitespace(name);
    if (trimmed.empty() || containsControlCharacters(trimmed)) {
        return false;
    }
    const std::size_t count = utf8CodePointCount(trimmed);
    return count >= 1 && count <= kMaxGroupNameCodePoints;
}

bool validGroupNumber(const std::string& group_no) {
    if (group_no.size() != kGroupNumberLength) {
        return false;
    }
    for (const unsigned char ch : group_no) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

bool canManageGroup(GroupRole actor, GroupRole target) {
    if (actor == target) {
        return false;
    }
    if (actor == GroupRole::kOwner) {
        return target == GroupRole::kAdmin || target == GroupRole::kMember;
    }
    if (actor == GroupRole::kAdmin) {
        return target == GroupRole::kMember;
    }
    return false;
}

bool canDisband(GroupRole actor, bool is_default) {
    return actor == GroupRole::kOwner && !is_default;
}

uint64_t defaultGroupId() {
    return kDefaultGroupId;
}

std::string defaultGroupNumber() {
    return kDefaultGroupNumber;
}

bool isDefaultGroupProtected(uint64_t room_id) {
    return room_id == kDefaultGroupId;
}

uint64_t computeJoinCursor(uint64_t max_message_id) {
    return max_message_id;
}

uint64_t unreadLowerBound(uint64_t join_message_id,
                          uint64_t last_read_message_id) {
    return std::max(join_message_id, last_read_message_id);
}

}  // namespace chatbox
