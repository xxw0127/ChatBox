#ifndef CHATBOX_COMMON_GROUP_PROTOCOL_H
#define CHATBOX_COMMON_GROUP_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

#include "common/group_types.h"
#include "common/protocol.h"

namespace chatbox {

std::string encodeGroupInfo(const GroupInfo& group);
std::string encodeGroupList(const std::vector<GroupInfo>& groups);
bool decodeGroupList(const std::string& body, std::vector<GroupInfo>& groups);

std::string encodeGroupMembers(uint32_t room_id,
                               const std::vector<GroupMemberInfo>& members);
bool decodeGroupMembers(const std::string& body, uint32_t& room_id,
                        std::vector<GroupMemberInfo>& members);

std::string encodeGroupSearch(const GroupInfo& group, uint8_t membership);
std::string encodeGroupSearchNotFound();
bool decodeGroupSearch(const std::string& body, GroupInfo& group,
                       uint8_t& membership);

std::string encodeGroupRequests(uint32_t room_id,
                                const std::vector<GroupRequestInfo>& requests);
bool decodeGroupRequests(const std::string& body, uint32_t& room_id,
                         std::vector<GroupRequestInfo>& requests);

bool decodeGroupCreateRequest(const std::string& body, std::string& name);
bool decodeGroupSearchRequest(const std::string& body, std::string& group_no);
bool decodeGroupJoinRequest(const std::string& body, std::string& group_no,
                            std::string& message);
bool decodeGroupRespondRequest(const std::string& body, uint64_t& request_id,
                               bool& accept);
bool decodeGroupInviteRequest(const std::string& body, uint32_t& room_id,
                              uint64_t& invitee_id);
bool decodeGroupInviteRespond(const std::string& body, uint64_t& invite_id,
                              bool& accept);
bool decodeGroupRoomRequest(const std::string& body, uint32_t& room_id);
bool decodeGroupUpdateRequest(const std::string& body, uint32_t& room_id,
                              std::string& name);
bool decodeGroupMemberUpdate(const std::string& body, uint32_t& room_id,
                             uint64_t& user_id, uint8_t& action);
bool decodeGroupMarkRead(const std::string& body, uint32_t& room_id,
                         uint64_t& message_id);

std::string encodeGroupIdResponse(uint64_t id);
std::string encodeGroupPush(uint8_t event, uint32_t room_id, uint64_t user_id,
                            uint8_t role);

}  // namespace chatbox

#endif  // CHATBOX_COMMON_GROUP_PROTOCOL_H
