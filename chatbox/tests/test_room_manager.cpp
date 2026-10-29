#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "server/room/room_manager.h"
#include "tests/test_framework.h"

namespace {

chatbox::RoomMember makeMember(uint64_t user_id, uint64_t connection_id) {
    chatbox::RoomMember member;
    member.user_id = user_id;
    member.connection_id = connection_id;
    member.io_thread_id = static_cast<int>(connection_id % 4);
    member.nickname = "user-" + std::to_string(user_id);
    return member;
}

int testTwoConnectionsOneMemberRow() {
    chatbox::RoomManager rooms({{1, "Room", 500}});
    std::string error;
    CHECK_TRUE(rooms.join(1, makeMember(7, 1), error));
    CHECK_TRUE(rooms.join(1, makeMember(7, 2), error));

    std::vector<chatbox::RoomMember> members;
    CHECK_TRUE(rooms.snapshot(1, members));
    CHECK_EQ(members.size(), static_cast<std::size_t>(1));
    CHECK_EQ(members[0].user_id, static_cast<uint64_t>(7));

    std::vector<chatbox::RoomMember> targets;
    CHECK_TRUE(rooms.targets(1, targets));
    CHECK_EQ(targets.size(), static_cast<std::size_t>(2));
    CHECK_EQ(rooms.onlineCount(1), 1);
    return 0;
}

int testLeaveRemovesAllUserConnections() {
    chatbox::RoomManager rooms({{1, "Room", 500}});
    std::string error;
    CHECK_TRUE(rooms.join(1, makeMember(7, 1), error));
    CHECK_TRUE(rooms.join(1, makeMember(7, 2), error));
    CHECK_TRUE(rooms.leave(1, 7));

    std::vector<chatbox::RoomMember> targets;
    CHECK_TRUE(rooms.targets(1, targets));
    CHECK_TRUE(targets.empty());
    CHECK_EQ(rooms.onlineCount(1), 0);
    CHECK_TRUE(rooms.removeConnection(1).empty());
    CHECK_TRUE(rooms.removeConnection(2).empty());
    return 0;
}

int testRemoveConnectionKeepsOtherDevice() {
    chatbox::RoomManager rooms({{1, "Room", 500}});
    std::string error;
    CHECK_TRUE(rooms.join(1, makeMember(7, 1), error));
    CHECK_TRUE(rooms.join(1, makeMember(7, 2), error));

    const std::vector<int> changed = rooms.removeConnection(1);
    CHECK_EQ(changed.size(), static_cast<std::size_t>(1));
    CHECK_EQ(changed[0], 1);
    CHECK_TRUE(rooms.isMember(1, 7));

    std::vector<chatbox::RoomMember> targets;
    CHECK_TRUE(rooms.targets(1, targets));
    CHECK_EQ(targets.size(), static_cast<std::size_t>(1));
    CHECK_EQ(targets[0].connection_id, static_cast<uint64_t>(2));
    return 0;
}

int testMaxMembersCountsUsers() {
    chatbox::RoomManager rooms({{1, "Room", 1}});
    std::string error;
    CHECK_TRUE(rooms.join(1, makeMember(7, 1), error));
    CHECK_TRUE(rooms.join(1, makeMember(7, 2), error));
    CHECK_TRUE(!rooms.join(1, makeMember(8, 3), error));
    CHECK_EQ(error, std::string("room full"));
    CHECK_EQ(rooms.onlineCount(1), 1);
    return 0;
}

int testTargetsIncludesBothDevices() {
    chatbox::RoomManager rooms({{1, "Room", 500}});
    std::string error;
    CHECK_TRUE(rooms.join(1, makeMember(7, 1), error));
    CHECK_TRUE(rooms.join(1, makeMember(7, 2), error));
    CHECK_TRUE(rooms.join(1, makeMember(8, 3), error));

    std::vector<chatbox::RoomMember> targets;
    CHECK_TRUE(rooms.targets(1, targets));
    CHECK_EQ(targets.size(), static_cast<std::size_t>(3));
    CHECK_EQ(rooms.onlineCount(1), 2);
    return 0;
}

int testNicknameUpdatesAllConnections() {
    chatbox::RoomManager rooms({{1, "Room", 500}});
    std::string error;
    CHECK_TRUE(rooms.join(1, makeMember(7, 1), error));
    CHECK_TRUE(rooms.join(1, makeMember(7, 2), error));
    rooms.updateNickname(7, "new-name");

    std::vector<chatbox::RoomMember> targets;
    CHECK_TRUE(rooms.targets(1, targets));
    for (const chatbox::RoomMember& member : targets) {
        CHECK_EQ(member.nickname, std::string("new-name"));
    }
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testTwoConnectionsOneMemberRow();
    failures += testLeaveRemovesAllUserConnections();
    failures += testRemoveConnectionKeepsOtherDevice();
    failures += testMaxMembersCountsUsers();
    failures += testTargetsIncludesBothDevices();
    failures += testNicknameUpdatesAllConnections();
    if (failures == 0) {
        std::cout << "test_room_manager: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
