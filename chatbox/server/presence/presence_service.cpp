#include "server/presence/presence_service.h"

#include <cstddef>

namespace chatbox {

Frame makePresencePushFrame(const std::vector<PresenceEntry>& entries) {
    Frame frame;
    frame.header.cmd = kCmdPresencePush;
    frame.header.flags = 0;  // push, not a response

    const std::size_t count =
        entries.size() > 65535 ? static_cast<std::size_t>(65535) : entries.size();
    ByteWriter writer;
    writer.putU16(static_cast<uint16_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
        writer.putU64(entries[i].user_id);
        writer.putU8(entries[i].online ? 1 : 0);
        writer.putU64(entries[i].seq);
    }
    frame.body = writer.data();
    return frame;
}

}  // namespace chatbox
