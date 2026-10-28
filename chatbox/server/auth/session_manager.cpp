#include "server/auth/session_manager.h"

namespace chatbox {

uint64_t SessionManager::nextPresenceSeq() {
    return ++presence_seq_;
}

bool SessionManager::bind(uint64_t user_id, const SessionBinding& binding,
                          SessionBinding& old_binding, uint64_t* presence_seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (presence_seq != nullptr) {
        *presence_seq = nextPresenceSeq();
    }
    auto it = bindings_.find(user_id);
    if (it == bindings_.end()) {
        bindings_[user_id] = binding;
        return false;
    }

    old_binding = it->second;
    const bool different = old_binding.connection_id != binding.connection_id;
    it->second = binding;
    return different;
}

bool SessionManager::unbind(uint64_t user_id, uint64_t connection_id,
                            uint64_t* presence_seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(user_id);
    if (it == bindings_.end() || it->second.connection_id != connection_id) {
        return false;
    }
    bindings_.erase(it);
    if (presence_seq != nullptr) {
        *presence_seq = nextPresenceSeq();
    }
    return true;
}

uint64_t SessionManager::unbindByConnection(uint64_t connection_id,
                                            uint64_t* presence_seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = bindings_.begin(); it != bindings_.end(); ++it) {
        if (it->second.connection_id != connection_id) {
            continue;
        }
        const uint64_t user_id = it->first;
        bindings_.erase(it);
        if (presence_seq != nullptr) {
            *presence_seq = nextPresenceSeq();
        }
        return user_id;
    }
    return 0;
}

bool SessionManager::find(uint64_t user_id, SessionBinding& binding) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = bindings_.find(user_id);
    if (it == bindings_.end() || it->second.connection.expired()) {
        return false;
    }
    binding = it->second;
    return true;
}

bool SessionManager::isOnline(uint64_t user_id) const {
    SessionBinding binding;
    return find(user_id, binding);
}

std::vector<SessionTarget> SessionManager::targets() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SessionTarget> result;
    result.reserve(bindings_.size());
    for (const auto& entry : bindings_) {
        if (entry.second.connection.expired()) {
            continue;
        }
        SessionTarget target;
        target.connection_id = entry.second.connection_id;
        target.io_thread_id = entry.second.io_thread_id;
        result.push_back(target);
    }
    return result;
}

}  // namespace chatbox
