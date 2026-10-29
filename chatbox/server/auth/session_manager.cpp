#include "server/auth/session_manager.h"

#include <utility>

namespace chatbox {

uint64_t SessionManager::nextPresenceSeq() {
    return ++presence_seq_;
}

SessionBinding* SessionManager::findBindingLocked(uint64_t user_id,
                                                  uint64_t connection_id) {
    auto user_it = bindings_.find(user_id);
    if (user_it == bindings_.end()) {
        return nullptr;
    }
    auto binding_it = user_it->second.find(connection_id);
    return binding_it == user_it->second.end() ? nullptr : &binding_it->second;
}

bool SessionManager::userOnlineLocked(uint64_t user_id) const {
    auto user_it = bindings_.find(user_id);
    if (user_it == bindings_.end()) {
        return false;
    }
    for (const auto& entry : user_it->second) {
        if (!entry.second.connection.expired()) {
            return true;
        }
    }
    return false;
}

SessionChange SessionManager::add(uint64_t user_id,
                                  const SessionBinding& binding) {
    std::lock_guard<std::mutex> lock(mutex_);
    SessionChange change;
    change.user_id = user_id;

    auto& user_bindings = bindings_[user_id];
    const bool was_online = !user_bindings.empty();

    auto same_connection = user_bindings.find(binding.connection_id);
    if (same_connection != user_bindings.end()) {
        change.removed.push_back(same_connection->second);
        user_bindings.erase(same_connection);
    }

    if (!binding.session_id.empty()) {
        for (auto it = user_bindings.begin(); it != user_bindings.end(); ++it) {
            if (it->second.session_id == binding.session_id) {
                change.removed.push_back(it->second);
                user_bindings.erase(it);
                break;
            }
        }
    }

    user_bindings[binding.connection_id] = binding;
    if (!was_online) {
        change.became_online = true;
        change.presence_seq = nextPresenceSeq();
    }
    return change;
}

SessionChange SessionManager::removeConnection(uint64_t connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    SessionChange change;
    for (auto user_it = bindings_.begin(); user_it != bindings_.end(); ++user_it) {
        auto binding_it = user_it->second.find(connection_id);
        if (binding_it == user_it->second.end()) {
            continue;
        }
        change.user_id = user_it->first;
        change.removed.push_back(binding_it->second);
        user_it->second.erase(binding_it);
        if (user_it->second.empty()) {
            change.went_offline = true;
            change.presence_seq = nextPresenceSeq();
            bindings_.erase(user_it);
        }
        return change;
    }
    return change;
}

SessionChange SessionManager::removeSession(uint64_t user_id,
                                            const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    SessionChange change;
    change.user_id = user_id;
    auto user_it = bindings_.find(user_id);
    if (user_it == bindings_.end()) {
        return change;
    }
    for (auto it = user_it->second.begin(); it != user_it->second.end(); ++it) {
        if (it->second.session_id != session_id) {
            continue;
        }
        change.removed.push_back(it->second);
        user_it->second.erase(it);
        if (user_it->second.empty()) {
            change.went_offline = true;
            change.presence_seq = nextPresenceSeq();
            bindings_.erase(user_it);
        }
        return change;
    }
    return change;
}

bool SessionManager::find(uint64_t user_id, SessionBinding& binding) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto user_it = bindings_.find(user_id);
    if (user_it == bindings_.end()) {
        return false;
    }
    for (const auto& entry : user_it->second) {
        if (!entry.second.connection.expired()) {
            binding = entry.second;
            return true;
        }
    }
    return false;
}

bool SessionManager::findAll(uint64_t user_id,
                             std::vector<SessionBinding>& bindings) const {
    bindings.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    auto user_it = bindings_.find(user_id);
    if (user_it == bindings_.end()) {
        return false;
    }
    for (const auto& entry : user_it->second) {
        if (!entry.second.connection.expired()) {
            bindings.push_back(entry.second);
        }
    }
    return !bindings.empty();
}

bool SessionManager::findBySession(const std::string& session_id,
                                   SessionBinding& binding) const {
    if (session_id.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& user_entry : bindings_) {
        for (const auto& binding_entry : user_entry.second) {
            if (binding_entry.second.session_id == session_id &&
                !binding_entry.second.connection.expired()) {
                binding = binding_entry.second;
                return true;
            }
        }
    }
    return false;
}

bool SessionManager::isOnline(uint64_t user_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return userOnlineLocked(user_id);
}

std::vector<SessionTarget> SessionManager::targets() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SessionTarget> result;
    for (const auto& user_entry : bindings_) {
        for (const auto& binding_entry : user_entry.second) {
            if (binding_entry.second.connection.expired()) {
                continue;
            }
            SessionTarget target;
            target.connection_id = binding_entry.second.connection_id;
            target.io_thread_id = binding_entry.second.io_thread_id;
            result.push_back(target);
        }
    }
    return result;
}

bool SessionManager::bind(uint64_t user_id, const SessionBinding& binding,
                          SessionBinding& old_binding,
                          uint64_t* presence_seq) {
    const SessionChange change = add(user_id, binding);
    if (presence_seq != nullptr) {
        *presence_seq = change.presence_seq;
    }
    if (!change.removed.empty()) {
        old_binding = change.removed.front();
    }
    return !change.removed.empty() &&
           change.removed.front().connection_id != binding.connection_id;
}

bool SessionManager::unbind(uint64_t user_id, uint64_t connection_id,
                            uint64_t* presence_seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto user_it = bindings_.find(user_id);
    if (user_it == bindings_.end()) {
        return false;
    }
    auto binding_it = user_it->second.find(connection_id);
    if (binding_it == user_it->second.end()) {
        return false;
    }
    user_it->second.erase(binding_it);
    if (presence_seq != nullptr && user_it->second.empty()) {
        *presence_seq = nextPresenceSeq();
    }
    if (user_it->second.empty()) {
        bindings_.erase(user_it);
    }
    return true;
}

uint64_t SessionManager::unbindByConnection(uint64_t connection_id,
                                            uint64_t* presence_seq) {
    const SessionChange change = removeConnection(connection_id);
    if (presence_seq != nullptr) {
        *presence_seq = change.presence_seq;
    }
    return change.user_id;
}

}  // namespace chatbox
