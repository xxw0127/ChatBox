#include "core/avatar_cache.h"

namespace chatbox {
namespace client {

bool AvatarCache::needsFetch(quint64 user_id, quint32 version) const {
    if (version == 0) {
        return false;
    }
    const auto it = entries_.constFind(user_id);
    if (it == entries_.constEnd()) {
        return true;
    }
    return !(it->loaded && it->version == version);
}

void AvatarCache::store(quint64 user_id, quint32 version, const QImage& image) {
    auto it = entries_.find(user_id);
    if (it != entries_.end() && it->loaded && it->version > version) {
        // A late response for an older avatar version must not replace a
        // newer image (including a newer explicit "no avatar" entry).
        return;
    }
    Entry entry;
    entry.version = version;
    entry.image = image;
    entry.loaded = true;
    entries_.insert(user_id, entry);
}

bool AvatarCache::lookup(quint64 user_id, QImage& image) const {
    const auto it = entries_.constFind(user_id);
    if (it == entries_.constEnd() || it->image.isNull()) {
        return false;
    }
    image = it->image;
    return true;
}

bool AvatarCache::contains(quint64 user_id) const {
    return entries_.contains(user_id);
}

quint32 AvatarCache::version(quint64 user_id) const {
    const auto it = entries_.constFind(user_id);
    return it == entries_.constEnd() ? 0 : it->version;
}

void AvatarCache::remove(quint64 user_id) {
    entries_.remove(user_id);
}

void AvatarCache::clear() {
    entries_.clear();
}

}  // namespace client
}  // namespace chatbox
