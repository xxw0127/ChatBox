#include <cstdint>
#include <iostream>

#include "server/auth/session_manager.h"
#include "tests/test_framework.h"

namespace {

int testPresenceSequenceAndTakeover() {
    chatbox::SessionManager sessions;

    chatbox::SessionBinding first;
    first.connection_id = 1;
    first.io_thread_id = 0;
    chatbox::SessionBinding old_binding;
    uint64_t seq1 = 0;
    CHECK_TRUE(!sessions.bind(7, first, old_binding, &seq1));
    CHECK_TRUE(seq1 != 0);

    chatbox::SessionBinding second;
    second.connection_id = 2;
    second.io_thread_id = 1;
    uint64_t seq2 = 0;
    CHECK_TRUE(sessions.bind(7, second, old_binding, &seq2));
    CHECK_TRUE(seq2 > seq1);
    CHECK_EQ(old_binding.connection_id, static_cast<uint64_t>(1));

    // The superseded connection must not be able to unbind the new session.
    uint64_t seq3 = 0;
    CHECK_TRUE(!sessions.unbind(7, 1, &seq3));
    CHECK_EQ(seq3, static_cast<uint64_t>(0));

    // The owning connection can.
    uint64_t seq4 = 0;
    CHECK_TRUE(sessions.unbind(7, 2, &seq4));
    CHECK_TRUE(seq4 > seq2);

    // A close callback for an already-superseded connection returns 0 and
    // assigns no sequence, so no false offline push is emitted.
    uint64_t seq5 = 0;
    CHECK_EQ(sessions.unbindByConnection(2, &seq5), static_cast<uint64_t>(0));
    CHECK_EQ(seq5, static_cast<uint64_t>(0));

    chatbox::SessionBinding third;
    third.connection_id = 3;
    third.io_thread_id = 2;
    uint64_t seq6 = 0;
    CHECK_TRUE(!sessions.bind(9, third, old_binding, &seq6));
    uint64_t seq7 = 0;
    CHECK_EQ(sessions.unbindByConnection(3, &seq7), static_cast<uint64_t>(9));
    CHECK_TRUE(seq7 > seq6);

    // Defensive: two users bound to one connection (a client that logged in
    // twice) are both drained, so neither can remain online forever.
    chatbox::SessionBinding fourth;
    fourth.connection_id = 4;
    fourth.io_thread_id = 3;
    uint64_t seq8 = 0;
    uint64_t seq9 = 0;
    CHECK_TRUE(!sessions.bind(11, fourth, old_binding, &seq8));
    CHECK_TRUE(!sessions.bind(12, fourth, old_binding, &seq9));
    uint64_t drain1 = 0;
    uint64_t drain2 = 0;
    uint64_t drain3 = 0;
    const uint64_t first_user = sessions.unbindByConnection(4, &drain1);
    const uint64_t second_user = sessions.unbindByConnection(4, &drain2);
    CHECK_TRUE((first_user == 11 && second_user == 12) ||
               (first_user == 12 && second_user == 11));
    CHECK_TRUE(drain1 != 0 && drain2 != 0);
    CHECK_EQ(sessions.unbindByConnection(4, &drain3), static_cast<uint64_t>(0));
    CHECK_EQ(drain3, static_cast<uint64_t>(0));
    return 0;
}

}  // namespace

int main() {
    const int failures = testPresenceSequenceAndTakeover();
    if (failures == 0) {
        std::cout << "test_session_manager: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
