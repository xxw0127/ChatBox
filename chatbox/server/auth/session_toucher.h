#ifndef CHATBOX_SERVER_AUTH_SESSION_TOUCHER_H
#define CHATBOX_SERVER_AUTH_SESSION_TOUCHER_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "server/db/mysql_connection.h"

namespace chatbox {

// Periodically refreshes last_seen_at / expires_at for online sessions so a
// long-lived connection does not expire and so device eviction sees fresh
// activity. It owns its own MySQL connection (one per thread).
class SessionToucher {
public:
    SessionToucher(DbConfig config, int interval_seconds = 600);
    ~SessionToucher();

    SessionToucher(const SessionToucher&) = delete;
    SessionToucher& operator=(const SessionToucher&) = delete;

    void start();
    void stop();

private:
    void loop();

    DbConfig config_;
    int interval_seconds_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable condition_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_SESSION_TOUCHER_H
