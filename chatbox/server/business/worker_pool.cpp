#include "server/business/worker_pool.h"

#include <utility>

#include "server/util/logging.h"

namespace chatbox {

namespace {
constexpr std::size_t kMaxQueueSize = 10000;
}  // namespace

WorkerPool::WorkerPool(int thread_count, DbConfig database_config, SessionManager& sessions,
                       RoomManager& rooms, std::string files_dir,
                       std::string ffmpeg_path, std::string ffprobe_path)
    : thread_count_(thread_count),
      database_config_(std::move(database_config)),
      sessions_(sessions),
      rooms_(rooms),
      files_dir_(std::move(files_dir)),
      ffmpeg_path_(std::move(ffmpeg_path)),
      ffprobe_path_(std::move(ffprobe_path)) {}

WorkerPool::~WorkerPool() {
    stop();
}

bool WorkerPool::start(std::string& error) {
    if (thread_count_ <= 0) {
        error = "worker thread count must be positive";
        return false;
    }
    if (started_) {
        return true;
    }

    workers_.clear();
    workers_.reserve(static_cast<std::size_t>(thread_count_));
    for (int i = 0; i < thread_count_; ++i) {
        auto worker = std::make_unique<Worker>();
        workers_.push_back(std::move(worker));
    }

    for (std::size_t i = 0; i < workers_.size(); ++i) {
        workers_[i]->thread = std::thread(&WorkerPool::workerLoop, this, i);
    }

    std::unique_lock<std::mutex> lock(startup_mutex_);
    startup_condition_.wait(lock, [this] {
        return startup_failed_ || ready_count_ == thread_count_;
    });
    if (startup_failed_) {
        error = startup_error_;
        lock.unlock();
        stop();
        return false;
    }

    started_ = true;
    error.clear();
    logMessage(LogLevel::kInfo, "worker",
               "worker pool started with " + std::to_string(thread_count_) + " threads");
    return true;
}

bool WorkerPool::submit(uint64_t key, Task task) {
    if (!started_ || stopping_ || workers_.empty()) {
        return false;
    }
    Worker& worker = *workers_[static_cast<std::size_t>(key % workers_.size())];
    std::lock_guard<std::mutex> lock(worker.mutex);
    if (worker.stopping || worker.queue.size() >= kMaxQueueSize) {
        return false;
    }
    worker.queue.push_back(std::move(task));
    worker.condition.notify_one();
    return true;
}

void WorkerPool::stop() {
    stopping_ = true;
    for (auto& worker : workers_) {
        {
            std::lock_guard<std::mutex> lock(worker->mutex);
            worker->stopping = true;
        }
        worker->condition.notify_all();
    }
    for (auto& worker : workers_) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
    if (started_) {
        started_ = false;
        logMessage(LogLevel::kInfo, "worker", "worker pool stopped");
    }
}

void WorkerPool::workerLoop(std::size_t index) {
    if (mysql_thread_init() != 0) {
        std::lock_guard<std::mutex> lock(startup_mutex_);
        startup_failed_ = true;
        startup_error_ = "mysql_thread_init failed";
        startup_condition_.notify_all();
        return;
    }

    auto database = std::make_unique<MysqlConnection>();
    std::string error;
    if (!database->connect(database_config_, error)) {
        const std::string failure =
            "worker " + std::to_string(index) + " database connect failed: " + error;
        {
            std::lock_guard<std::mutex> lock(startup_mutex_);
            startup_failed_ = true;
            startup_error_ = failure;
            startup_condition_.notify_all();
        }
        logMessage(LogLevel::kError, "worker", failure);
        mysql_thread_end();
        return;
    }

    auto auth = std::make_unique<AuthService>(*database, sessions_, user_locks_,
                                              login_limiter_, registration_limiter_);
    auto room = std::make_unique<RoomService>(*database, rooms_);
    auto private_chat = std::make_unique<PrivateService>(*database, sessions_);
    auto history = std::make_unique<HistoryService>(*database, rooms_);
    auto friend_chat = std::make_unique<FriendService>(*database, sessions_);
    auto profile = std::make_unique<ProfileService>(*database, sessions_);
    auto group = std::make_unique<GroupService>(*database, rooms_, files_dir_);
    auto file = std::make_unique<FileService>(*database, rooms_, files_dir_,
                                             ffmpeg_path_, ffprobe_path_);
    {
        std::lock_guard<std::mutex> lock(startup_mutex_);
        ++ready_count_;
        startup_condition_.notify_all();
    }

    Worker& worker = *workers_[index];
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(worker.mutex);
            worker.condition.wait(lock, [&worker] {
                return worker.stopping || !worker.queue.empty();
            });
            if (worker.stopping && worker.queue.empty()) {
                break;
            }
            task = std::move(worker.queue.front());
            worker.queue.pop_front();
        }
        if (task) {
            WorkerContext context{*database, *auth,        *room, *private_chat,
                                  *history,   *friend_chat, *profile, *group,
                                  *file};
            task(context);
        }
    }

    mysql_thread_end();
}

}  // namespace chatbox
