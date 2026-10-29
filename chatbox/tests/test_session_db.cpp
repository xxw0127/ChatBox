#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "server/db/mysql_connection.h"
#include "tests/test_framework.h"

namespace {

std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value == nullptr || value[0] == '\0' ? fallback : value;
}

std::string makeToken(uint64_t base, int index) {
    char buffer[65] = {0};
    std::snprintf(buffer, sizeof(buffer), "%064llu",
                  static_cast<unsigned long long>(base * 100 + index));
    return std::string(buffer);
}

std::string makeSessionId(uint64_t base, int index) {
    return "test-" + std::to_string(base) + "-" + std::to_string(index);
}

chatbox::SessionRecord makeSession(uint64_t user_id, uint64_t base, int index,
                                   const std::string& device_id,
                                   const std::string& last_seen_at) {
    chatbox::SessionRecord session;
    session.user_id = user_id;
    session.session_id = makeSessionId(base, index);
    session.device_id = device_id;
    session.device_name = device_id.empty() ? "Legacy" : "Device " + device_id;
    session.platform = device_id.empty() ? "" : "windows";
    session.app_version = "0.4.0";
    session.caps = chatbox::kCapMultiDevice | chatbox::kCapSyncPull;
    session.created_at = "2026-10-29 10:00:00";
    session.last_seen_at = last_seen_at;
    session.expires_at = "2030-01-01 00:00:00";
    return session;
}

int testInsertAndFind(chatbox::MysqlConnection& db, uint64_t user_id) {
    chatbox::SessionRecord session = makeSession(user_id, user_id, 0, "device-a",
                                                 "2026-10-29 10:01:00");
    std::string error;
    CHECK_TRUE(db.insertSession(session, makeToken(user_id, 0), error));

    chatbox::SessionRecord found;
    bool present = false;
    CHECK_TRUE(db.findSession(makeToken(user_id, 0), found, present, error));
    CHECK_TRUE(present);
    CHECK_EQ(found.user_id, user_id);
    CHECK_EQ(found.session_id, session.session_id);
    CHECK_EQ(found.device_id, session.device_id);
    CHECK_EQ(found.device_name, session.device_name);
    CHECK_EQ(found.caps, session.caps);
    CHECK_TRUE(found.valid);
    return 0;
}

int testListOnlyCurrentUser(chatbox::MysqlConnection& db, uint64_t user_id) {
    const uint64_t other = user_id + 1;
    std::string error;
    CHECK_TRUE(db.insertSession(makeSession(user_id, user_id, 1, "device-b",
                                            "2026-10-29 10:02:00"),
                                makeToken(user_id, 1), error));
    CHECK_TRUE(db.insertSession(makeSession(user_id, user_id, 2, "device-c",
                                            "2026-10-29 10:03:00"),
                                makeToken(user_id, 2), error));
    CHECK_TRUE(db.insertSession(makeSession(other, other, 0, "device-x",
                                            "2026-10-29 10:04:00"),
                                makeToken(other, 0), error));

    std::vector<chatbox::SessionRecord> sessions;
    CHECK_TRUE(db.listSessions(user_id, sessions, error));
    CHECK_EQ(sessions.size(), static_cast<std::size_t>(2));
    for (const chatbox::SessionRecord& session : sessions) {
        CHECK_EQ(session.user_id, user_id);
    }
    return 0;
}

int testRevokeById(chatbox::MysqlConnection& db, uint64_t user_id) {
    std::string error;
    chatbox::SessionRecord session = makeSession(user_id, user_id, 3, "device-d",
                                                 "2026-10-29 10:05:00");
    CHECK_TRUE(db.insertSession(session, makeToken(user_id, 3), error));
    CHECK_TRUE(db.revokeSessionById(user_id, session.session_id, 4, error));

    chatbox::SessionRecord found;
    bool present = false;
    CHECK_TRUE(db.findSession(makeToken(user_id, 3), found, present, error));
    CHECK_TRUE(present);
    CHECK_EQ(found.revoked, 4);
    return 0;
}

int testRevokeForDeviceKeepsOther(chatbox::MysqlConnection& db,
                                  uint64_t user_id) {
    std::string error;
    chatbox::SessionRecord first = makeSession(user_id, user_id, 4, "device-e",
                                               "2026-10-29 10:06:00");
    chatbox::SessionRecord second = makeSession(user_id, user_id, 5, "device-f",
                                                "2026-10-29 10:07:00");
    CHECK_TRUE(db.insertSession(first, makeToken(user_id, 4), error));
    CHECK_TRUE(db.insertSession(second, makeToken(user_id, 5), error));
    CHECK_TRUE(db.revokeSessionsForDevice(user_id, "device-e", 4, error));

    chatbox::SessionRecord found;
    bool present = false;
    CHECK_TRUE(db.findSession(makeToken(user_id, 4), found, present, error));
    CHECK_TRUE(present);
    CHECK_EQ(found.revoked, 4);

    CHECK_TRUE(db.findSession(makeToken(user_id, 5), found, present, error));
    CHECK_TRUE(present);
    CHECK_EQ(found.revoked, 0);
    return 0;
}

int testRevokeLegacyOnly(chatbox::MysqlConnection& db, uint64_t user_id) {
    std::string error;
    chatbox::SessionRecord legacy = makeSession(user_id, user_id, 6, "",
                                                "2026-10-29 10:08:00");
    chatbox::SessionRecord modern = makeSession(user_id, user_id, 7, "device-g",
                                                "2026-10-29 10:09:00");
    CHECK_TRUE(db.insertSession(legacy, makeToken(user_id, 6), error));
    CHECK_TRUE(db.insertSession(modern, makeToken(user_id, 7), error));
    CHECK_TRUE(db.revokeLegacySessions(user_id, 1, error));

    chatbox::SessionRecord found;
    bool present = false;
    CHECK_TRUE(db.findSession(makeToken(user_id, 6), found, present, error));
    CHECK_TRUE(present);
    CHECK_EQ(found.revoked, 1);

    CHECK_TRUE(db.findSession(makeToken(user_id, 7), found, present, error));
    CHECK_TRUE(present);
    CHECK_EQ(found.revoked, 0);
    return 0;
}

int testCountAndEvictOldest(chatbox::MysqlConnection& db, uint64_t user_id) {
    std::string error;
    chatbox::SessionRecord oldest = makeSession(user_id, user_id, 8, "device-h",
                                                "2026-10-29 10:10:00");
    chatbox::SessionRecord middle = makeSession(user_id, user_id, 9, "device-i",
                                                "2026-10-29 10:11:00");
    chatbox::SessionRecord newest = makeSession(user_id, user_id, 10, "device-j",
                                                "2026-10-29 10:12:00");
    CHECK_TRUE(db.insertSession(oldest, makeToken(user_id, 8), error));
    CHECK_TRUE(db.insertSession(middle, makeToken(user_id, 9), error));
    CHECK_TRUE(db.insertSession(newest, makeToken(user_id, 10), error));

    uint32_t count = 0;
    CHECK_TRUE(db.countActiveNewSessions(user_id, count, error));
    CHECK_EQ(count, static_cast<uint32_t>(3));

    std::string revoked_session_id;
    CHECK_TRUE(db.revokeOldestNewSession(user_id, 5, revoked_session_id, error));
    CHECK_EQ(revoked_session_id, oldest.session_id);

    CHECK_TRUE(db.countActiveNewSessions(user_id, count, error));
    CHECK_EQ(count, static_cast<uint32_t>(2));

    chatbox::SessionRecord found;
    bool present = false;
    CHECK_TRUE(db.findSession(makeToken(user_id, 8), found, present, error));
    CHECK_TRUE(present);
    CHECK_EQ(found.revoked, 5);
    return 0;
}

int testTouchSessionExtendsExpiry(chatbox::MysqlConnection& db,
                                  uint64_t user_id) {
    std::string error;
    chatbox::SessionRecord session = makeSession(user_id, user_id, 11, "device-k",
                                                 "2026-10-29 10:13:00");
    session.expires_at = "2027-01-01 00:00:00";
    CHECK_TRUE(db.insertSession(session, makeToken(user_id, 11), error));
    CHECK_TRUE(db.touchSession(makeToken(user_id, 11), "2031-01-01 00:00:00", error));

    chatbox::SessionRecord found;
    bool present = false;
    CHECK_TRUE(db.findSession(makeToken(user_id, 11), found, present, error));
    CHECK_TRUE(present);
    CHECK_EQ(found.expires_at, std::string("2031-01-01 00:00:00"));
    return 0;
}

}  // namespace

int main() {
    const std::string password = envOr("CHATBOX_TEST_DB_PASSWORD", "");
    if (password.empty()) {
        std::cout << "test_session_db: skipped (CHATBOX_TEST_DB_PASSWORD not set)\n";
        return 77;
    }

    chatbox::DbConfig config;
    config.host = envOr("CHATBOX_TEST_DB_HOST", "127.0.0.1");
    config.port = static_cast<unsigned int>(
        std::strtoul(envOr("CHATBOX_TEST_DB_PORT", "3306").c_str(), nullptr, 10));
    config.user = envOr("CHATBOX_TEST_DB_USER", "chatbox");
    config.password = password;
    config.database = envOr("CHATBOX_TEST_DB_NAME", "chatbox_test");

    chatbox::MysqlConnection db;
    std::string error;
    if (!db.connect(config, error)) {
        std::cerr << "test_session_db: connect failed: " << error << '\n';
        return 1;
    }

    const uint64_t user_id =
        900000000ULL + static_cast<uint64_t>(std::time(nullptr)) % 1000000ULL;
    int failures = 0;
    failures += testInsertAndFind(db, user_id);
    failures += testListOnlyCurrentUser(db, user_id + 10);
    failures += testRevokeById(db, user_id + 20);
    failures += testRevokeForDeviceKeepsOther(db, user_id + 30);
    failures += testRevokeLegacyOnly(db, user_id + 40);
    failures += testCountAndEvictOldest(db, user_id + 50);
    failures += testTouchSessionExtendsExpiry(db, user_id + 60);
    if (failures == 0) {
        std::cout << "test_session_db: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
