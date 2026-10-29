#ifndef CHATBOX_COMMON_SESSION_PROTOCOL_H
#define CHATBOX_COMMON_SESSION_PROTOCOL_H

#include <cstdint>
#include <string>
#include <vector>

#include "common/protocol.h"

namespace chatbox {

struct DeviceInfo {
    std::string device_id;
    std::string device_name;
    std::string platform;
    std::string app_version;
    uint32_t caps = 0;
};

struct SessionInfo {
    std::string session_id;
    std::string device_id;
    std::string device_name;
    std::string platform;
    std::string app_version;
    std::string created_at;
    std::string last_seen_at;
    bool current = false;
};

struct SyncCursor {
    uint8_t kind = 0;
    uint64_t target_id = 0;
    uint64_t last_message_id = 0;
};

struct ReadStateEvent {
    uint8_t kind = 0;
    uint64_t target_id = 0;
    uint64_t last_read_message_id = 0;
    uint32_t unread = 0;
};

void appendDeviceInfo(ByteWriter& writer, const DeviceInfo& info);
// present=false means the legacy client omitted the optional device block.
bool readDeviceInfo(ByteReader& reader, DeviceInfo& info, bool& present);

void appendAuthExtras(ByteWriter& writer, uint32_t server_caps,
                      uint32_t ttl_seconds);
// Returns false when the response has no extras (legacy server response).
bool readAuthExtras(ByteReader& reader, uint32_t& server_caps,
                    uint32_t& ttl_seconds);

void appendSessionList(ByteWriter& writer,
                       const std::vector<SessionInfo>& sessions);
bool readSessionList(ByteReader& reader,
                     std::vector<SessionInfo>& sessions);

void appendSyncPull(ByteWriter& writer,
                    const std::vector<SyncCursor>& cursors,
                    uint16_t start_index);
bool readSyncPull(ByteReader& reader, std::vector<SyncCursor>& cursors,
                  uint16_t& start_index);

void appendReadState(ByteWriter& writer, const ReadStateEvent& event);
bool readReadState(ByteReader& reader, ReadStateEvent& event);

}  // namespace chatbox

#endif  // CHATBOX_COMMON_SESSION_PROTOCOL_H
