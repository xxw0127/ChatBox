#ifndef CHATBOX_SERVER_BUSINESS_WORKER_POOL_H
#define CHATBOX_SERVER_BUSINESS_WORKER_POOL_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "server/auth/auth_service.h"
#include "server/auth/login_rate_limiter.h"
#include "server/auth/registration_rate_limiter.h"
#include "server/auth/session_manager.h"
#include "server/auth/user_lock_table.h"
#include "server/db/mysql_connection.h"
#include "server/friend/friend_service.h"
#include "server/file/file_service.h"
#include "server/group/group_service.h"
#include "server/history/history_service.h"
#include "server/private/private_service.h"
#include "server/profile/profile_service.h"
#include "server/room/room_manager.h"
#include "server/room/room_service.h"

namespace chatbox {

struct WorkerContext {
    MysqlConnection& database;
    AuthService& auth;
    RoomService& room;
    PrivateService& private_chat;
    HistoryService& history;
    FriendService& friend_chat;
    ProfileService& profile;
    GroupService& group;
    FileService& file;
};

class WorkerPool {
public:
    using Task = std::function<void(WorkerContext&)>;

    WorkerPool(int thread_count, DbConfig database_config, SessionManager& sessions,
               RoomManager& rooms, std::string files_dir,
               std::string ffmpeg_path = "/usr/bin/ffmpeg",
               std::string ffprobe_path = "/usr/bin/ffprobe");
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    bool start(std::string& error);
    bool submit(uint64_t key, Task task);
    void stop();

private:
    struct Worker {
        std::thread thread;
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<Task> queue;
        bool stopping = false;
    };

    void workerLoop(std::size_t index);

    int thread_count_;
    DbConfig database_config_;
    SessionManager& sessions_;
    RoomManager& rooms_;
    std::string files_dir_;
    std::string ffmpeg_path_;
    std::string ffprobe_path_;
    LoginRateLimiter login_limiter_{5, 60};
    RegistrationRateLimiter registration_limiter_{20, 100, 3600};
    UserLockTable user_locks_;
    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool> started_{false};
    std::atomic<bool> stopping_{false};
    std::mutex startup_mutex_;
    std::condition_variable startup_condition_;
    int ready_count_ = 0;
    bool startup_failed_ = false;
    std::string startup_error_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_BUSINESS_WORKER_POOL_H
