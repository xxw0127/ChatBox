#ifndef CHATBOX_SERVER_AUTH_SESSION_MANAGER_H
#define CHATBOX_SERVER_AUTH_SESSION_MANAGER_H

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "server/net/connection.h"

namespace chatbox {

struct SessionBinding {
    uint64_t connection_id = 0;
    int io_thread_id = -1;
    std::string peer_ip;
    std::weak_ptr<Connection> connection;
    std::string session_id;
    std::string device_id;
    uint32_t caps = 0;
};

struct SessionTarget {
    uint64_t connection_id = 0;
    int io_thread_id = -1;
};

struct SessionChange {
    uint64_t user_id = 0;
    bool became_online = false;
    bool went_offline = false;
    uint64_t presence_seq = 0;
    std::vector<SessionBinding> removed;
};

class SessionManager {
public:
    // Adds one device binding. Re-adding the same connection or the same
    // session_id replaces the old binding and returns it in `removed`.
    SessionChange add(uint64_t user_id, const SessionBinding& binding);
    // Removes one connection. `user_id` in the result identifies the owner.
    SessionChange removeConnection(uint64_t connection_id);
    // Removes one session id belonging to a user.
    SessionChange removeSession(uint64_t user_id, const std::string& session_id);
    bool find(uint64_t user_id, SessionBinding& binding) const;
    bool findAll(uint64_t user_id, std::vector<SessionBinding>& bindings) const;
    bool findBySession(const std::string& session_id,
                       SessionBinding& binding) const;
    bool isOnline(uint64_t user_id) const;
    // All live (non-expired) bindings, for fan-out without holding the lock.
    std::vector<SessionTarget> targets() const;

    // Legacy single-session wrappers kept until AuthService is migrated.
    bool bind(uint64_t user_id, const SessionBinding& binding,
              SessionBinding& old_binding, uint64_t* presence_seq = nullptr);
    bool unbind(uint64_t user_id, uint64_t connection_id,
                uint64_t* presence_seq = nullptr);
    uint64_t unbindByConnection(uint64_t connection_id,
                                uint64_t* presence_seq = nullptr);

private:
    uint64_t nextPresenceSeq();
    SessionBinding* findBindingLocked(uint64_t user_id, uint64_t connection_id);
    bool userOnlineLocked(uint64_t user_id) const;

    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, SessionBinding>>
        bindings_;
    std::atomic<uint64_t> presence_seq_{0};
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_SESSION_MANAGER_H
