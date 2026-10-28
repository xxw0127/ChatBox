#ifndef CHATBOX_SERVER_DB_MYSQL_CONNECTION_H
#define CHATBOX_SERVER_DB_MYSQL_CONNECTION_H

#include <cstdint>
#include <mysql/mysql.h>
#include <string>
#include <vector>

#include "common/group_types.h"

namespace chatbox {

struct DbConfig {
    std::string host = "127.0.0.1";
    unsigned int port = 3306;
    std::string user = "chatbox";
    std::string password;
    std::string database = "chatbox";
};

struct UserRecord {
    uint64_t id = 0;
    std::string username;
    std::string nickname;
    std::string password_hash;
    int status = 0;
};

struct UserProfile {
    uint64_t id = 0;
    std::string username;
    std::string nickname;
    std::string signature;
    std::string avatar;
    uint32_t avatar_version = 0;
};

struct SessionRecord {
    uint64_t user_id = 0;
    int revoked = 0;
    bool valid = false;
};

struct RoomRow {
    int id = 0;
    std::string name;
    int max_members = 0;
};

struct HistoryMessage {
    uint64_t id = 0;
    uint64_t sender_id = 0;
    std::string nickname;
    std::string content;
    std::string created_at;
    uint64_t reply_to_id = 0;
    std::string reply_sender_name;
    std::string reply_content;
    bool recalled = false;
};

struct SearchResultRow {
    uint64_t id = 0;
    uint64_t sender_id = 0;
    std::string nickname;
    std::string content;
    std::string created_at;
    bool recalled = false;
};

struct HistoryPage {
    std::vector<HistoryMessage> messages;
    bool has_more = false;
    uint64_t next_before_id = 0;
};

struct ConversationRow {
    uint64_t peer_id = 0;
    std::string nickname;
    uint64_t last_message_id = 0;
    std::string last_content;
    std::string last_time;
    uint32_t unread = 0;
};

struct UserSearchRow {
    uint64_t id = 0;
    std::string username;
    std::string nickname;
    std::string remark;
    uint32_t avatar_version = 0;
};

struct FriendRequestRow {
    uint64_t id = 0;
    std::string username;
    std::string nickname;
    std::string created_at;
};

class MysqlConnection {
public:
    MysqlConnection();
    ~MysqlConnection();

    MysqlConnection(const MysqlConnection&) = delete;
    MysqlConnection& operator=(const MysqlConnection&) = delete;

    bool connect(const DbConfig& config, std::string& error);
    void close();

    bool createUser(const std::string& username, const std::string& nickname,
                    const std::string& password_hash, uint64_t& user_id, bool& duplicate,
                    std::string& error);
    bool createUserWithDefaultGroup(const std::string& username,
                                    const std::string& nickname,
                                    const std::string& password_hash,
                                    uint64_t& user_id, bool& duplicate,
                                    std::string& error);
    bool findUserByUsername(const std::string& username, UserRecord& user, bool& found,
                            std::string& error);
    bool findUserById(uint64_t user_id, UserRecord& user, bool& found, std::string& error);
    bool updateLastLogin(uint64_t user_id, std::string& error);
    bool revokeSessionsForUser(uint64_t user_id, int reason, std::string& error);
    bool insertSession(const std::string& token_hash, uint64_t user_id,
                       const std::string& expires_at, std::string& error);
    bool findSession(const std::string& token_hash, SessionRecord& session, bool& found,
                     std::string& error);
    bool revokeSession(const std::string& token_hash, int reason, std::string& error);
    bool listRooms(std::vector<RoomRow>& rooms, std::string& error);
    bool insertMessage(int room_id, uint64_t sender_id, const std::string& client_msg_id,
                       const std::string& content, uint64_t reply_to_id,
                       const std::string& reply_sender_name,
                       const std::string& reply_content, const std::string& created_at,
                       uint64_t& message_id, bool& duplicate, std::string& error);
    bool insertPrivateMessage(uint64_t sender_id, uint64_t receiver_id,
                              const std::string& client_msg_id, const std::string& content,
                              uint64_t reply_to_id, const std::string& reply_sender_name,
                              const std::string& reply_content, const std::string& created_at,
                              uint64_t& message_id, bool& duplicate, std::string& error);
    bool listRoomHistory(int room_id, uint64_t after_id, uint64_t before_id,
                         int limit, HistoryPage& page, std::string& error);
    bool listPrivateHistory(uint64_t user_id, uint64_t peer_id, uint64_t before_id, int limit,
                            HistoryPage& page, std::string& error);
    bool searchRoomMessages(int room_id, uint64_t after_id,
                            const std::string& keyword, int limit,
                            std::vector<SearchResultRow>& results, std::string& error);
    bool searchPrivateMessages(uint64_t user_id, uint64_t peer_id,
                               const std::string& keyword, int limit,
                               std::vector<SearchResultRow>& results, std::string& error);
    bool recallRoomMessage(int room_id, uint64_t message_id, uint64_t user_id,
                           std::string& error);
    bool recallPrivateMessage(uint64_t user_id, uint64_t peer_id, uint64_t message_id,
                              std::string& error);
    bool listConversations(uint64_t user_id, std::vector<ConversationRow>& conversations,
                           std::string& error);
    bool markPrivateRead(uint64_t user_id, uint64_t peer_id, uint64_t message_id,
                         std::string& error);
    bool searchUsers(const std::string& keyword, std::vector<UserSearchRow>& users,
                     std::string& error);
    bool areFriends(uint64_t user_a, uint64_t user_b, bool& friends, std::string& error);
    bool createFriendRequest(uint64_t requester_id, uint64_t target_id,
                             bool& already_pending, bool& reverse_pending,
                             std::string& error);
    bool respondFriendRequest(uint64_t requester_id, uint64_t target_id, bool accept,
                              std::string& error);
    bool listFriends(uint64_t user_id, std::vector<UserSearchRow>& friends,
                     std::string& error);
    bool listFriendRequests(uint64_t user_id,
                            std::vector<FriendRequestRow>& requests,
                            std::string& error);
    bool removeFriend(uint64_t user_id, uint64_t friend_id, std::string& error);
    bool setFriendRemark(uint64_t user_id, uint64_t friend_id,
                         const std::string& remark, std::string& error);
    bool getUserProfile(uint64_t target_id, UserProfile& profile, bool& found,
                        std::string& error);
    bool getAvatar(uint64_t target_id, uint32_t& version, std::string& avatar, bool& found,
                   std::string& error);
    bool updateUserProfile(uint64_t user_id, const std::string& nickname,
                           const std::string& signature, uint8_t avatar_action,
                           const std::string& avatar, uint32_t& version, std::string& error);
    bool getProfileRelation(uint64_t viewer_id, uint64_t target_id, uint8_t& relation,
                            std::string& remark, std::string& error);
    bool listFriendIds(uint64_t user_id, std::vector<uint64_t>& ids, std::string& error);

    bool listGroupsForUser(uint64_t user_id, std::vector<GroupInfo>& groups,
                           std::string& error);
    bool findGroupByNumber(const std::string& group_no, GroupInfo& group,
                           bool& found, std::string& error);
    bool findGroupById(uint64_t room_id, GroupInfo& group, bool& found,
                       std::string& error);
    bool createGroup(const std::string& name, uint64_t owner_id, GroupInfo& group,
                     std::string& error);
    bool updateGroupName(uint64_t room_id, const std::string& name,
                         std::string& error);
    bool disbandGroup(uint64_t room_id, std::string& error);
    bool addRoomMember(uint64_t room_id, uint64_t user_id, GroupRole role,
                       std::string& error);
    bool removeRoomMember(uint64_t room_id, uint64_t user_id, std::string& error);
    bool listRoomMembers(uint64_t room_id, std::vector<GroupMemberInfo>& members,
                         std::string& error);
    bool getRoomMemberRole(uint64_t room_id, uint64_t user_id, GroupRole& role,
                           bool& found, std::string& error);
    bool setRoomMemberRole(uint64_t room_id, uint64_t user_id, GroupRole role,
                           std::string& error);
    bool transferRoomOwner(uint64_t room_id, uint64_t new_owner,
                           std::string& error);
    bool listJoinRequests(uint64_t room_id, std::vector<GroupRequestInfo>& requests,
                          std::string& error);
    bool createJoinRequest(uint64_t room_id, uint64_t user_id,
                           const std::string& message, uint64_t& request_id,
                           std::string& error);
    bool hasPendingJoinRequest(uint64_t room_id, uint64_t user_id, bool& pending,
                               std::string& error);
    bool findJoinRequest(uint64_t request_id, uint64_t& room_id,
                         uint64_t& user_id, int& status, bool& found,
                         std::string& error);
    bool respondJoinRequest(uint64_t request_id, uint64_t reviewer_id, bool accept,
                            std::string& error);
    bool createInvite(uint64_t room_id, uint64_t inviter_id, uint64_t invitee_id,
                      uint64_t& invite_id, std::string& error);
    bool hasPendingInvite(uint64_t room_id, uint64_t invitee_id, bool& pending,
                          std::string& error);
    bool findInvite(uint64_t invite_id, uint64_t& room_id, uint64_t& invitee_id,
                    int& status, bool& found, std::string& error);
    bool respondInvite(uint64_t invite_id, uint64_t invitee_id, bool accept,
                       std::string& error);
    bool listInvitesForUser(uint64_t user_id,
                            std::vector<GroupRequestInfo>& invites,
                            std::string& error);
    bool markRoomRead(uint64_t room_id, uint64_t user_id, uint64_t message_id,
                      std::string& error);
    bool unreadCount(uint64_t room_id, uint64_t user_id, uint32_t& unread,
                     std::string& error);
    bool isRoomMember(uint64_t room_id, uint64_t user_id, bool& member,
                      std::string& error);
    bool roomJoinMessageId(uint64_t room_id, uint64_t user_id,
                           uint64_t& message_id, std::string& error);
    bool maxRoomMessageId(uint64_t room_id, uint64_t& message_id,
                          std::string& error);
    bool countOwnedGroupsToday(uint64_t user_id, uint32_t& count,
                               std::string& error);
    bool countJoinRequestsToday(uint64_t user_id, uint32_t& count,
                                std::string& error);
    bool countInvitesToday(uint64_t user_id, uint32_t& count,
                           std::string& error);

private:
    bool ensureConnected(std::string& error);

    MYSQL* mysql_ = nullptr;
    DbConfig config_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_DB_MYSQL_CONNECTION_H
