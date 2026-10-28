#ifndef CHATBOX_SERVER_AUTH_SESSION_MANAGER_H
#define CHATBOX_SERVER_AUTH_SESSION_MANAGER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "server/net/connection.h"

namespace chatbox {

struct SessionBinding {
    uint64_t connection_id = 0;
    int io_thread_id = -1;
    std::string peer_ip;
    std::weak_ptr<Connection> connection;
};

// Minimal routing information for a presence broadcast: enough to find the
// owning IO thread without touching the weak connection pointer.
struct SessionTarget {
    uint64_t connection_id = 0;
    int io_thread_id = -1;
};

class SessionManager {
public:
    // Replaces the old binding for the user. Returns true when an old binding
    // with a different connection was present. presence_seq receives a
    // per-user monotonic sequence assigned while the lock is held.
    bool bind(uint64_t user_id, const SessionBinding& binding,
              SessionBinding& old_binding, uint64_t* presence_seq = nullptr);
    // Removes the binding only when connection_id still owns it. Returns true
    // when a binding was removed; presence_seq is written only in that case.
    bool unbind(uint64_t user_id, uint64_t connection_id,
                uint64_t* presence_seq = nullptr);
    // Removes whichever user owns connection_id and returns that user id, or 0
    // when this connection no longer owns a binding (e.g. it was superseded).
    uint64_t unbindByConnection(uint64_t connection_id,
                                uint64_t* presence_seq = nullptr);
    bool find(uint64_t user_id, SessionBinding& binding) const;
    bool isOnline(uint64_t user_id) const;
    // All live (non-expired) bindings, for fan-out without holding the lock.
    std::vector<SessionTarget> targets() const;

private:
    uint64_t nextPresenceSeq();

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, SessionBinding> bindings_;
    std::atomic<uint64_t> presence_seq_{0};
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_SESSION_MANAGER_H
