#include "server/auth/session_toucher.h"

#include <chrono>
#include <ctime>
#include <string>
#include <vector>

#include "server/util/logging.h"

namespace chatbox {

namespace {

constexpr int kTokenTtlDays = 30;

std::string utcPlusDays(int days) {
    const std::time_t target = std::time(nullptr) + static_cast<std::time_t>(days) * 86400;
    std::tm utc_time{};
    gmtime_r(&target, &utc_time);
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &utc_time);
    return buffer;
}

}  // namespace

SessionToucher::SessionToucher(DbConfig config, const SessionManager& sessions,
                               int interval_seconds)
    : config_(std::move(config)),
      sessions_(sessions),
      interval_seconds_(interval_seconds > 0 ? interval_seconds : 600) {}

SessionToucher::~SessionToucher() {
    stop();
}

void SessionToucher::start() {
    if (running_.exchange(true)) {
        return;
    }
    thread_ = std::thread([this]() { loop(); });
}

void SessionToucher::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    condition_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SessionToucher::loop() {
    MysqlConnection database;
    bool connected = false;
    while (running_.load()) {
        if (!connected) {
            std::string error;
            if (database.connect(config_, error)) {
                connected = true;
            } else {
                logMessage(LogLevel::kWarn, "session", "toucher connect failed: " + error);
            }
        }
        if (connected) {
            const std::vector<std::string> token_hashes =
                sessions_.activeTokenHashes();
            if (!token_hashes.empty()) {
                std::string error;
                if (!database.touchSessions(token_hashes, utcPlusDays(kTokenTtlDays),
                                            error)) {
                    logMessage(LogLevel::kWarn, "session",
                               "touch sessions failed: " + error);
                    database.close();
                    connected = false;
                }
            }
        }

        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait_for(lock, std::chrono::seconds(interval_seconds_), [this]() {
            return !running_.load();
        });
    }
    database.close();
}

}  // namespace chatbox
