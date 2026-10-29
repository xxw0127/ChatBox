#include "server/auth/auth_service.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>

#include "common/session_protocol.h"
#include "common/text_utils.h"
#include "server/auth/password_hash.h"
#include "server/auth/token.h"
#include "server/util/logging.h"
#include "server/util/metrics.h"

namespace chatbox {

namespace {

constexpr int kTokenTtlDays = 30;
constexpr uint32_t kMaxNewSessions = 5;
constexpr uint32_t kServerCaps =
    kCapMultiDevice | kCapSyncPull | kCapReadStatePush | kCapMessageEcho |
    kCapSessionManage | kCapDeviceMetadata;

Frame makeError(uint32_t seq, uint16_t code, const std::string& message) {
    Frame response;
    response.header.flags = kFlagResponse | kFlagError;
    response.header.cmd = kCmdError;
    response.header.seq = seq;
    ByteWriter writer;
    writer.putU16(code);
    writer.putString(message);
    response.body = writer.data();
    return response;
}

Frame makeSuccess(uint16_t command, uint32_t seq) {
    Frame response;
    response.header.flags = kFlagResponse;
    response.header.cmd = command;
    response.header.seq = seq;
    return response;
}

bool readString(ByteReader& reader, std::string& value, std::size_t max_size) {
    if (!reader.getString(value)) {
        return false;
    }
    return value.size() <= max_size;
}

bool validUsername(const std::string& username) {
    if (username.size() < 3 || username.size() > 32) {
        return false;
    }
    for (const unsigned char ch : username) {
        if (!std::isalnum(ch) && ch != '_') {
            return false;
        }
    }
    return true;
}

bool validNickname(const std::string& nickname) {
    if (nickname.empty() || utf8CodePointCount(nickname) > 32) {
        return false;
    }
    return !containsControlCharacters(nickname);
}

std::string utcNow() {
    const std::time_t now = std::time(nullptr);
    std::tm utc_time{};
    gmtime_r(&now, &utc_time);
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &utc_time);
    return buffer;
}

std::string utcPlusDays(int days) {
    const std::time_t target = std::time(nullptr) + static_cast<std::time_t>(days) * 86400;
    std::tm utc_time{};
    gmtime_r(&target, &utc_time);
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &utc_time);
    return buffer;
}

void appendRemoved(const SessionChange& change, uint64_t keep_connection_id,
                   AuthResult& result) {
    for (const SessionBinding& binding : change.removed) {
        if (binding.connection_id != keep_connection_id) {
            result.close_bindings.push_back(binding);
        }
    }
    if (change.went_offline) {
        result.went_offline = true;
    }
}

void closeDeviceBindings(SessionManager& sessions, uint64_t user_id,
                         const std::string& device_id,
                         uint64_t keep_connection_id, AuthResult& result) {
    std::vector<SessionBinding> bindings;
    if (!sessions.findAll(user_id, bindings)) {
        return;
    }
    for (const SessionBinding& binding : bindings) {
        if (binding.connection_id == keep_connection_id) {
            continue;
        }
        if (!device_id.empty() ? binding.device_id != device_id
                                : !binding.device_id.empty()) {
            continue;
        }
        const SessionChange change = sessions.removeConnection(binding.connection_id);
        appendRemoved(change, keep_connection_id, result);
        if (change.presence_seq != 0) {
            result.presence_seq = change.presence_seq;
        }
    }
}

uint16_t revokedCode(int revoked) {
    switch (revoked) {
        case 1:
            return 1010;  // kicked by new login
        case 2:
        case 4:
            return 1002;
        case 3:
            return 1010;
        case 5:
            return 1011;
        default:
            return 1009;
    }
}

std::string revokedMessage(int revoked) {
    switch (revoked) {
        case 1:
            return "kicked by new login";
        case 2:
        case 4:
            return "session revoked";
        case 3:
            return "session replaced";
        case 5:
            return "too many devices";
        default:
            return "session expired";
    }
}

}  // namespace

AuthService::AuthService(MysqlConnection& database, SessionManager& sessions,
                         UserLockTable& user_locks, LoginRateLimiter& login_limiter,
                         RegistrationRateLimiter& registration_limiter)
    : database_(database),
      sessions_(sessions),
      user_locks_(user_locks),
      login_limiter_(login_limiter),
      registration_limiter_(registration_limiter) {}

AuthResult AuthService::handle(const Frame& request, uint64_t current_user_id,
                               const std::string& current_token_hash,
                               const SessionBinding& binding) {
    switch (request.header.cmd) {
        case kCmdRegister:
            return handleRegister(request, binding);
        case kCmdLogin:
            return handleLogin(request, binding);
        case kCmdLogout:
            return handleLogout(request, current_user_id, current_token_hash, binding);
        case kCmdResume:
            return handleResume(request, binding);
        case kCmdSessionList:
            return handleSessionList(request, current_user_id, binding);
        case kCmdSessionRevoke:
            return handleSessionRevoke(request, current_user_id, binding);
        default:
            return AuthResult{makeError(request.header.seq, 1001, "unknown command")};
    }
}

AuthResult AuthService::handleRegister(const Frame& request,
                                       const SessionBinding& binding) {
    ByteReader reader(request.body.data(), request.body.size());
    std::string username;
    std::string password;
    std::string nickname;
    if (!readString(reader, username, 32) || !readString(reader, password, 128) ||
        !readString(reader, nickname, 128)) {
        return AuthResult{makeError(request.header.seq, 1001, "invalid register body")};
    }
    if (!validUsername(username)) {
        logMessage(LogLevel::kWarn, "auth",
                   "register invalid username len=" + std::to_string(username.size()) +
                       " value=" + username);
        return AuthResult{makeError(request.header.seq, 1001, "invalid username")};
    }
    if (password.size() < 8 || password.size() > 128) {
        return AuthResult{makeError(request.header.seq, 1001, "invalid password length")};
    }
    if (!validNickname(nickname)) {
        return AuthResult{makeError(request.header.seq, 1001, "invalid nickname")};
    }
    if (!registration_limiter_.allow(binding.peer_ip, std::chrono::steady_clock::now())) {
        metrics().rate_limited.fetch_add(1, std::memory_order_relaxed);
        return AuthResult{makeError(request.header.seq, 1005,
                                    "too many registrations")};
    }

    std::string password_hash;
    std::string error;
    if (!hashPassword(password, password_hash, error)) {
        logMessage(LogLevel::kError, "auth", "password hash failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }

    uint64_t user_id = 0;
    bool duplicate = false;
    if (!database_.createUserWithDefaultGroup(username, nickname, password_hash,
                                              user_id, duplicate, error)) {
        if (duplicate) {
            return AuthResult{makeError(request.header.seq, 1003, "username already exists")};
        }
        logMessage(LogLevel::kError, "auth", "create user failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }

    AuthResult result;
    result.response = makeSuccess(kCmdRegister, request.header.seq);
    ByteWriter writer;
    writer.putU64(user_id);
    result.response.body = writer.data();
    return result;
}

AuthResult AuthService::handleLogin(const Frame& request,
                                    const SessionBinding& binding) {
    ByteReader reader(request.body.data(), request.body.size());
    std::string username;
    std::string password;
    DeviceInfo device;
    bool has_device = false;
    if (!readString(reader, username, 32) || !readString(reader, password, 128) ||
        !readDeviceInfo(reader, device, has_device)) {
        return AuthResult{makeError(request.header.seq, 1001, "invalid login body")};
    }
    if (login_limiter_.isBlocked(binding.peer_ip, username)) {
        metrics().auth_failure.fetch_add(1, std::memory_order_relaxed);
        metrics().rate_limited.fetch_add(1, std::memory_order_relaxed);
        return AuthResult{makeError(request.header.seq, 1005, "too many login attempts")};
    }

    UserRecord user;
    bool found = false;
    std::string error;
    if (!database_.findUserByUsername(username, user, found, error)) {
        logMessage(LogLevel::kError, "auth", "find user failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!found || user.status != 0 || !verifyPassword(password, user.password_hash)) {
        metrics().auth_failure.fetch_add(1, std::memory_order_relaxed);
        login_limiter_.recordFailure(binding.peer_ip, username);
        return AuthResult{makeError(request.header.seq, 1004, "invalid username or password")};
    }
    login_limiter_.recordSuccess(binding.peer_ip, username);
    logMessage(LogLevel::kInfo, "auth",
               "login ok user=" + username + " ip=" + binding.peer_ip);

    const std::string token = generateToken();
    const std::string token_hash = hashToken(token);
    const std::string session_id = generateSessionId();
    if (token.empty() || token_hash.empty() || session_id.empty()) {
        logMessage(LogLevel::kError, "auth", "token generation failed");
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }

    std::lock_guard<std::mutex> user_lock(user_locks_.forUser(user.id));
    const std::string expires_at = utcPlusDays(kTokenTtlDays);
    const std::string now = utcNow();

    AuthResult result;
    if (has_device) {
        if (!database_.revokeSessionsForDevice(user.id, device.device_id, 3, error)) {
            logMessage(LogLevel::kError, "auth", "revoke same device failed: " + error);
            return AuthResult{makeError(request.header.seq, 2001, "internal error")};
        }

        uint32_t active_count = 0;
        if (!database_.countActiveNewSessions(user.id, active_count, error)) {
            logMessage(LogLevel::kError, "auth", "count sessions failed: " + error);
            return AuthResult{makeError(request.header.seq, 2001, "internal error")};
        }
        while (active_count >= kMaxNewSessions) {
            std::string revoked_session_id;
            if (!database_.revokeOldestNewSession(user.id, 5, revoked_session_id, error)) {
                logMessage(LogLevel::kError, "auth", "evict session failed: " + error);
                return AuthResult{makeError(request.header.seq, 2001, "internal error")};
            }
            if (revoked_session_id.empty()) {
                break;
            }
            result.close_code = 1011;
            result.close_message = "too many devices";
            SessionBinding old_binding;
            if (sessions_.findBySession(revoked_session_id, old_binding)) {
                const SessionChange change = sessions_.removeConnection(old_binding.connection_id);
                appendRemoved(change, binding.connection_id, result);
                if (change.presence_seq != 0) {
                    result.presence_seq = change.presence_seq;
                }
            }
            if (active_count == 0) {
                break;
            }
            --active_count;
        }

        SessionRecord session;
        session.user_id = user.id;
        session.session_id = session_id;
        session.device_id = device.device_id;
        session.device_name = device.device_name;
        session.platform = device.platform;
        session.app_version = device.app_version;
        session.caps = device.caps;
        session.created_at = now;
        session.last_seen_at = now;
        session.expires_at = expires_at;
        if (!database_.insertSession(session, token_hash, error)) {
            logMessage(LogLevel::kError, "auth", "insert session failed: " + error);
            return AuthResult{makeError(request.header.seq, 2001, "internal error")};
        }
        result.session_id = session_id;
        result.device_id = device.device_id;
        result.caps = device.caps;
    } else {
        // Legacy clients keep single-session semantics among themselves.
        if (!database_.revokeLegacySessions(user.id, 1, error)) {
            logMessage(LogLevel::kError, "auth", "revoke legacy sessions failed: " + error);
            return AuthResult{makeError(request.header.seq, 2001, "internal error")};
        }
        if (!database_.insertSession(token_hash, user.id, expires_at, error)) {
            logMessage(LogLevel::kError, "auth", "insert legacy session failed: " + error);
            return AuthResult{makeError(request.header.seq, 2001, "internal error")};
        }
        result.close_code = 1010;
        result.close_message = "kicked by new login";
    }

    SessionBinding session_binding = binding;
    session_binding.session_id = session_id;
    session_binding.device_id = has_device ? device.device_id : std::string();
    session_binding.caps = has_device ? device.caps : 0;
    const SessionChange change = sessions_.add(user.id, session_binding);
    appendRemoved(change, binding.connection_id, result);
    result.became_online = change.became_online;
    if (change.presence_seq != 0) {
        result.presence_seq = change.presence_seq;
    }
    if (has_device) {
        closeDeviceBindings(sessions_, user.id, device.device_id,
                            binding.connection_id, result);
    } else {
        closeDeviceBindings(sessions_, user.id, std::string(),
                            binding.connection_id, result);
    }

    result.response = makeSuccess(kCmdLogin, request.header.seq);
    ByteWriter writer;
    writer.putU64(user.id);
    writer.putString(user.nickname);
    writer.putString(token);
    appendAuthExtras(writer, kServerCaps, kTokenTtlDays * 24 * 3600);
    result.response.body = writer.data();
    result.bind_session = true;
    result.user_id = user.id;
    result.nickname = user.nickname;
    result.token_hash = token_hash;
    metrics().auth_success.fetch_add(1, std::memory_order_relaxed);
    return result;
}

AuthResult AuthService::handleLogout(const Frame& request, uint64_t current_user_id,
                                     const std::string& current_token_hash,
                                     const SessionBinding& binding) {
    if (current_user_id == 0 || current_token_hash.empty()) {
        return AuthResult{makeError(request.header.seq, 1002, "not authenticated")};
    }

    std::lock_guard<std::mutex> user_lock(user_locks_.forUser(current_user_id));
    std::string error;
    if (!database_.revokeSession(current_token_hash, 2, error)) {
        logMessage(LogLevel::kError, "auth", "revoke session failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }
    AuthResult result;
    result.response = makeSuccess(kCmdLogout, request.header.seq);
    result.clear_session = true;
    const SessionChange change = sessions_.removeConnection(binding.connection_id);
    appendRemoved(change, binding.connection_id, result);
    result.went_offline = change.went_offline;
    if (change.presence_seq != 0) {
        result.presence_seq = change.presence_seq;
    }
    return result;
}

AuthResult AuthService::handleResume(const Frame& request,
                                     const SessionBinding& binding) {
    ByteReader reader(request.body.data(), request.body.size());
    std::string token;
    DeviceInfo device;
    bool has_device = false;
    if (!readString(reader, token, 64) ||
        !readDeviceInfo(reader, device, has_device)) {
        return AuthResult{makeError(request.header.seq, 1001, "invalid resume body")};
    }
    const std::string token_hash = hashToken(token);
    if (token_hash.empty()) {
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }

    SessionRecord session;
    bool found = false;
    std::string error;
    if (!database_.findSession(token_hash, session, found, error)) {
        logMessage(LogLevel::kError, "auth", "find session failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!found) {
        return AuthResult{makeError(request.header.seq, 1009, "session expired")};
    }
    if (session.revoked != 0) {
        return AuthResult{makeError(request.header.seq, revokedCode(session.revoked),
                                    revokedMessage(session.revoked))};
    }
    if (!session.valid) {
        return AuthResult{makeError(request.header.seq, 1009, "session expired")};
    }

    UserRecord user;
    bool user_found = false;
    if (!database_.findUserById(session.user_id, user, user_found, error)) {
        logMessage(LogLevel::kError, "auth", "find user by id failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!user_found || user.status != 0) {
        return AuthResult{makeError(request.header.seq, 1002, "account unavailable")};
    }

    std::lock_guard<std::mutex> user_lock(user_locks_.forUser(user.id));
    SessionRecord current_session;
    bool current_found = false;
    if (!database_.findSession(token_hash, current_session, current_found, error)) {
        logMessage(LogLevel::kError, "auth", "recheck session failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!current_found) {
        return AuthResult{makeError(request.header.seq, 1009, "session expired")};
    }
    if (current_session.revoked != 0) {
        return AuthResult{makeError(request.header.seq,
                                    revokedCode(current_session.revoked),
                                    revokedMessage(current_session.revoked))};
    }
    if (!current_session.valid) {
        return AuthResult{makeError(request.header.seq, 1009, "session expired")};
    }

    const std::string expires_at = utcPlusDays(kTokenTtlDays);
    if (has_device) {
        if (current_session.device_id.empty() || !device.device_id.empty()) {
            if (!database_.upgradeSessionDevice(token_hash, device, expires_at, error)) {
                logMessage(LogLevel::kError, "auth", "upgrade session failed: " + error);
                return AuthResult{makeError(request.header.seq, 2001, "internal error")};
            }
        }
        current_session.device_id = device.device_id;
        current_session.device_name = device.device_name;
        current_session.platform = device.platform;
        current_session.app_version = device.app_version;
        current_session.caps = device.caps;
    } else if (!database_.touchSession(token_hash, expires_at, error)) {
        logMessage(LogLevel::kError, "auth", "touch session failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }

    SessionBinding session_binding = binding;
    session_binding.session_id = current_session.session_id;
    session_binding.device_id = current_session.device_id;
    session_binding.caps = current_session.caps;
    const SessionChange change = sessions_.add(user.id, session_binding);
    AuthResult result;
    appendRemoved(change, binding.connection_id, result);
    if (!result.close_bindings.empty()) {
        result.close_code = 1010;
        result.close_message = "session taken over";
    }
    result.became_online = change.became_online;
    if (change.presence_seq != 0) {
        result.presence_seq = change.presence_seq;
    }
    result.response = makeSuccess(kCmdResume, request.header.seq);
    ByteWriter writer;
    writer.putU64(user.id);
    writer.putString(user.nickname);
    appendAuthExtras(writer, kServerCaps, kTokenTtlDays * 24 * 3600);
    result.response.body = writer.data();
    result.bind_session = true;
    result.user_id = user.id;
    result.nickname = user.nickname;
    result.token_hash = token_hash;
    result.session_id = current_session.session_id;
    result.device_id = current_session.device_id;
    result.caps = current_session.caps;
    metrics().auth_success.fetch_add(1, std::memory_order_relaxed);
    return result;
}

AuthResult AuthService::handleSessionList(const Frame& request,
                                          uint64_t current_user_id,
                                          const SessionBinding& binding) {
    if (current_user_id == 0) {
        return AuthResult{makeError(request.header.seq, 1002, "not authenticated")};
    }
    std::vector<SessionRecord> records;
    std::string error;
    if (!database_.listSessions(current_user_id, records, error)) {
        logMessage(LogLevel::kError, "auth", "list sessions failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }
    std::vector<SessionInfo> sessions;
    sessions.reserve(records.size());
    for (const SessionRecord& record : records) {
        SessionInfo info;
        info.session_id = record.session_id;
        info.device_id = record.device_id;
        info.device_name = record.device_name;
        info.platform = record.platform;
        info.app_version = record.app_version;
        info.created_at = record.created_at;
        info.last_seen_at = record.last_seen_at;
        info.current = !binding.session_id.empty() &&
                       record.session_id == binding.session_id;
        sessions.push_back(std::move(info));
    }

    AuthResult result;
    result.response = makeSuccess(kCmdSessionList, request.header.seq);
    ByteWriter writer;
    appendSessionList(writer, sessions);
    result.response.body = writer.data();
    return result;
}

AuthResult AuthService::handleSessionRevoke(const Frame& request,
                                            uint64_t current_user_id,
                                            const SessionBinding& binding) {
    if (current_user_id == 0) {
        return AuthResult{makeError(request.header.seq, 1002, "not authenticated")};
    }
    ByteReader reader(request.body.data(), request.body.size());
    std::string session_id;
    if (!readString(reader, session_id, 36) || session_id.empty()) {
        return AuthResult{makeError(request.header.seq, 1001, "invalid session id")};
    }

    std::lock_guard<std::mutex> user_lock(user_locks_.forUser(current_user_id));
    std::string error;
    if (!database_.revokeSessionById(current_user_id, session_id, 4, error)) {
        if (error == "session not found") {
            return AuthResult{makeError(request.header.seq, 1001, "session not found")};
        }
        logMessage(LogLevel::kError, "auth", "revoke session by id failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }

    AuthResult result;
    result.response = makeSuccess(kCmdSessionRevoke, request.header.seq);
    result.close_code = 1002;
    result.close_message = "session revoked";
    SessionBinding target;
    if (sessions_.findBySession(session_id, target)) {
        const SessionChange change = sessions_.removeConnection(target.connection_id);
        appendRemoved(change, binding.connection_id, result);
        if (change.presence_seq != 0) {
            result.presence_seq = change.presence_seq;
        }
    }
    if (session_id == binding.session_id) {
        result.clear_session = true;
    }
    return result;
}

}  // namespace chatbox
