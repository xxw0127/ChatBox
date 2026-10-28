#ifndef CHATBOX_CLIENT_CORE_PRESENCE_BOOK_H
#define CHATBOX_CLIENT_CORE_PRESENCE_BOOK_H

#include <QHash>
#include <QtGlobal>

namespace chatbox {
namespace client {

// Last known online state per user, with the server sequence that produced
// it. Pushes that arrive out of order are dropped; list responses pass their
// own value as the fallback so a fresh push still wins over a snapshot that
// was generated earlier.
class PresenceBook {
public:
    // Returns false when the update is older than (or equal to) the stored
    // one, and when user_id is zero.
    bool apply(quint64 user_id, bool online, quint64 seq);
    bool online(quint64 user_id, bool fallback) const;
    quint64 seq(quint64 user_id) const;
    void clear();

private:
    QHash<quint64, quint64> seq_;
    QHash<quint64, bool> online_;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_PRESENCE_BOOK_H
