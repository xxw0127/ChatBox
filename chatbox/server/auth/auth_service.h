#ifndef CHATBOX_SERVER_AUTH_AUTH_SERVICE_H
#define CHATBOX_SERVER_AUTH_AUTH_SERVICE_H

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "common/protocol.h"
#include "server/auth/login_rate_limiter.h"
#include "server/auth/registration_rate_limiter.h"
#include "server/auth/session_manager.h"
#include "server/auth/user_lock_table.h"
#include "server/db/mysql_connection.h"

namespace chatbox {

struct AuthResult {
    Frame response;
    bool bind_session = false;
    bool clear_session = false;
    uint64_t user_id = 0;
    std::string nickname;
    std::string token_hash;
    std::string session_id;
    std::string device_id;
    uint32_t caps = 0;
    // Bindings displaced by login/resume/revocation. main closes them.
    std::vector<SessionBinding> close_bindings;
    uint16_t close_code = 1010;
    std::string close_message = "session replaced";
    bool became_online = false;
    bool went_offline = false;
    // Non-zero when this auth command changed the user's online state; the
    // caller broadcasts it with this sequence number.
    uint64_t presence_seq = 0;

    AuthResult() = default;
    explicit AuthResult(Frame frame) : response(std::move(frame)) {}
};

class AuthService {
public:
    AuthService(MysqlConnection& database, SessionManager& sessions,
                UserLockTable& user_locks, LoginRateLimiter& login_limiter,
                RegistrationRateLimiter& registration_limiter);

    AuthResult handle(const Frame& request, uint64_t current_user_id,
                      const std::string& current_token_hash,
                      const SessionBinding& binding);

private:
    AuthResult handleRegister(const Frame& request, const SessionBinding& binding);
    AuthResult handleLogin(const Frame& request, const SessionBinding& binding);
    AuthResult handleLogout(const Frame& request, uint64_t current_user_id,
                            const std::string& current_token_hash,
                            const SessionBinding& binding);
    AuthResult handleResume(const Frame& request, const SessionBinding& binding);
    AuthResult handleSessionList(const Frame& request, uint64_t current_user_id,
                                 const SessionBinding& binding);
    AuthResult handleSessionRevoke(const Frame& request, uint64_t current_user_id,
                                   const SessionBinding& binding);

    MysqlConnection& database_;
    SessionManager& sessions_;
    UserLockTable& user_locks_;
    LoginRateLimiter& login_limiter_;
    RegistrationRateLimiter& registration_limiter_;
};

}  // namespace chatbox

#endif  // CHATBOX_SERVER_AUTH_AUTH_SERVICE_H
