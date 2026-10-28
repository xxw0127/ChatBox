#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mysql/mysql.h>
#include <pthread.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/protocol.h"
#include "server/auth/session_manager.h"
#include "server/business/worker_pool.h"
#include "server/config.h"
#include "server/db/mysql_connection.h"
#include "server/net/acceptor.h"
#include "server/net/connection_limiter.h"
#include "server/net/io_thread.h"
#include "server/presence/presence_service.h"
#include "server/room/room_manager.h"
#include "server/room/room_service.h"
#include "server/util/logging.h"
#include "server/util/metrics.h"

namespace {

using chatbox::ByteWriter;
using chatbox::Connection;
using chatbox::Frame;
using chatbox::LogLevel;

Frame makeErrorResponse(uint32_t seq, uint16_t code, const std::string& message) {
    Frame response;
    response.header.flags = chatbox::kFlagResponse | chatbox::kFlagError;
    response.header.cmd = chatbox::kCmdError;
    response.header.seq = seq;
    ByteWriter writer;
    writer.putU16(code);
    writer.putString(message);
    response.body = writer.data();
    return response;
}

// A user can disappear through an explicit LOGOUT or a raw connection close.
// Both paths must push the remaining member list; otherwise every other client
// keeps a stale presence row until it happens to re-enter the room.
void broadcastChangedRoomMembers(
    chatbox::RoomManager& rooms,
    const std::vector<chatbox::IoThread*>& io_threads,
    const std::vector<int>& room_ids) {
    for (const int room_id : room_ids) {
        std::vector<chatbox::RoomMember> members;
        if (!rooms.snapshot(room_id, members) || members.empty()) {
            continue;
        }
        const Frame frame = chatbox::makeRoomMembersFrame(room_id, members);
        const std::string encoded = chatbox::encodeFrame(frame);
        std::unordered_map<int, std::vector<uint64_t>> groups;
        for (const chatbox::RoomMember& member : members) {
            if (member.io_thread_id >= 0 &&
                member.io_thread_id < static_cast<int>(io_threads.size())) {
                groups[member.io_thread_id].push_back(member.connection_id);
            }
        }
        for (const auto& group : groups) {
            io_threads[group.first]->postMulticast(group.second, encoded);
        }
    }
}

// Presence events are rare (login / logout / disconnect), so fanning one
// small frame out to every authenticated session is cheaper than maintaining
// friend and room audiences in two places. The sender's own connection is
// excluded: it does not need to learn its own state.
void broadcastPresence(const chatbox::SessionManager& sessions,
                       const std::vector<chatbox::IoThread*>& io_threads,
                       const chatbox::PresenceEntry& entry,
                       uint64_t exclude_connection_id) {
    if (entry.user_id == 0) {
        return;
    }
    const std::vector<chatbox::SessionTarget> targets = sessions.targets();
    std::unordered_map<int, std::vector<uint64_t>> groups;
    for (const chatbox::SessionTarget& target : targets) {
        if (target.connection_id == exclude_connection_id) {
            continue;
        }
        if (target.io_thread_id < 0 ||
            target.io_thread_id >= static_cast<int>(io_threads.size())) {
            continue;
        }
        groups[target.io_thread_id].push_back(target.connection_id);
    }
    if (groups.empty()) {
        return;
    }
    const std::string encoded =
        chatbox::encodeFrame(chatbox::makePresencePushFrame({entry}));
    for (const auto& group : groups) {
        io_threads[group.first]->postMulticast(group.second, encoded);
    }
}

}  // namespace

int main(int argc, char** argv) {
    chatbox::Config config;
    std::string error;
    if (!chatbox::parseArgs(argc, argv, config, error)) {
        std::cerr << "argument error: " << error << '\n';
        chatbox::printUsage(argv[0]);
        return 2;
    }
    if (config.show_help) {
        chatbox::printUsage(argv[0]);
        return 0;
    }

    const char* database_password = std::getenv("CHATBOX_DB_PASSWORD");
    if (database_password == nullptr || database_password[0] == '\0') {
        std::cerr << "CHATBOX_DB_PASSWORD is required\n";
        return 2;
    }
    config.db_password = database_password;

    if (!config.log_file.empty()) {
        std::error_code ec;
        const std::filesystem::path log_path(config.log_file);
        if (log_path.has_parent_path()) {
            std::filesystem::create_directories(log_path.parent_path(), ec);
        }
    }
    chatbox::initLogging(config.log_file, config.log_level);
    chatbox::logMessage(LogLevel::kInfo, "main",
                         "starting chatbox_server port=" + std::to_string(config.port) +
                             " io_threads=" + std::to_string(config.io_threads) +
                             " worker_threads=" + std::to_string(config.worker_threads) +
                             " idle_timeout=" + std::to_string(config.idle_timeout_sec));

    std::signal(SIGPIPE, SIG_IGN);

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGTERM);
    sigaddset(&signal_set, SIGUSR1);
    if (pthread_sigmask(SIG_BLOCK, &signal_set, nullptr) != 0) {
        chatbox::logMessage(LogLevel::kError, "main", "failed to block signals");
        return 1;
    }

    if (mysql_library_init(0, nullptr, nullptr) != 0) {
        chatbox::logMessage(LogLevel::kError, "main", "mysql_library_init failed");
        return 1;
    }

    chatbox::SessionManager sessions;
    chatbox::DbConfig db_config;
    db_config.host = config.db_host;
    db_config.port = static_cast<unsigned int>(config.db_port);
    db_config.user = config.db_user;
    db_config.password = config.db_password;
    db_config.database = config.db_name;

    if (mysql_thread_init() != 0) {
        chatbox::logMessage(LogLevel::kError, "main", "mysql_thread_init failed");
        mysql_library_end();
        return 1;
    }
    std::vector<chatbox::RoomRow> room_rows;
    {
        chatbox::MysqlConnection loader;
        if (!loader.connect(db_config, error)) {
            chatbox::logMessage(LogLevel::kError, "main", "database connect failed: " + error);
            mysql_thread_end();
            mysql_library_end();
            return 1;
        }
        if (!loader.listRooms(room_rows, error)) {
            chatbox::logMessage(LogLevel::kError, "main", "load rooms failed: " + error);
            mysql_thread_end();
            mysql_library_end();
            return 1;
        }
    }
    mysql_thread_end();

    if (room_rows.empty()) {
        chatbox::logMessage(LogLevel::kError, "main", "no rooms configured");
        mysql_library_end();
        return 1;
    }

    std::vector<chatbox::RoomInfo> room_infos;
    room_infos.reserve(room_rows.size());
    for (const auto& row : room_rows) {
        chatbox::RoomInfo info;
        info.id = row.id;
        info.name = row.name;
        info.max_members = row.max_members;
        room_infos.push_back(std::move(info));
    }
    chatbox::RoomManager room_manager(std::move(room_infos));
    chatbox::ConnectionLimiter connection_limiter(config.max_connections,
                                                   config.max_conn_per_ip);

    chatbox::WorkerPool worker_pool(config.worker_threads, db_config, sessions,
                                     room_manager);
    if (!worker_pool.start(error)) {
        chatbox::logMessage(LogLevel::kError, "main", "worker pool start failed: " + error);
        mysql_library_end();
        return 1;
    }

    std::vector<std::unique_ptr<chatbox::IoThread>> io_threads;
    std::vector<chatbox::IoThread*> io_thread_refs;
    io_threads.reserve(static_cast<std::size_t>(config.io_threads));
    io_thread_refs.reserve(static_cast<std::size_t>(config.io_threads));

    auto frame_handler = [&worker_pool, &io_thread_refs, &room_manager, &sessions](
                             const Connection::Ptr& connection, const Frame& frame,
                             chatbox::IoThread&) -> bool {
        if (frame.header.cmd == chatbox::kCmdPing) {
            Frame response;
            response.header.flags = chatbox::kFlagResponse;
            response.header.cmd = chatbox::kCmdPing;
            response.header.seq = frame.header.seq;
            response.body = frame.body;
            return connection->enqueue(chatbox::encodeFrame(response));
        }

        const uint16_t command = frame.header.cmd;
        const bool recall_command = command == chatbox::kCmdMessageRecall;
        const bool recall_room =
            recall_command && !frame.body.empty() &&
            static_cast<unsigned char>(frame.body[0]) == 1;
        const bool auth_command =
            command >= chatbox::kCmdRegister && command <= chatbox::kCmdResume;
        const bool room_command =
            (command >= chatbox::kCmdRoomList && command <= chatbox::kCmdRoomMembers) ||
            command == chatbox::kCmdChatSend || recall_room;
        const bool private_command = command == chatbox::kCmdPrivateSend ||
                                     command == chatbox::kCmdPrivateConversations ||
                                     command == chatbox::kCmdPrivateMarkRead ||
                                     command == chatbox::kCmdUserSearch ||
                                     (recall_command && !recall_room);
        const bool history_command = command == chatbox::kCmdHistory ||
                                     command == chatbox::kCmdMessageSearch;
        const bool friend_command =
            (command >= chatbox::kCmdFriendRequest &&
             command <= chatbox::kCmdFriendRemove) ||
            command == chatbox::kCmdFriendSetRemark;
        const bool profile_command = command == chatbox::kCmdProfileGet ||
                                     command == chatbox::kCmdAvatarGet ||
                                     command == chatbox::kCmdProfileUpdate;
        if (!auth_command && !room_command && !private_command && !history_command &&
            !friend_command && !profile_command) {
            const Frame response = makeErrorResponse(frame.header.seq, 1001, "unknown command");
            return connection->enqueue(chatbox::encodeFrame(response));
        }

        if ((command == chatbox::kCmdChatSend ||
             command == chatbox::kCmdPrivateSend) &&
            !connection->allowMessage()) {
            chatbox::metrics().rate_limited.fetch_add(1, std::memory_order_relaxed);
            const Frame response =
                makeErrorResponse(frame.header.seq, 1005, "too many messages");
            return connection->enqueue(chatbox::encodeFrame(response));
        }

        chatbox::metrics().messages_in.fetch_add(1, std::memory_order_relaxed);

        const uint64_t connection_id = connection->id();
        const int io_thread_id = connection->ioThreadId();
        const uint64_t user_id = connection->userId();
        const std::string nickname = connection->nickname();
        const std::string token_hash = connection->tokenHash();

        const bool submitted = worker_pool.submit(
            connection_id,
            [connection, frame, command, auth_command, room_command, private_command,
             history_command, friend_command, profile_command, connection_id, io_thread_id,
             user_id, nickname, token_hash, &io_thread_refs, &room_manager, &sessions](
                chatbox::WorkerContext& context) {
                if (auth_command) {
                    chatbox::SessionBinding binding;
                    binding.connection_id = connection_id;
                    binding.io_thread_id = io_thread_id;
                    binding.peer_ip = connection->peerIp();
                    binding.connection = connection;

                    chatbox::AuthResult result =
                        context.auth.handle(frame, user_id, token_hash, binding);
                    if (result.bind_session && result.presence_seq != 0) {
                        chatbox::PresenceEntry entry;
                        entry.user_id = result.user_id;
                        entry.online = true;
                        entry.seq = result.presence_seq;
                        broadcastPresence(sessions, io_thread_refs, entry, connection_id);
                    }
                    if (result.clear_session && result.presence_seq != 0) {
                        chatbox::PresenceEntry entry;
                        entry.user_id = user_id;
                        entry.online = false;
                        entry.seq = result.presence_seq;
                        broadcastPresence(sessions, io_thread_refs, entry, connection_id);
                    }
                    if (result.has_old_binding &&
                        result.old_binding.connection_id != connection_id &&
                        result.old_binding.io_thread_id >= 0 &&
                        result.old_binding.io_thread_id <
                            static_cast<int>(io_thread_refs.size())) {
                        const bool resume_takeover = command == chatbox::kCmdResume;
                        const std::string reason =
                            resume_takeover ? "resume_takeover" : "kicked_by_new_login";
                        const std::string message =
                            resume_takeover ? "session taken over" : "kicked by new login";
                        io_thread_refs[result.old_binding.io_thread_id]->postErrorAndClose(
                            result.old_binding.connection_id, 1010, message, reason);
                    }
                    if (result.clear_session) {
                        const std::vector<int> changed_rooms =
                            room_manager.removeConnection(connection_id);
                        broadcastChangedRoomMembers(room_manager, io_thread_refs,
                                                    changed_rooms);
                    }

                    if (io_thread_id < 0 ||
                        io_thread_id >= static_cast<int>(io_thread_refs.size())) {
                        return;
                    }
                    chatbox::IoThread* owner = io_thread_refs[io_thread_id];
                    owner->post([connection, result, owner] {
                        if (connection->closed()) {
                            return;
                        }
                        if (result.clear_session) {
                            connection->clearAuth();
                        }
                        if (result.bind_session) {
                            connection->setAuth(result.user_id, result.nickname,
                                                result.token_hash);
                        }
                        owner->sendFrame(connection, chatbox::encodeFrame(result.response));
                    });
                    return;
                }

                chatbox::RoomMember caller;
                caller.user_id = user_id;
                caller.connection_id = connection_id;
                caller.io_thread_id = io_thread_id;
                caller.nickname = nickname;
                caller.connection = connection;

                if (friend_command) {
                    chatbox::FriendResult result =
                        context.friend_chat.handle(frame, caller);
                    if (result.push && result.target_io_thread_id >= 0 &&
                        result.target_io_thread_id <
                            static_cast<int>(io_thread_refs.size())) {
                        io_thread_refs[result.target_io_thread_id]->postMulticast(
                            std::vector<uint64_t>{result.target_connection_id},
                            result.push_frame);
                    }
                    if (io_thread_id < 0 ||
                        io_thread_id >= static_cast<int>(io_thread_refs.size())) {
                        return;
                    }
                    chatbox::IoThread* owner = io_thread_refs[io_thread_id];
                    owner->post([connection, response = std::move(result.response),
                                 owner] {
                        if (connection->closed()) {
                            return;
                        }
                        owner->sendFrame(connection, chatbox::encodeFrame(response));
                    });
                    return;
                }

                if (profile_command) {
                    chatbox::ProfileResult result =
                        context.profile.handle(frame, caller);
                    if (io_thread_id < 0 ||
                        io_thread_id >= static_cast<int>(io_thread_refs.size())) {
                        return;
                    }
                    chatbox::IoThread* owner = io_thread_refs[io_thread_id];
                    if (result.nickname_changed) {
                        room_manager.updateNickname(user_id, result.new_nickname);
                        owner->post([connection, nickname = result.new_nickname] {
                            if (connection->closed()) {
                                return;
                            }
                            connection->setNickname(nickname);
                        });
                    }
                    if (!result.push_frame.empty() && !result.push_targets.empty()) {
                        std::unordered_map<int, std::vector<uint64_t>> groups;
                        for (const chatbox::ProfilePushTarget& target :
                             result.push_targets) {
                            if (target.io_thread_id >= 0 &&
                                target.io_thread_id <
                                    static_cast<int>(io_thread_refs.size())) {
                                groups[target.io_thread_id].push_back(target.connection_id);
                            }
                        }
                        for (auto& entry : groups) {
                            io_thread_refs[entry.first]->postMulticast(
                                entry.second, result.push_frame);
                        }
                    }
                    owner->post([connection, response = std::move(result.response),
                                 owner] {
                        if (connection->closed()) {
                            return;
                        }
                        owner->sendFrame(connection, chatbox::encodeFrame(response));
                    });
                    return;
                }

                if (room_command) {
                    chatbox::RoomResult result = context.room.handle(frame, caller);
                    if (result.broadcast) {
                        std::unordered_map<int, std::vector<uint64_t>> groups;
                        for (const chatbox::RoomMember& target : result.targets) {
                            if (target.io_thread_id >= 0 &&
                                target.io_thread_id <
                                    static_cast<int>(io_thread_refs.size())) {
                                groups[target.io_thread_id].push_back(target.connection_id);
                            }
                        }
                        for (auto& entry : groups) {
                            io_thread_refs[entry.first]->postMulticast(
                                entry.second, result.broadcast_frame);
                        }
                    }

                    if (io_thread_id < 0 ||
                        io_thread_id >= static_cast<int>(io_thread_refs.size())) {
                        return;
                    }
                    chatbox::IoThread* owner = io_thread_refs[io_thread_id];
                    owner->post([connection, response = std::move(result.response), owner] {
                        if (connection->closed()) {
                            return;
                        }
                        owner->sendFrame(connection, chatbox::encodeFrame(response));
                    });
                    return;
                }

                if (private_command) {
                    chatbox::PrivateResult result =
                        context.private_chat.handle(frame, caller);
                    if (result.push && result.target_io_thread_id >= 0 &&
                        result.target_io_thread_id <
                            static_cast<int>(io_thread_refs.size())) {
                        io_thread_refs[result.target_io_thread_id]->postMulticast(
                            std::vector<uint64_t>{result.target_connection_id},
                            result.push_frame);
                    }

                    if (io_thread_id < 0 ||
                        io_thread_id >= static_cast<int>(io_thread_refs.size())) {
                        return;
                    }
                    chatbox::IoThread* owner = io_thread_refs[io_thread_id];
                    owner->post([connection, response = std::move(result.response), owner] {
                        if (connection->closed()) {
                            return;
                        }
                        owner->sendFrame(connection, chatbox::encodeFrame(response));
                    });
                    return;
                }

                Frame history_response = context.history.handle(frame, caller);
                if (io_thread_id < 0 ||
                    io_thread_id >= static_cast<int>(io_thread_refs.size())) {
                    return;
                }
                chatbox::IoThread* owner = io_thread_refs[io_thread_id];
                owner->post([connection, response = std::move(history_response), owner] {
                    if (connection->closed()) {
                        return;
                    }
                    owner->sendFrame(connection, chatbox::encodeFrame(response));
                });
            });
        if (!submitted) {
            const Frame response = makeErrorResponse(frame.header.seq, 2001, "server busy");
            return connection->enqueue(chatbox::encodeFrame(response));
        }
        return true;
    };

    for (int i = 0; i < config.io_threads; ++i) {
        auto io_thread = std::make_unique<chatbox::IoThread>(
            i, frame_handler, config.idle_timeout_sec,
            [&room_manager, &sessions, &connection_limiter, &io_thread_refs](
                uint64_t connection_id) {
                const std::vector<int> changed_rooms =
                    room_manager.removeConnection(connection_id);
                broadcastChangedRoomMembers(room_manager, io_thread_refs,
                                            changed_rooms);
                uint64_t presence_seq = 0;
                const uint64_t user_id =
                    sessions.unbindByConnection(connection_id, &presence_seq);
                connection_limiter.release(connection_id);
                if (user_id != 0 && presence_seq != 0) {
                    chatbox::PresenceEntry entry;
                    entry.user_id = user_id;
                    entry.online = false;
                    entry.seq = presence_seq;
                    broadcastPresence(sessions, io_thread_refs, entry, connection_id);
                }
            },
            config.proxy_protocol,
            [&connection_limiter](uint64_t connection_id, const std::string& ip,
                                  std::string& rekey_error) {
                return connection_limiter.rekey(connection_id, ip, rekey_error);
            });
        if (!io_thread->start(error)) {
            chatbox::logMessage(LogLevel::kError, "main",
                                 "failed to start IO thread " + std::to_string(i) + ": " + error);
            worker_pool.stop();
            for (auto& started : io_threads) {
                started->stop();
            }
            mysql_library_end();
            return 1;
        }
        io_thread_refs.push_back(io_thread.get());
        io_threads.push_back(std::move(io_thread));
    }

    chatbox::Acceptor acceptor(config.port, io_thread_refs, connection_limiter,
                                config.listen_address, config.proxy_protocol);
    if (!acceptor.start(error)) {
        chatbox::logMessage(LogLevel::kError, "main", "failed to start acceptor: " + error);
        worker_pool.stop();
        for (auto& io_thread : io_threads) {
            io_thread->stop();
        }
        mysql_library_end();
        return 1;
    }

    chatbox::startStatsReporter();
    chatbox::logMessage(LogLevel::kInfo, "main",
                         "server ready on port " + std::to_string(acceptor.port()));

    int signal_number = 0;
    while (true) {
        sigwait(&signal_set, &signal_number);
        if (signal_number == SIGUSR1) {
            chatbox::logMessage(LogLevel::kInfo, "main", "received SIGUSR1, reopening log");
            chatbox::reopenLogging();
            continue;
        }
        break;
    }
    chatbox::logMessage(LogLevel::kInfo, "main",
                         "received signal " + std::to_string(signal_number) + ", shutting down");

    chatbox::stopStatsReporter();
    acceptor.stop();
    worker_pool.stop();
    for (auto& io_thread : io_threads) {
        io_thread->stop();
    }

    mysql_library_end();
    chatbox::logMessage(LogLevel::kInfo, "main", "server stopped");
    return 0;
}
