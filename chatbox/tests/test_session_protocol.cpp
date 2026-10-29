#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "common/session_protocol.h"
#include "tests/test_framework.h"

namespace {

int testDeviceInfoRoundTrip() {
    chatbox::DeviceInfo info;
    info.device_id = "device-1234";
    info.device_name = "Pixel 8";
    info.platform = "android";
    info.app_version = "0.1.0";
    info.caps = chatbox::kCapMultiDevice | chatbox::kCapSyncPull;

    chatbox::ByteWriter writer;
    chatbox::appendDeviceInfo(writer, info);
    chatbox::ByteReader reader(writer.data().data(), writer.data().size());
    chatbox::DeviceInfo decoded;
    bool present = false;
    CHECK_TRUE(chatbox::readDeviceInfo(reader, decoded, present));
    CHECK_TRUE(present);
    CHECK_EQ(decoded.device_id, info.device_id);
    CHECK_EQ(decoded.device_name, info.device_name);
    CHECK_EQ(decoded.platform, info.platform);
    CHECK_EQ(decoded.app_version, info.app_version);
    CHECK_EQ(decoded.caps, info.caps);
    CHECK_EQ(reader.remaining(), static_cast<std::size_t>(0));
    return 0;
}

int testLegacyBodyHasNoDeviceInfo() {
    const char empty = 0;
    chatbox::ByteReader reader(&empty, 0);
    chatbox::DeviceInfo info;
    bool present = true;
    CHECK_TRUE(chatbox::readDeviceInfo(reader, info, present));
    CHECK_TRUE(!present);
    return 0;
}

int testAuthExtrasRoundTrip() {
    chatbox::ByteWriter writer;
    chatbox::appendAuthExtras(writer, chatbox::kCapSyncPull, 30 * 24 * 3600);
    chatbox::ByteReader reader(writer.data().data(), writer.data().size());
    uint32_t caps = 0;
    uint32_t ttl = 0;
    CHECK_TRUE(chatbox::readAuthExtras(reader, caps, ttl));
    CHECK_EQ(caps, chatbox::kCapSyncPull);
    CHECK_EQ(ttl, static_cast<uint32_t>(30 * 24 * 3600));
    return 0;
}

int testMissingAuthExtrasReturnsFalse() {
    const char empty = 0;
    chatbox::ByteReader reader(&empty, 0);
    uint32_t caps = 1;
    uint32_t ttl = 1;
    CHECK_TRUE(!chatbox::readAuthExtras(reader, caps, ttl));
    CHECK_EQ(caps, static_cast<uint32_t>(0));
    CHECK_EQ(ttl, static_cast<uint32_t>(0));
    return 0;
}

int testSessionListRoundTrip() {
    std::vector<chatbox::SessionInfo> sessions;
    chatbox::SessionInfo first;
    first.session_id = "session-a";
    first.device_id = "device-a";
    first.device_name = "Desktop";
    first.platform = "windows";
    first.app_version = "0.4.0";
    first.created_at = "2026-10-29 10:00:00";
    first.last_seen_at = "2026-10-29 11:00:00";
    first.current = true;
    sessions.push_back(first);

    chatbox::SessionInfo second;
    second.session_id = "session-b";
    second.device_id = "device-b";
    second.device_name = "Phone";
    second.platform = "android";
    second.app_version = "0.1.0";
    second.created_at = "2026-10-29 10:05:00";
    second.last_seen_at = "2026-10-29 11:05:00";
    sessions.push_back(second);

    chatbox::ByteWriter writer;
    chatbox::appendSessionList(writer, sessions);
    chatbox::ByteReader reader(writer.data().data(), writer.data().size());
    std::vector<chatbox::SessionInfo> decoded;
    CHECK_TRUE(chatbox::readSessionList(reader, decoded));
    CHECK_EQ(decoded.size(), static_cast<std::size_t>(2));
    CHECK_EQ(decoded[0].session_id, first.session_id);
    CHECK_EQ(decoded[0].current, true);
    CHECK_EQ(decoded[1].platform, second.platform);
    CHECK_EQ(decoded[1].current, false);
    return 0;
}

int testSyncPullRoundTrip() {
    std::vector<chatbox::SyncCursor> cursors;
    cursors.push_back(chatbox::SyncCursor{1, 42, 100});
    cursors.push_back(chatbox::SyncCursor{2, 7, 0});

    chatbox::ByteWriter writer;
    chatbox::appendSyncPull(writer, cursors, 1);
    chatbox::ByteReader reader(writer.data().data(), writer.data().size());
    std::vector<chatbox::SyncCursor> decoded;
    uint16_t start_index = 0;
    CHECK_TRUE(chatbox::readSyncPull(reader, decoded, start_index));
    CHECK_EQ(start_index, static_cast<uint16_t>(1));
    CHECK_EQ(decoded.size(), static_cast<std::size_t>(2));
    CHECK_EQ(decoded[0].kind, static_cast<uint8_t>(1));
    CHECK_EQ(decoded[0].target_id, static_cast<uint64_t>(42));
    CHECK_EQ(decoded[0].last_message_id, static_cast<uint64_t>(100));
    CHECK_EQ(decoded[1].kind, static_cast<uint8_t>(2));
    return 0;
}

int testReadStateRoundTrip() {
    chatbox::ReadStateEvent event;
    event.kind = 2;
    event.target_id = 99;
    event.last_read_message_id = 1234;
    event.unread = 3;

    chatbox::ByteWriter writer;
    chatbox::appendReadState(writer, event);
    chatbox::ByteReader reader(writer.data().data(), writer.data().size());
    chatbox::ReadStateEvent decoded;
    CHECK_TRUE(chatbox::readReadState(reader, decoded));
    CHECK_EQ(decoded.kind, event.kind);
    CHECK_EQ(decoded.target_id, event.target_id);
    CHECK_EQ(decoded.last_read_message_id, event.last_read_message_id);
    CHECK_EQ(decoded.unread, event.unread);
    return 0;
}

int testInvalidDeviceInfoRejected() {
    chatbox::ByteWriter writer;
    writer.putU8(2);
    chatbox::ByteReader reader(writer.data().data(), writer.data().size());
    chatbox::DeviceInfo info;
    bool present = false;
    CHECK_TRUE(!chatbox::readDeviceInfo(reader, info, present));
    return 0;
}

int testInvalidSyncCursorRejected() {
    chatbox::ByteWriter writer;
    writer.putU16(1);
    writer.putU16(0);
    writer.putU8(3);
    writer.putU64(1);
    writer.putU64(0);
    chatbox::ByteReader reader(writer.data().data(), writer.data().size());
    std::vector<chatbox::SyncCursor> cursors;
    uint16_t start_index = 0;
    CHECK_TRUE(!chatbox::readSyncPull(reader, cursors, start_index));
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testDeviceInfoRoundTrip();
    failures += testLegacyBodyHasNoDeviceInfo();
    failures += testAuthExtrasRoundTrip();
    failures += testMissingAuthExtrasReturnsFalse();
    failures += testSessionListRoundTrip();
    failures += testSyncPullRoundTrip();
    failures += testReadStateRoundTrip();
    failures += testInvalidDeviceInfoRejected();
    failures += testInvalidSyncCursorRejected();
    if (failures == 0) {
        std::cout << "test_session_protocol: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
