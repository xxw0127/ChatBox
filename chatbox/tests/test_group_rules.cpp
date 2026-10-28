#include <string>

#include "server/group/group_rules.h"
#include "tests/test_framework.h"

namespace {

int testGroupNames() {
    CHECK_TRUE(chatbox::validGroupName("设计群"));
    CHECK_TRUE(chatbox::validGroupName("A"));
    CHECK_TRUE(chatbox::validGroupName(" 群聊A "));
    CHECK_TRUE(!chatbox::validGroupName(""));
    CHECK_TRUE(!chatbox::validGroupName("   "));
    CHECK_TRUE(!chatbox::validGroupName(std::string(33, 'x')));
    CHECK_TRUE(!chatbox::validGroupName("bad\nname"));
    return 0;
}

int testGroupNumbers() {
    CHECK_TRUE(chatbox::validGroupNumber("10000001"));
    CHECK_TRUE(chatbox::validGroupNumber("99999999"));
    CHECK_TRUE(!chatbox::validGroupNumber("1234567"));
    CHECK_TRUE(!chatbox::validGroupNumber("123456789"));
    CHECK_TRUE(!chatbox::validGroupNumber("1000000a"));
    CHECK_TRUE(!chatbox::validGroupNumber(""));
    return 0;
}

int testGroupRoles() {
    using chatbox::GroupRole;
    CHECK_TRUE(chatbox::canManageGroup(GroupRole::kOwner, GroupRole::kAdmin));
    CHECK_TRUE(chatbox::canManageGroup(GroupRole::kOwner, GroupRole::kMember));
    CHECK_TRUE(chatbox::canManageGroup(GroupRole::kAdmin, GroupRole::kMember));
    CHECK_TRUE(!chatbox::canManageGroup(GroupRole::kAdmin, GroupRole::kAdmin));
    CHECK_TRUE(!chatbox::canManageGroup(GroupRole::kAdmin, GroupRole::kOwner));
    CHECK_TRUE(!chatbox::canManageGroup(GroupRole::kMember, GroupRole::kMember));
    CHECK_TRUE(!chatbox::canManageGroup(GroupRole::kOwner, GroupRole::kOwner));
    CHECK_TRUE(chatbox::canDisband(GroupRole::kOwner, false));
    CHECK_TRUE(!chatbox::canDisband(GroupRole::kOwner, true));
    CHECK_TRUE(!chatbox::canDisband(GroupRole::kAdmin, false));
    CHECK_TRUE(!chatbox::canDisband(GroupRole::kMember, false));
    return 0;
}

int testDefaultGroup() {
    CHECK_EQ(chatbox::defaultGroupId(), 1u);
    CHECK_EQ(chatbox::defaultGroupNumber(), std::string("10000001"));
    CHECK_TRUE(chatbox::isDefaultGroupProtected(1));
    CHECK_TRUE(!chatbox::isDefaultGroupProtected(2));
    CHECK_EQ(chatbox::computeJoinCursor(0), 0u);
    CHECK_EQ(chatbox::computeJoinCursor(42), 42u);
    CHECK_EQ(chatbox::unreadLowerBound(10, 20), 20u);
    CHECK_EQ(chatbox::unreadLowerBound(30, 20), 30u);
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testGroupNames();
    failures += testGroupNumbers();
    failures += testGroupRoles();
    failures += testDefaultGroup();
    if (failures == 0) {
        std::cout << "test_group_rules: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
