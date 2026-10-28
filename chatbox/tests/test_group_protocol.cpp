#include <string>
#include <vector>

#include "common/group_protocol.h"
#include "tests/test_framework.h"

namespace {

int testGroupListRoundTrip() {
    std::vector<chatbox::GroupInfo> groups(2);
    groups[0].room_id = 1;
    groups[0].name = "群聊A";
    groups[0].group_no = "10000001";
    groups[0].owner_id = 4;
    groups[0].member_count = 12;
    groups[0].role = chatbox::GroupRole::kOwner;
    groups[0].unread = 3;
    groups[0].is_default = true;
    groups[0].status = chatbox::GroupStatus::kActive;
    groups[1].room_id = 7;
    groups[1].name = "设计群";
    groups[1].group_no = "10000002";
    groups[1].role = chatbox::GroupRole::kMember;
    groups[1].status = chatbox::GroupStatus::kDissolved;

    const std::string body = chatbox::encodeGroupList(groups);
    std::vector<chatbox::GroupInfo> decoded;
    CHECK_TRUE(chatbox::decodeGroupList(body, decoded));
    CHECK_EQ(decoded.size(), 2u);
    CHECK_EQ(decoded[0].room_id, 1u);
    CHECK_EQ(decoded[0].group_no, std::string("10000001"));
    CHECK_TRUE(decoded[0].is_default);
    CHECK_EQ(decoded[0].role, chatbox::GroupRole::kOwner);
    CHECK_EQ(decoded[1].status, chatbox::GroupStatus::kDissolved);
    CHECK_TRUE(!chatbox::decodeGroupList(body.substr(0, body.size() - 1), decoded));
    return 0;
}

int testGroupMembersAndRequests() {
    std::vector<chatbox::GroupMemberInfo> members(2);
    members[0].user_id = 4;
    members[0].nickname = "阿杰";
    members[0].role = chatbox::GroupRole::kOwner;
    members[0].online = true;
    members[1].user_id = 5;
    members[1].nickname = "小雨";
    const std::string member_body = chatbox::encodeGroupMembers(1, members);
    uint32_t room_id = 0;
    std::vector<chatbox::GroupMemberInfo> decoded_members;
    CHECK_TRUE(chatbox::decodeGroupMembers(member_body, room_id, decoded_members));
    CHECK_EQ(room_id, 1u);
    CHECK_EQ(decoded_members.size(), 2u);
    CHECK_EQ(decoded_members[0].nickname, std::string("阿杰"));
    CHECK_TRUE(decoded_members[0].online);

    std::vector<chatbox::GroupRequestInfo> requests(1);
    requests[0].request_id = 9;
    requests[0].room_id = 7;
    requests[0].user_id = 8;
    requests[0].nickname = "林一";
    requests[0].room_name = "设计群";
    requests[0].message = "想加入";
    requests[0].status = 0;
    requests[0].created_at = "2026-10-28 10:00:00";
    const std::string request_body = chatbox::encodeGroupRequests(7, requests);
    uint32_t request_room = 0;
    std::vector<chatbox::GroupRequestInfo> decoded_requests;
    CHECK_TRUE(chatbox::decodeGroupRequests(request_body, request_room,
                                            decoded_requests));
    CHECK_EQ(request_room, 7u);
    CHECK_EQ(decoded_requests.size(), 1u);
    CHECK_EQ(decoded_requests[0].room_name, std::string("设计群"));
    CHECK_EQ(decoded_requests[0].message, std::string("想加入"));

    chatbox::GroupInfo found_group;
    found_group.room_id = 7;
    found_group.name = "设计群";
    uint8_t membership = 2;
    chatbox::GroupInfo decoded_group;
    uint8_t decoded_membership = 0;
    CHECK_TRUE(chatbox::decodeGroupSearch(
        chatbox::encodeGroupSearch(found_group, membership),
        decoded_group, decoded_membership));
    CHECK_EQ(decoded_group.name, std::string("设计群"));
    CHECK_EQ(decoded_membership, 2u);
    CHECK_TRUE(chatbox::decodeGroupSearch(
        chatbox::encodeGroupSearchNotFound(), decoded_group, decoded_membership));
    CHECK_EQ(decoded_membership, 0u);
    return 0;
}

int testGroupRequestDecoders() {
    chatbox::ByteWriter writer;
    writer.putString("10000007");
    writer.putString("申请说明");
    std::string group_no;
    std::string message;
    CHECK_TRUE(chatbox::decodeGroupJoinRequest(writer.data(), group_no, message));
    CHECK_EQ(group_no, std::string("10000007"));
    CHECK_EQ(message, std::string("申请说明"));

    chatbox::ByteWriter mark;
    mark.putU32(7);
    mark.putU64(123);
    uint32_t room_id = 0;
    uint64_t message_id = 0;
    CHECK_TRUE(chatbox::decodeGroupMarkRead(mark.data(), room_id, message_id));
    CHECK_EQ(room_id, 7u);
    CHECK_EQ(message_id, 123u);

    chatbox::ByteWriter respond;
    respond.putU64(42);
    respond.putU8(1);
    uint64_t request_id = 0;
    bool accept = false;
    CHECK_TRUE(chatbox::decodeGroupRespondRequest(respond.data(), request_id, accept));
    CHECK_EQ(request_id, 42u);
    CHECK_TRUE(accept);
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testGroupListRoundTrip();
    failures += testGroupMembersAndRequests();
    failures += testGroupRequestDecoders();
    if (failures == 0) {
        std::cout << "test_group_protocol: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
