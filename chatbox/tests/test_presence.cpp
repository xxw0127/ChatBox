#include <cstdint>
#include <iostream>
#include <vector>

#include "common/protocol.h"
#include "server/presence/presence_service.h"
#include "tests/test_framework.h"

namespace {

int testPresenceFrameRoundTrip() {
    std::vector<chatbox::PresenceEntry> entries;
    entries.push_back(chatbox::PresenceEntry{7, true, 100});
    entries.push_back(chatbox::PresenceEntry{9, false, 101});

    const chatbox::Frame frame = chatbox::makePresencePushFrame(entries);
    CHECK_EQ(frame.header.cmd, chatbox::kCmdPresencePush);
    CHECK_EQ(frame.header.flags, 0);  // push, not a response

    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint16_t count = 0;
    CHECK_TRUE(reader.getU16(count));
    CHECK_EQ(count, static_cast<uint16_t>(2));

    uint64_t user_id = 0;
    uint8_t online = 0;
    uint64_t seq = 0;
    CHECK_TRUE(reader.getU64(user_id));
    CHECK_TRUE(reader.getU8(online));
    CHECK_TRUE(reader.getU64(seq));
    CHECK_EQ(user_id, static_cast<uint64_t>(7));
    CHECK_EQ(online, static_cast<uint8_t>(1));
    CHECK_EQ(seq, static_cast<uint64_t>(100));

    CHECK_TRUE(reader.getU64(user_id));
    CHECK_TRUE(reader.getU8(online));
    CHECK_TRUE(reader.getU64(seq));
    CHECK_EQ(user_id, static_cast<uint64_t>(9));
    CHECK_EQ(online, static_cast<uint8_t>(0));
    CHECK_EQ(seq, static_cast<uint64_t>(101));
    CHECK_EQ(reader.remaining(), static_cast<std::size_t>(0));
    return 0;
}

int testEmptyPresenceFrame() {
    const chatbox::Frame frame = chatbox::makePresencePushFrame({});
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint16_t count = 1;
    CHECK_TRUE(reader.getU16(count));
    CHECK_EQ(count, static_cast<uint16_t>(0));
    CHECK_EQ(reader.remaining(), static_cast<std::size_t>(0));
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testPresenceFrameRoundTrip();
    failures += testEmptyPresenceFrame();
    if (failures == 0) {
        std::cout << "test_presence: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
