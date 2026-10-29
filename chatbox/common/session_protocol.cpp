#include "common/session_protocol.h"

#include <algorithm>
#include <limits>

namespace chatbox {

namespace {

bool fits(const std::string& value, std::size_t max_size) {
    return value.size() <= max_size;
}

bool validString(const std::string& value, std::size_t max_size,
                 bool allow_empty) {
    return fits(value, max_size) && (allow_empty || !value.empty());
}

}  // namespace

void appendDeviceInfo(ByteWriter& writer, const DeviceInfo& info) {
    writer.putU8(1);
    writer.putString(info.device_id);
    writer.putString(info.device_name);
    writer.putString(info.platform);
    writer.putString(info.app_version);
    writer.putU32(info.caps);
}

bool readDeviceInfo(ByteReader& reader, DeviceInfo& info, bool& present) {
    present = false;
    info = DeviceInfo{};
    if (reader.remaining() == 0) {
        return true;
    }

    uint8_t has_device = 0;
    if (!reader.getU8(has_device)) {
        return false;
    }
    if (has_device == 0) {
        return true;
    }
    if (has_device != 1) {
        return false;
    }
    present = true;
    if (!reader.getString(info.device_id) ||
        !reader.getString(info.device_name) ||
        !reader.getString(info.platform) ||
        !reader.getString(info.app_version) ||
        !reader.getU32(info.caps)) {
        return false;
    }
    return validString(info.device_id, 36, false) &&
           validString(info.device_name, 64, true) &&
           validString(info.platform, 16, true) &&
           validString(info.app_version, 32, true);
}

void appendAuthExtras(ByteWriter& writer, uint32_t server_caps,
                      uint32_t ttl_seconds) {
    writer.putU32(server_caps);
    writer.putU32(ttl_seconds);
}

bool readAuthExtras(ByteReader& reader, uint32_t& server_caps,
                    uint32_t& ttl_seconds) {
    server_caps = 0;
    ttl_seconds = 0;
    if (reader.remaining() < 8) {
        return false;
    }
    return reader.getU32(server_caps) && reader.getU32(ttl_seconds);
}

void appendSessionList(ByteWriter& writer,
                       const std::vector<SessionInfo>& sessions) {
    const std::size_t count =
        std::min<std::size_t>(sessions.size(), std::numeric_limits<uint16_t>::max());
    writer.putU16(static_cast<uint16_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
        const SessionInfo& session = sessions[i];
        writer.putString(session.session_id);
        writer.putString(session.device_id);
        writer.putString(session.device_name);
        writer.putString(session.platform);
        writer.putString(session.app_version);
        writer.putString(session.created_at);
        writer.putString(session.last_seen_at);
        writer.putU8(session.current ? 1 : 0);
    }
}

bool readSessionList(ByteReader& reader,
                     std::vector<SessionInfo>& sessions) {
    sessions.clear();
    uint16_t count = 0;
    if (!reader.getU16(count) || count > 1024) {
        return false;
    }
    sessions.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        SessionInfo session;
        uint8_t current = 0;
        if (!reader.getString(session.session_id) ||
            !reader.getString(session.device_id) ||
            !reader.getString(session.device_name) ||
            !reader.getString(session.platform) ||
            !reader.getString(session.app_version) ||
            !reader.getString(session.created_at) ||
            !reader.getString(session.last_seen_at) ||
            !reader.getU8(current)) {
            return false;
        }
        if (!validString(session.session_id, 36, true) ||
            !validString(session.device_id, 36, true) ||
            !validString(session.device_name, 64, true) ||
            !validString(session.platform, 16, true) ||
            !validString(session.app_version, 32, true) ||
            !validString(session.created_at, 32, true) ||
            !validString(session.last_seen_at, 32, true)) {
            return false;
        }
        session.current = current != 0;
        sessions.push_back(std::move(session));
    }
    return true;
}

void appendSyncPull(ByteWriter& writer,
                    const std::vector<SyncCursor>& cursors,
                    uint16_t start_index) {
    const std::size_t count =
        std::min<std::size_t>(cursors.size(), std::numeric_limits<uint16_t>::max());
    writer.putU16(static_cast<uint16_t>(count));
    writer.putU16(start_index);
    for (std::size_t i = 0; i < count; ++i) {
        writer.putU8(cursors[i].kind);
        writer.putU64(cursors[i].target_id);
        writer.putU64(cursors[i].last_message_id);
    }
}

bool readSyncPull(ByteReader& reader, std::vector<SyncCursor>& cursors,
                  uint16_t& start_index) {
    cursors.clear();
    start_index = 0;
    uint16_t count = 0;
    if (!reader.getU16(count) || !reader.getU16(start_index) || count > 1024) {
        return false;
    }
    if (start_index > count) {
        return false;
    }
    cursors.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        SyncCursor cursor;
        if (!reader.getU8(cursor.kind) || !reader.getU64(cursor.target_id) ||
            !reader.getU64(cursor.last_message_id)) {
            return false;
        }
        if ((cursor.kind != 1 && cursor.kind != 2) || cursor.target_id == 0) {
            return false;
        }
        cursors.push_back(cursor);
    }
    return true;
}

void appendReadState(ByteWriter& writer, const ReadStateEvent& event) {
    writer.putU8(event.kind);
    writer.putU64(event.target_id);
    writer.putU64(event.last_read_message_id);
    writer.putU32(event.unread);
}

bool readReadState(ByteReader& reader, ReadStateEvent& event) {
    event = ReadStateEvent{};
    if (!reader.getU8(event.kind) || !reader.getU64(event.target_id) ||
        !reader.getU64(event.last_read_message_id) ||
        !reader.getU32(event.unread)) {
        return false;
    }
    return (event.kind == 1 || event.kind == 2) && event.target_id != 0;
}

}  // namespace chatbox
