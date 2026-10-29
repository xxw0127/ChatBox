#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "server/auth/session_manager.h"
#include "tests/test_framework.h"

namespace {

std::shared_ptr<chatbox::Connection> makeConnection(uint64_t id, int io_thread) {
    return std::make_shared<chatbox::Connection>(
        -1, id, io_thread, "127.0.0.1", false);
}

chatbox::SessionBinding makeBinding(
    uint64_t connection_id, const std::string& session_id,
    const std::string& device_id,
    const std::shared_ptr<chatbox::Connection>& connection) {
    chatbox::SessionBinding binding;
    binding.connection_id = connection_id;
    binding.io_thread_id = static_cast<int>(connection_id % 4);
    binding.peer_ip = "127.0.0.1";
    binding.connection = connection;
    binding.session_id = session_id;
    binding.token_hash = "hash-" + session_id;
    binding.device_id = device_id;
    binding.caps = chatbox::kCapMultiDevice;
    return binding;
}

int testFirstAddBecomesOnline() {
    chatbox::SessionManager sessions;
    auto connection = makeConnection(1, 0);
    const auto change = sessions.add(
        7, makeBinding(1, "session-1", "device-1", connection));
    CHECK_TRUE(change.became_online);
    CHECK_TRUE(!change.went_offline);
    CHECK_TRUE(change.presence_seq != 0);
    CHECK_TRUE(change.removed.empty());
    CHECK_TRUE(sessions.isOnline(7));
    return 0;
}

int testSecondAddKeepsOnlineWithoutSeq() {
    chatbox::SessionManager sessions;
    auto first = makeConnection(1, 0);
    auto second = makeConnection(2, 1);
    const auto first_change = sessions.add(
        7, makeBinding(1, "session-1", "device-1", first));
    const auto second_change = sessions.add(
        7, makeBinding(2, "session-2", "device-2", second));
    CHECK_TRUE(first_change.became_online);
    CHECK_TRUE(!second_change.became_online);
    CHECK_EQ(second_change.presence_seq, static_cast<uint64_t>(0));

    std::vector<chatbox::SessionBinding> bindings;
    CHECK_TRUE(sessions.findAll(7, bindings));
    CHECK_EQ(bindings.size(), static_cast<std::size_t>(2));
    return 0;
}

int testRemoveOneKeepsOnline() {
    chatbox::SessionManager sessions;
    auto first = makeConnection(1, 0);
    auto second = makeConnection(2, 1);
    sessions.add(7, makeBinding(1, "session-1", "device-1", first));
    sessions.add(7, makeBinding(2, "session-2", "device-2", second));

    const auto change = sessions.removeConnection(1);
    CHECK_EQ(change.user_id, static_cast<uint64_t>(7));
    CHECK_TRUE(!change.went_offline);
    CHECK_EQ(change.presence_seq, static_cast<uint64_t>(0));
    CHECK_TRUE(sessions.isOnline(7));

    std::vector<chatbox::SessionBinding> bindings;
    CHECK_TRUE(sessions.findAll(7, bindings));
    CHECK_EQ(bindings.size(), static_cast<std::size_t>(1));
    return 0;
}

int testRemoveLastGoesOffline() {
    chatbox::SessionManager sessions;
    auto connection = makeConnection(1, 0);
    sessions.add(7, makeBinding(1, "session-1", "device-1", connection));
    const auto change = sessions.removeConnection(1);
    CHECK_TRUE(change.went_offline);
    CHECK_TRUE(change.presence_seq != 0);
    CHECK_TRUE(!sessions.isOnline(7));
    return 0;
}

int testRemoveSessionOnlyRemovesTarget() {
    chatbox::SessionManager sessions;
    auto first = makeConnection(1, 0);
    auto second = makeConnection(2, 1);
    sessions.add(7, makeBinding(1, "session-1", "device-1", first));
    sessions.add(7, makeBinding(2, "session-2", "device-2", second));

    const auto change = sessions.removeSession(7, "session-2");
    CHECK_EQ(change.removed.size(), static_cast<std::size_t>(1));
    CHECK_EQ(change.removed[0].connection_id, static_cast<uint64_t>(2));
    CHECK_TRUE(!change.went_offline);

    chatbox::SessionBinding binding;
    CHECK_TRUE(!sessions.findBySession("session-2", binding));
    CHECK_TRUE(sessions.findBySession("session-1", binding));
    return 0;
}

int testDuplicateConnectionReplacesBinding() {
    chatbox::SessionManager sessions;
    auto first = makeConnection(1, 0);
    sessions.add(7, makeBinding(1, "session-1", "device-1", first));
    const auto change = sessions.add(
        7, makeBinding(1, "session-1b", "device-1", first));
    CHECK_EQ(change.removed.size(), static_cast<std::size_t>(1));
    CHECK_TRUE(!change.became_online);
    CHECK_EQ(change.presence_seq, static_cast<uint64_t>(0));

    std::vector<chatbox::SessionBinding> bindings;
    CHECK_TRUE(sessions.findAll(7, bindings));
    CHECK_EQ(bindings.size(), static_cast<std::size_t>(1));
    CHECK_EQ(bindings[0].session_id, std::string("session-1b"));
    return 0;
}

int testExpiredConnectionIsOffline() {
    chatbox::SessionManager sessions;
    chatbox::SessionBinding expired;
    expired.connection_id = 1;
    expired.io_thread_id = 0;
    expired.session_id = "session-expired";
    sessions.add(7, expired);

    CHECK_TRUE(!sessions.isOnline(7));
    std::vector<chatbox::SessionBinding> bindings;
    CHECK_TRUE(!sessions.findAll(7, bindings));
    CHECK_TRUE(sessions.targets().empty());

    const auto change = sessions.removeConnection(1);
    CHECK_EQ(change.user_id, static_cast<uint64_t>(7));
    CHECK_TRUE(change.went_offline);
    return 0;
}

int testActiveTokenHashesSkipExpired() {
    chatbox::SessionManager sessions;
    auto first = makeConnection(1, 0);
    auto second = makeConnection(2, 1);
    sessions.add(7, makeBinding(1, "session-1", "device-1", first));
    sessions.add(7, makeBinding(2, "session-2", "device-2", second));
    chatbox::SessionBinding expired;
    expired.connection_id = 3;
    expired.session_id = "session-3";
    expired.token_hash = "hash-session-3";
    sessions.add(8, expired);

    const std::vector<std::string> hashes = sessions.activeTokenHashes();
    CHECK_EQ(hashes.size(), static_cast<std::size_t>(2));
    bool found_first = false;
    bool found_second = false;
    for (const std::string& hash : hashes) {
        found_first = found_first || hash == "hash-session-1";
        found_second = found_second || hash == "hash-session-2";
    }
    CHECK_TRUE(found_first);
    CHECK_TRUE(found_second);
    return 0;
}

}  // namespace

int main() {
    int failures = 0;
    failures += testFirstAddBecomesOnline();
    failures += testSecondAddKeepsOnlineWithoutSeq();
    failures += testRemoveOneKeepsOnline();
    failures += testRemoveLastGoesOffline();
    failures += testRemoveSessionOnlyRemovesTarget();
    failures += testDuplicateConnectionReplacesBinding();
    failures += testExpiredConnectionIsOffline();
    failures += testActiveTokenHashesSkipExpired();
    if (failures == 0) {
        std::cout << "test_session_manager: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}


