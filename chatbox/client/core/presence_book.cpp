#include "core/presence_book.h"

namespace chatbox {
namespace client {

bool PresenceBook::apply(quint64 user_id, bool online, quint64 seq) {
    if (user_id == 0) {
        return false;
    }
    const auto it = seq_.constFind(user_id);
    if (it != seq_.constEnd() && seq <= *it) {
        return false;
    }
    seq_.insert(user_id, seq);
    online_.insert(user_id, online);
    return true;
}

bool PresenceBook::online(quint64 user_id, bool fallback) const {
    const auto it = online_.constFind(user_id);
    return it == online_.constEnd() ? fallback : *it;
}

quint64 PresenceBook::seq(quint64 user_id) const {
    return seq_.value(user_id, 0);
}

void PresenceBook::clear() {
    seq_.clear();
    online_.clear();
}

}  // namespace client
}  // namespace chatbox
