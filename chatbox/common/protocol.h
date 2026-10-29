#ifndef CHATBOX_COMMON_PROTOCOL_H
#define CHATBOX_COMMON_PROTOCOL_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace chatbox {

constexpr uint16_t kProtocolMagic = 0x4348;
constexpr uint8_t kProtocolVersion = 1;
constexpr std::size_t kFrameHeaderSize = 14;
constexpr uint32_t kMaxInFrameBody = 64 * 1024;
constexpr uint32_t kMaxOutFrameBody = 512 * 1024;

// Additive capability bits negotiated in LOGIN/RESUME. Old clients send none
// and keep receiving only the legacy push set.
constexpr uint32_t kCapMultiDevice = 1u << 0;
constexpr uint32_t kCapSyncPull = 1u << 1;
constexpr uint32_t kCapReadStatePush = 1u << 2;
constexpr uint32_t kCapMessageEcho = 1u << 3;
constexpr uint32_t kCapSessionManage = 1u << 4;
constexpr uint32_t kCapDeviceMetadata = 1u << 5;

enum FrameFlags : uint8_t {
    kFlagResponse = 0x01,
    kFlagCompressed = 0x02,
    kFlagError = 0x04,
};

enum Command : uint16_t {
    kCmdRegister = 0x0001,
    kCmdLogin = 0x0002,
    kCmdLogout = 0x0003,
    kCmdResume = 0x0004,
    kCmdRoomList = 0x0010,
    kCmdJoinRoom = 0x0011,
    kCmdLeaveRoom = 0x0012,
    kCmdRoomMembers = 0x0013,
    kCmdChatSend = 0x0020,
    kCmdChatSendResp = 0x0021,
    kCmdChatPush = 0x0022,
    kCmdPrivateSend = 0x0030,
    kCmdPrivateSendResp = 0x0031,
    kCmdPrivatePush = 0x0032,
    kCmdPrivateConversations = 0x0033,
    kCmdPrivateMarkRead = 0x0034,
    kCmdPing = 0x0040,
    kCmdHistory = 0x0050,
    kCmdHistoryResp = 0x0051,
    kCmdUserSearch = 0x0060,
    kCmdMessageRecall = 0x0070,
    kCmdMessageSearch = 0x0080,
    kCmdFriendRequest = 0x0090,
    kCmdFriendRespond = 0x0091,
    kCmdFriendList = 0x0092,
    kCmdFriendRequests = 0x0093,
    kCmdFriendRemove = 0x0094,
    kCmdFriendRequestPush = 0x0095,
    kCmdFriendAcceptPush = 0x0096,
    kCmdFriendSetRemark = 0x0097,
    kCmdProfileGet = 0x00A0,
    kCmdProfileUpdate = 0x00A1,
    kCmdAvatarGet = 0x00A2,
    kCmdProfileUpdatePush = 0x00A3,
    kCmdGroupList = 0x00B0,
    kCmdGroupCreate = 0x00B1,
    kCmdGroupUpdate = 0x00B2,
    kCmdGroupDisband = 0x00B3,
    kCmdGroupSearch = 0x00B4,
    kCmdGroupJoinRequest = 0x00B5,
    kCmdGroupJoinRespond = 0x00B6,
    kCmdGroupInvite = 0x00B7,
    kCmdGroupInviteRespond = 0x00B8,
    kCmdGroupMemberList = 0x00B9,
    kCmdGroupMemberUpdate = 0x00BA,
    kCmdGroupRequestList = 0x00BB,
    kCmdGroupPush = 0x00BC,
    kCmdGroupMarkRead = 0x00BD,
    kCmdGroupLeave = 0x00BE,
    kCmdFileAuth = 0x00C0,
    kCmdFileUploadInit = 0x00C1,
    kCmdFileUploadChunk = 0x00C2,
    kCmdFileUploadStatus = 0x00C3,
    kCmdFileUploadCommit = 0x00C4,
    kCmdFileUploadCancel = 0x00C5,
    kCmdFileOpen = 0x00C6,
    kCmdFileRead = 0x00C7,
    kCmdFileChunk = 0x00C8,
    kCmdFileReadAck = 0x00C9,
    kCmdFileDelete = 0x00CA,
    kCmdFileInfo = 0x00CB,
    kCmdFileThumb = 0x00CC,
    kCmdFileList = 0x00CD,
    kCmdFileDeleted = 0x00CE,
    kCmdSessionList = 0x00D0,
    kCmdSessionRevoke = 0x00D1,
    kCmdReadStatePush = 0x00D2,
    kCmdSyncPull = 0x00D3,
    kCmdSyncResp = 0x00D4,
    kCmdMessageEchoPush = 0x00D5,
    // Realtime friend/room presence. Kept below the group range
    kCmdPresencePush = 0x00A4,

    kCmdError = 0x00FF,
};

struct FrameHeader {
    uint16_t magic = kProtocolMagic;
    uint8_t version = kProtocolVersion;
    uint8_t flags = 0;
    uint16_t cmd = 0;
    uint32_t seq = 0;
    uint32_t body_len = 0;
};

struct Frame {
    FrameHeader header;
    std::string body;
};

void appendU16(std::string& out, uint16_t value);
void appendU32(std::string& out, uint32_t value);

class ByteWriter {
public:
    void putU8(uint8_t value);
    void putU16(uint16_t value);
    void putU32(uint32_t value);
    void putU64(uint64_t value);
    void putString(const std::string& value);
    const std::string& data() const;

private:
    std::string data_;
};

class ByteReader {
public:
    ByteReader(const char* data, std::size_t size);

    bool getU8(uint8_t& value);
    bool getU16(uint16_t& value);
    bool getU32(uint32_t& value);
    bool getU64(uint64_t& value);
    bool getString(std::string& value);
    std::size_t remaining() const;

private:
    const char* data_;
    std::size_t size_;
    std::size_t offset_;
};

std::string encodeFrame(const Frame& frame);
bool decodeHeader(const char* data, std::size_t size, FrameHeader& header);
bool validateHeader(const FrameHeader& header, uint32_t max_body, std::string& error);

}  // namespace chatbox

#endif  // CHATBOX_COMMON_PROTOCOL_H
