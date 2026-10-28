#include "server/auth/auth_service.h"

#include <cctype>
#include <chrono>
#include <ctime>
#include <string>

#include "common/text_utils.h"
#include "server/auth/password_hash.h"
#include "server/auth/token.h"
#include "server/util/logging.h"
#include "server/util/metrics.h"

namespace chatbox {

namespace {

constexpr int kTokenTtlDays = 7;

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

std::string utcPlusDays(int days) {
    const std::time_t target = std::time(nullptr) + static_cast<std::time_t>(days) * 86400;
    std::tm utc_time{};
    gmtime_r(&target, &utc_time);
    char buffer[32] = {0};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &utc_time);
    return buffer;
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

AuthResult AuthService::handleLogin(const Frame& request, const SessionBinding& binding) {
    ByteReader reader(request.body.data(), request.body.size());
    std::string username;
    std::string password;
    if (!readString(reader, username, 32) || !readString(reader, password, 128)) {
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
    if (token.empty() || token_hash.empty()) {
        logMessage(LogLevel::kError, "auth", "token generation failed");
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }

    std::lock_guard<std::mutex> user_lock(user_locks_.forUser(user.id));
    if (!database_.revokeSessionsForUser(user.id, 1, error)) {
        logMessage(LogLevel::kError, "auth", "revoke old sessions failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!database_.insertSession(token_hash, user.id, utcPlusDays(kTokenTtlDays), error)) {
        logMessage(LogLevel::kError, "auth", "insert session failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!database_.updateLastLogin(user.id, error)) {
        logMessage(LogLevel::kWarn, "auth", "update last login failed: " + error);
    }

    AuthResult result;
    result.response = makeSuccess(kCmdLogin, request.header.seq);
    ByteWriter writer;
    writer.putU64(user.id);
    writer.putString(user.nickname);
    writer.putString(token);
    result.response.body = writer.data();
    result.bind_session = true;
    result.user_id = user.id;
    result.nickname = user.nickname;
    result.token_hash = token_hash;
    SessionBinding old_binding;
    if (sessions_.bind(user.id, binding, old_binding)) {
        result.has_old_binding = true;
        result.old_binding = old_binding;
    }
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
    sessions_.unbind(current_user_id, binding.connection_id);

    AuthResult result;
    result.response = makeSuccess(kCmdLogout, request.header.seq);
    result.clear_session = true;
    return result;
}

AuthResult AuthService::handleResume(const Frame& request, const SessionBinding& binding) {
    ByteReader reader(request.body.data(), request.body.size());
    std::string token;
    if (!readString(reader, token, 64)) {
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
    if (session.revoked == 1) {
        return AuthResult{makeError(request.header.seq, 1010, "kicked by new login")};
    }
    if (session.revoked == 2) {
        return AuthResult{makeError(request.header.seq, 1002, "session revoked")};
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

    // Re-check under the user lock: a concurrent login or logout may have
    // revoked the session between the first lookup and this point.
    SessionRecord current_session;
    bool current_found = false;
    if (!database_.findSession(token_hash, current_session, current_found, error)) {
        logMessage(LogLevel::kError, "auth", "recheck session failed: " + error);
        return AuthResult{makeError(request.header.seq, 2001, "internal error")};
    }
    if (!current_found || !current_session.valid) {
        return AuthResult{makeError(request.header.seq, 1009, "session expired")};
    }
    if (current_session.revoked == 1) {
        return AuthResult{makeError(request.header.seq, 1010, "kicked by new login")};
    }
    if (current_session.revoked == 2) {
        return AuthResult{makeError(request.header.seq, 1002, "session revoked")};
    }

    AuthResult result;
    result.response = makeSuccess(kCmdResume, request.header.seq);
    ByteWriter writer;
    writer.putU64(user.id);
    writer.putString(user.nickname);
    result.response.body = writer.data();
    result.bind_session = true;
    result.user_id = user.id;
    result.nickname = user.nickname;
    result.token_hash = token_hash;
    SessionBinding old_binding;
    if (sessions_.bind(user.id, binding, old_binding)) {
        result.has_old_binding = true;
        result.old_binding = old_binding;
    }
    metrics().auth_success.fetch_add(1, std::memory_order_relaxed);
    return result;
}

}  // namespace chatbox
