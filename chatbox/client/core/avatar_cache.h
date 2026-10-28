#ifndef CHATBOX_CLIENT_CORE_AVATAR_CACHE_H
#define CHATBOX_CLIENT_CORE_AVATAR_CACHE_H

#include <QHash>
#include <QImage>
#include <QtGlobal>

namespace chatbox {
namespace client {

// In-memory cache of avatar images keyed by user_id. Version 0 means the user
// has no avatar and is never fetched. An empty image marks a version that was
// loaded and has no avatar bytes. Store() is monotonic: a response for an older
// version is ignored once a newer version is cached.
class AvatarCache {
public:
    bool needsFetch(quint64 user_id, quint32 version) const;
    void store(quint64 user_id, quint32 version, const QImage& image);
    bool lookup(quint64 user_id, QImage& image) const;
    bool contains(quint64 user_id) const;
    // Cached version, or 0 when the user has no entry / no avatar.
    quint32 version(quint64 user_id) const;
    void remove(quint64 user_id);
    void clear();

private:
    struct Entry {
        quint32 version = 0;
        QImage image;
        bool loaded = false;
    };

    QHash<quint64, Entry> entries_;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_AVATAR_CACHE_H
