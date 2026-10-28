#ifndef CHATBOX_SERVER_PRESENCE_PRESENCE_SERVICE_H
#define CHATBOX_SERVER_PRESENCE_PRESENCE_SERVICE_H

#include <cstdint>
#include <vector>

#include "common/protocol.h"

namespace chatbox {

// One online/offline transition. seq is assigned by SessionManager while the
// bind/unbind lock is held, so the client can drop out-of-order arrivals.
struct PresenceEntry {
    uint64_t user_id = 0;
    bool online = false;
    uint64_t seq = 0;
};

// Builds a PRESENCE_PUSH frame (u16 count followed by {u64 user_id, u8 online,
// u64 seq}); entries beyond 65535 are dropped because the count is u16.
Frame makePresencePushFrame(const std::vector<PresenceEntry>& entries);

}  // namespace chatbox

#endif  // CHATBOX_SERVER_PRESENCE_PRESENCE_SERVICE_H
