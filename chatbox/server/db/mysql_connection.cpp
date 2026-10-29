#include "server/db/mysql_connection.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include "server/util/logging.h"
#include "server/util/metrics.h"

namespace chatbox {

namespace {

class Stmt {
public:
    Stmt(MYSQL* mysql, const char* sql, std::string& error) {
        stmt_ = mysql_stmt_init(mysql);
        if (stmt_ == nullptr) {
            error = mysql_error(mysql);
            return;
        }
        if (mysql_stmt_prepare(stmt_, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
            error = mysql_stmt_error(stmt_);
            mysql_stmt_close(stmt_);
            stmt_ = nullptr;
        }
    }

    ~Stmt() {
        if (stmt_ != nullptr) {
            mysql_stmt_close(stmt_);
        }
    }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    MYSQL_STMT* get() const {
        return stmt_;
    }

private:
    MYSQL_STMT* stmt_ = nullptr;
};

void bindString(MYSQL_BIND& bind, const std::string& value, unsigned long& length) {
    std::memset(&bind, 0, sizeof(bind));
    length = static_cast<unsigned long>(value.size());
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = const_cast<char*>(value.data());
    bind.buffer_length = length;
    bind.length = &length;
}

void bindBlob(MYSQL_BIND& bind, const std::string& value, unsigned long& length) {
    std::memset(&bind, 0, sizeof(bind));
    length = static_cast<unsigned long>(value.size());
    bind.buffer_type = MYSQL_TYPE_BLOB;
    bind.buffer = const_cast<char*>(value.data());
    bind.buffer_length = length;
    bind.length = &length;
}

void bindU64(MYSQL_BIND& bind, uint64_t& value) {
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &value;
    bind.buffer_length = sizeof(value);
    bind.is_unsigned = true;
}

void bindInt(MYSQL_BIND& bind, int& value) {
    std::memset(&bind, 0, sizeof(bind));
    bind.buffer_type = MYSQL_TYPE_LONG;
    bind.buffer = &value;
    bind.buffer_length = sizeof(value);
}

bool execute(MYSQL_STMT* stmt, std::string& error) {
    if (mysql_stmt_execute(stmt) != 0) {
        error = mysql_stmt_error(stmt);
        if (mysql_stmt_errno(stmt) != 1062) {
            metrics().db_errors.fetch_add(1, std::memory_order_relaxed);
        }
        return false;
    }
    return true;
}

std::string likePattern(const std::string& keyword) {
    std::string escaped;
    escaped.reserve(keyword.size());
    for (const char ch : keyword) {
        if (ch == '!' || ch == '%' || ch == '_') {
            escaped.push_back('!');
        }
        escaped.push_back(ch);
    }
    return "%" + escaped + "%";
}

std::string stringResult(const char* buffer, unsigned long length, bool is_null,
                         std::size_t capacity) {
    if (is_null) {
        return {};
    }
    const std::size_t safe_length =
        length > capacity ? capacity : static_cast<std::size_t>(length);
    return std::string(buffer, safe_length);
}

bool readHistoryPage(MYSQL_STMT* stmt, int limit, HistoryPage& page,
                     std::string& error, bool ascending = false) {
    page = HistoryPage{};
    uint64_t id = 0;
    uint64_t sender_id = 0;
    char nickname_buffer[129] = {0};
    char content_buffer[2048] = {0};
    char created_buffer[32] = {0};
    uint64_t reply_to_id = 0;
    char reply_sender_buffer[129] = {0};
    char reply_content_buffer[2048] = {0};
    int recalled = 0;
    int msg_type = 0;
    uint64_t file_id = 0;
    unsigned long id_length = 0;
    unsigned long sender_length = 0;
    unsigned long nickname_length = 0;
    unsigned long content_length = 0;
    unsigned long created_length = 0;
    unsigned long reply_to_length = 0;
    unsigned long reply_sender_length = 0;
    unsigned long reply_content_length = 0;
    unsigned long recalled_length = 0;
    bool id_null = false;
    bool sender_null = false;
    bool nickname_null = false;
    bool content_null = false;
    bool created_null = false;
    bool reply_to_null = false;
    bool reply_sender_null = false;
    bool reply_content_null = false;
    bool recalled_null = false;

    MYSQL_BIND results[11] = {};
    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;
    results[0].buffer_length = sizeof(id);
    results[0].is_unsigned = true;
    results[0].length = &id_length;
    results[0].is_null = &id_null;
    results[1].buffer_type = MYSQL_TYPE_LONGLONG;
    results[1].buffer = &sender_id;
    results[1].buffer_length = sizeof(sender_id);
    results[1].is_unsigned = true;
    results[1].length = &sender_length;
    results[1].is_null = &sender_null;
    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = nickname_buffer;
    results[2].buffer_length = sizeof(nickname_buffer) - 1;
    results[2].length = &nickname_length;
    results[2].is_null = &nickname_null;
    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = content_buffer;
    results[3].buffer_length = sizeof(content_buffer) - 1;
    results[3].length = &content_length;
    results[3].is_null = &content_null;
    results[4].buffer_type = MYSQL_TYPE_STRING;
    results[4].buffer = created_buffer;
    results[4].buffer_length = sizeof(created_buffer) - 1;
    results[4].length = &created_length;
    results[4].is_null = &created_null;
    results[5].buffer_type = MYSQL_TYPE_LONGLONG;
    results[5].buffer = &reply_to_id;
    results[5].buffer_length = sizeof(reply_to_id);
    results[5].is_unsigned = true;
    results[5].length = &reply_to_length;
    results[5].is_null = &reply_to_null;
    results[6].buffer_type = MYSQL_TYPE_STRING;
    results[6].buffer = reply_sender_buffer;
    results[6].buffer_length = sizeof(reply_sender_buffer) - 1;
    results[6].length = &reply_sender_length;
    results[6].is_null = &reply_sender_null;
    results[7].buffer_type = MYSQL_TYPE_STRING;
    results[7].buffer = reply_content_buffer;
    results[7].buffer_length = sizeof(reply_content_buffer) - 1;
    results[7].length = &reply_content_length;
    results[7].is_null = &reply_content_null;
    results[8].buffer_type = MYSQL_TYPE_TINY;
    results[8].buffer = &recalled;
    results[8].buffer_length = sizeof(recalled);
    results[8].length = &recalled_length;
    results[8].is_null = &recalled_null;
    results[9].buffer_type = MYSQL_TYPE_TINY;
    results[9].buffer = &msg_type;
    results[9].buffer_length = sizeof(msg_type);
    results[10].buffer_type = MYSQL_TYPE_LONGLONG;
    results[10].buffer = &file_id;
    results[10].buffer_length = sizeof(file_id);
    results[10].is_unsigned = true;

    if (mysql_stmt_bind_result(stmt, results) != 0) {
        error = mysql_stmt_error(stmt);
        return false;
    }

    std::vector<HistoryMessage> rows;
    while (true) {
        const int fetch_result = mysql_stmt_fetch(stmt);
        if (fetch_result == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt);
            return false;
        }
        HistoryMessage message;
        message.id = id;
        message.sender_id = sender_id;
        message.nickname = stringResult(nickname_buffer, nickname_length, nickname_null,
                                        sizeof(nickname_buffer) - 1);
        message.content = stringResult(content_buffer, content_length, content_null,
                                       sizeof(content_buffer) - 1);
        message.created_at = stringResult(created_buffer, created_length, created_null,
                                          sizeof(created_buffer) - 1);
        message.reply_to_id = reply_to_null ? 0 : reply_to_id;
        message.reply_sender_name =
            stringResult(reply_sender_buffer, reply_sender_length, reply_sender_null,
                         sizeof(reply_sender_buffer) - 1);
        message.reply_content =
            stringResult(reply_content_buffer, reply_content_length, reply_content_null,
                         sizeof(reply_content_buffer) - 1);
        message.recalled = !recalled_null && recalled != 0;
        message.msg_type = static_cast<uint8_t>(msg_type);
        message.file_id = file_id;
        rows.push_back(std::move(message));
    }

    if (static_cast<int>(rows.size()) > limit) {
        page.has_more = true;
        rows.pop_back();
    }
    if (!ascending) {
        std::reverse(rows.begin(), rows.end());
    } else {
        page.next_before_id = 0;
    }
    page.messages = std::move(rows);
    page.next_before_id = page.messages.empty() ? 0 : page.messages.front().id;
    error.clear();
    return true;
}

bool readSearchRows(MYSQL_STMT* stmt, int limit, std::vector<SearchResultRow>& results,
                    std::string& error) {
    results.clear();
    (void)limit;
    uint64_t id = 0;
    uint64_t sender_id = 0;
    char nickname_buffer[129] = {0};
    char content_buffer[2048] = {0};
    char created_buffer[32] = {0};
    int recalled = 0;
    int msg_type = 0;
    uint64_t file_id = 0;
    unsigned long id_length = 0;
    unsigned long sender_length = 0;
    unsigned long nickname_length = 0;
    unsigned long content_length = 0;
    unsigned long created_length = 0;
    unsigned long recalled_length = 0;
    bool id_null = false;
    bool sender_null = false;
    bool nickname_null = false;
    bool content_null = false;
    bool created_null = false;
    bool recalled_null = false;

    MYSQL_BIND binds[8] = {};
    binds[0].buffer_type = MYSQL_TYPE_LONGLONG;
    binds[0].buffer = &id;
    binds[0].buffer_length = sizeof(id);
    binds[0].is_unsigned = true;
    binds[0].length = &id_length;
    binds[0].is_null = &id_null;
    binds[1].buffer_type = MYSQL_TYPE_LONGLONG;
    binds[1].buffer = &sender_id;
    binds[1].buffer_length = sizeof(sender_id);
    binds[1].is_unsigned = true;
    binds[1].length = &sender_length;
    binds[1].is_null = &sender_null;
    binds[2].buffer_type = MYSQL_TYPE_STRING;
    binds[2].buffer = nickname_buffer;
    binds[2].buffer_length = sizeof(nickname_buffer) - 1;
    binds[2].length = &nickname_length;
    binds[2].is_null = &nickname_null;
    binds[3].buffer_type = MYSQL_TYPE_STRING;
    binds[3].buffer = content_buffer;
    binds[3].buffer_length = sizeof(content_buffer) - 1;
    binds[3].length = &content_length;
    binds[3].is_null = &content_null;
    binds[4].buffer_type = MYSQL_TYPE_STRING;
    binds[4].buffer = created_buffer;
    binds[4].buffer_length = sizeof(created_buffer) - 1;
    binds[4].length = &created_length;
    binds[4].is_null = &created_null;
    binds[5].buffer_type = MYSQL_TYPE_TINY;
    binds[5].buffer = &recalled;
    binds[5].buffer_length = sizeof(recalled);
    binds[5].length = &recalled_length;
    binds[5].is_null = &recalled_null;
    binds[6].buffer_type = MYSQL_TYPE_TINY;
    binds[6].buffer = &msg_type;
    binds[6].buffer_length = sizeof(msg_type);
    binds[7].buffer_type = MYSQL_TYPE_LONGLONG;
    binds[7].buffer = &file_id;
    binds[7].buffer_length = sizeof(file_id);
    binds[7].is_unsigned = true;

    if (mysql_stmt_bind_result(stmt, binds) != 0) {
        error = mysql_stmt_error(stmt);
        return false;
    }
    while (true) {
        const int fetch_result = mysql_stmt_fetch(stmt);
        if (fetch_result == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt);
            return false;
        }
        SearchResultRow row;
        row.id = id_null ? 0 : id;
        row.sender_id = sender_null ? 0 : sender_id;
        row.nickname = stringResult(nickname_buffer, nickname_length, nickname_null,
                                    sizeof(nickname_buffer) - 1);
        row.content = stringResult(content_buffer, content_length, content_null,
                                   sizeof(content_buffer) - 1);
        row.created_at = stringResult(created_buffer, created_length, created_null,
                                      sizeof(created_buffer) - 1);
        row.recalled = !recalled_null && recalled != 0;
        row.msg_type = static_cast<uint8_t>(msg_type);
        row.file_id = file_id;
        results.push_back(std::move(row));
    }
    error.clear();
    return true;
}

}  // namespace

MysqlConnection::MysqlConnection() = default;
MysqlConnection::~MysqlConnection() {
    close();
}

bool MysqlConnection::connect(const DbConfig& config, std::string& error) {
    close();
    config_ = config;
    mysql_ = mysql_init(nullptr);
    if (mysql_ == nullptr) {
        error = "mysql_init failed";
        return false;
    }

    unsigned int timeout_seconds = 5;
    bool get_server_public_key = true;
    mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout_seconds);
    mysql_options(mysql_, MYSQL_OPT_READ_TIMEOUT, &timeout_seconds);
    mysql_options(mysql_, MYSQL_OPT_WRITE_TIMEOUT, &timeout_seconds);
    mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    mysql_options(mysql_, MYSQL_OPT_GET_SERVER_PUBLIC_KEY, &get_server_public_key);

    if (mysql_real_connect(mysql_, config.host.c_str(), config.user.c_str(),
                           config.password.c_str(), config.database.c_str(), config.port,
                           nullptr, 0) == nullptr) {
        error = mysql_error(mysql_);
        close();
        return false;
    }

    if (mysql_set_character_set(mysql_, "utf8mb4") != 0) {
        error = mysql_error(mysql_);
        close();
        return false;
    }
    if (mysql_query(mysql_, "SET time_zone = '+00:00'") != 0) {
        error = mysql_error(mysql_);
        close();
        return false;
    }
    error.clear();
    return true;
}

void MysqlConnection::close() {
    if (mysql_ != nullptr) {
        mysql_close(mysql_);
        mysql_ = nullptr;
    }
}

bool MysqlConnection::ensureConnected(std::string& error) {
    if (mysql_ != nullptr) {
        if (mysql_ping(mysql_) == 0) {
            return true;
        }
        metrics().db_errors.fetch_add(1, std::memory_order_relaxed);
        logMessage(LogLevel::kWarn, "db", "mysql connection lost, reconnecting");
        close();
    }
    if (config_.host.empty()) {
        error = "database is not connected";
        return false;
    }
    if (!connect(config_, error)) {
        metrics().db_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    logMessage(LogLevel::kInfo, "db", "mysql connection restored host=" + config_.host +
                                         " database=" + config_.database);
    return true;
}

bool MysqlConnection::createUser(const std::string& username, const std::string& nickname,
                                 const std::string& password_hash, uint64_t& user_id,
                                 bool& duplicate, std::string& error) {
    duplicate = false;
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_, "INSERT INTO users (username, nickname, password_hash) VALUES (?, ?, ?)",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    unsigned long username_length = 0;
    unsigned long nickname_length = 0;
    unsigned long hash_length = 0;
    MYSQL_BIND params[3] = {};
    bindString(params[0], username, username_length);
    bindString(params[1], nickname, nickname_length);
    bindString(params[2], password_hash, hash_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        duplicate = (mysql_stmt_errno(stmt.get()) == 1062);
        return false;
    }
    user_id = mysql_stmt_insert_id(stmt.get());
    error.clear();
    return true;
}

bool MysqlConnection::findUserByUsername(const std::string& username, UserRecord& user,
                                         bool& found, std::string& error) {
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "SELECT id, username, nickname, password_hash, status FROM users WHERE username = ? "
              "LIMIT 1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    unsigned long username_length = 0;
    MYSQL_BIND param = {};
    bindString(param, username, username_length);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    uint64_t id = 0;
    char username_buffer[33] = {0};
    char nickname_buffer[129] = {0};
    char hash_buffer[256] = {0};
    int status = 0;
    unsigned long id_length = 0;
    unsigned long nickname_length = 0;
    unsigned long hash_length = 0;
    unsigned long status_length = 0;
    bool id_null = false;
    bool username_null = false;
    bool nickname_null = false;
    bool hash_null = false;
    bool status_null = false;

    MYSQL_BIND results[5] = {};
    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;
    results[0].buffer_length = sizeof(id);
    results[0].is_unsigned = true;
    results[0].length = &id_length;
    results[0].is_null = &id_null;
    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = username_buffer;
    results[1].buffer_length = sizeof(username_buffer) - 1;
    results[1].length = &username_length;
    results[1].is_null = &username_null;
    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = nickname_buffer;
    results[2].buffer_length = sizeof(nickname_buffer) - 1;
    results[2].length = &nickname_length;
    results[2].is_null = &nickname_null;
    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = hash_buffer;
    results[3].buffer_length = sizeof(hash_buffer) - 1;
    results[3].length = &hash_length;
    results[3].is_null = &hash_null;
    results[4].buffer_type = MYSQL_TYPE_LONG;
    results[4].buffer = &status;
    results[4].buffer_length = sizeof(status);
    results[4].length = &status_length;
    results[4].is_null = &status_null;

    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(stmt.get());
    if (fetch_result == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    user.id = id;
    user.username = stringResult(username_buffer, username_length, username_null,
                                 sizeof(username_buffer) - 1);
    user.nickname = stringResult(nickname_buffer, nickname_length, nickname_null,
                                 sizeof(nickname_buffer) - 1);
    user.password_hash =
        stringResult(hash_buffer, hash_length, hash_null, sizeof(hash_buffer) - 1);
    user.status = status_null ? 0 : status;
    found = !id_null;
    error.clear();
    return true;
}

bool MysqlConnection::findUserById(uint64_t user_id, UserRecord& user, bool& found,
                                   std::string& error) {
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "SELECT id, username, nickname, password_hash, status FROM users WHERE id = ? LIMIT "
              "1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    MYSQL_BIND param = {};
    bindU64(param, user_id);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    uint64_t id = 0;
    char username_buffer[33] = {0};
    char nickname_buffer[129] = {0};
    char hash_buffer[256] = {0};
    int status = 0;
    unsigned long id_length = 0;
    unsigned long username_length = 0;
    unsigned long nickname_length = 0;
    unsigned long hash_length = 0;
    unsigned long status_length = 0;
    bool id_null = false;
    bool username_null = false;
    bool nickname_null = false;
    bool hash_null = false;
    bool status_null = false;

    MYSQL_BIND results[5] = {};
    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;
    results[0].buffer_length = sizeof(id);
    results[0].is_unsigned = true;
    results[0].length = &id_length;
    results[0].is_null = &id_null;
    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = username_buffer;
    results[1].buffer_length = sizeof(username_buffer) - 1;
    results[1].length = &username_length;
    results[1].is_null = &username_null;
    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = nickname_buffer;
    results[2].buffer_length = sizeof(nickname_buffer) - 1;
    results[2].length = &nickname_length;
    results[2].is_null = &nickname_null;
    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = hash_buffer;
    results[3].buffer_length = sizeof(hash_buffer) - 1;
    results[3].length = &hash_length;
    results[3].is_null = &hash_null;
    results[4].buffer_type = MYSQL_TYPE_LONG;
    results[4].buffer = &status;
    results[4].buffer_length = sizeof(status);
    results[4].length = &status_length;
    results[4].is_null = &status_null;

    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(stmt.get());
    if (fetch_result == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    user.id = id;
    user.username = stringResult(username_buffer, username_length, username_null,
                                 sizeof(username_buffer) - 1);
    user.nickname = stringResult(nickname_buffer, nickname_length, nickname_null,
                                 sizeof(nickname_buffer) - 1);
    user.password_hash =
        stringResult(hash_buffer, hash_length, hash_null, sizeof(hash_buffer) - 1);
    user.status = status_null ? 0 : status;
    found = !id_null;
    error.clear();
    return true;
}

bool MysqlConnection::updateLastLogin(uint64_t user_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_, "UPDATE users SET last_login_at = UTC_TIMESTAMP(3) WHERE id = ?", error);
    if (stmt.get() == nullptr) {
        return false;
    }
    MYSQL_BIND param = {};
    bindU64(param, user_id);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::revokeSessionsForUser(uint64_t user_id, int reason, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "UPDATE sessions SET revoked = ? WHERE user_id = ? AND revoked = 0", error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int reason_value = reason;
    uint64_t id_value = user_id;
    MYSQL_BIND params[2] = {};
    bindInt(params[0], reason_value);
    bindU64(params[1], id_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::insertSession(const SessionRecord& session,
                                    const std::string& token_hash,
                                    std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "INSERT INTO sessions (token_hash, session_id, device_id, device_name, "
              "platform, app_version, caps, last_seen_at, user_id, expires_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    const std::string last_seen =
        !session.last_seen_at.empty() ? session.last_seen_at
                                      : (!session.created_at.empty()
                                             ? session.created_at
                                             : std::string("1970-01-01 00:00:00"));
    unsigned long token_length = 0;
    unsigned long session_id_length = 0;
    unsigned long device_id_length = 0;
    unsigned long device_name_length = 0;
    unsigned long platform_length = 0;
    unsigned long app_version_length = 0;
    unsigned long last_seen_length = 0;
    unsigned long expires_length = 0;
    uint64_t user_id_value = session.user_id;
    int caps_value = static_cast<int>(session.caps);

    MYSQL_BIND params[10] = {};
    bindString(params[0], token_hash, token_length);
    if (session.session_id.empty()) {
        params[1].buffer_type = MYSQL_TYPE_NULL;
    } else {
        bindString(params[1], session.session_id, session_id_length);
    }
    bindString(params[2], session.device_id, device_id_length);
    bindString(params[3], session.device_name, device_name_length);
    bindString(params[4], session.platform, platform_length);
    bindString(params[5], session.app_version, app_version_length);
    bindInt(params[6], caps_value);
    bindString(params[7], last_seen, last_seen_length);
    bindU64(params[8], user_id_value);
    bindString(params[9], session.expires_at, expires_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

namespace {

bool readSessionRecord(MYSQL_STMT* stmt, SessionRecord& session, bool& found,
                       std::string& error) {
    char session_id_buffer[37] = {0};
    char device_id_buffer[37] = {0};
    char device_name_buffer[65] = {0};
    char platform_buffer[17] = {0};
    char app_version_buffer[33] = {0};
    char created_buffer[32] = {0};
    char last_seen_buffer[32] = {0};
    char expires_buffer[32] = {0};
    uint32_t caps = 0;
    uint64_t user_id = 0;
    int revoked = 0;
    int valid = 0;

    unsigned long session_id_length = 0;
    unsigned long device_id_length = 0;
    unsigned long device_name_length = 0;
    unsigned long platform_length = 0;
    unsigned long app_version_length = 0;
    unsigned long created_length = 0;
    unsigned long last_seen_length = 0;
    unsigned long expires_length = 0;
    unsigned long caps_length = 0;
    unsigned long user_id_length = 0;
    unsigned long revoked_length = 0;
    unsigned long valid_length = 0;
    bool session_id_null = false;
    bool device_id_null = false;
    bool device_name_null = false;
    bool platform_null = false;
    bool app_version_null = false;
    bool created_null = false;
    bool last_seen_null = false;
    bool expires_null = false;
    bool caps_null = false;
    bool user_id_null = false;
    bool revoked_null = false;
    bool valid_null = false;

    MYSQL_BIND results[12] = {};
    results[0].buffer_type = MYSQL_TYPE_STRING;
    results[0].buffer = session_id_buffer;
    results[0].buffer_length = sizeof(session_id_buffer);
    results[0].length = &session_id_length;
    results[0].is_null = &session_id_null;
    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = device_id_buffer;
    results[1].buffer_length = sizeof(device_id_buffer);
    results[1].length = &device_id_length;
    results[1].is_null = &device_id_null;
    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = device_name_buffer;
    results[2].buffer_length = sizeof(device_name_buffer);
    results[2].length = &device_name_length;
    results[2].is_null = &device_name_null;
    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = platform_buffer;
    results[3].buffer_length = sizeof(platform_buffer);
    results[3].length = &platform_length;
    results[3].is_null = &platform_null;
    results[4].buffer_type = MYSQL_TYPE_STRING;
    results[4].buffer = app_version_buffer;
    results[4].buffer_length = sizeof(app_version_buffer);
    results[4].length = &app_version_length;
    results[4].is_null = &app_version_null;
    results[5].buffer_type = MYSQL_TYPE_LONG;
    results[5].buffer = &caps;
    results[5].buffer_length = sizeof(caps);
    results[5].is_unsigned = true;
    results[5].length = &caps_length;
    results[5].is_null = &caps_null;
    results[6].buffer_type = MYSQL_TYPE_STRING;
    results[6].buffer = created_buffer;
    results[6].buffer_length = sizeof(created_buffer);
    results[6].length = &created_length;
    results[6].is_null = &created_null;
    results[7].buffer_type = MYSQL_TYPE_STRING;
    results[7].buffer = last_seen_buffer;
    results[7].buffer_length = sizeof(last_seen_buffer);
    results[7].length = &last_seen_length;
    results[7].is_null = &last_seen_null;
    results[8].buffer_type = MYSQL_TYPE_STRING;
    results[8].buffer = expires_buffer;
    results[8].buffer_length = sizeof(expires_buffer);
    results[8].length = &expires_length;
    results[8].is_null = &expires_null;
    results[9].buffer_type = MYSQL_TYPE_LONGLONG;
    results[9].buffer = &user_id;
    results[9].buffer_length = sizeof(user_id);
    results[9].is_unsigned = true;
    results[9].length = &user_id_length;
    results[9].is_null = &user_id_null;
    results[10].buffer_type = MYSQL_TYPE_LONG;
    results[10].buffer = &revoked;
    results[10].buffer_length = sizeof(revoked);
    results[10].length = &revoked_length;
    results[10].is_null = &revoked_null;
    results[11].buffer_type = MYSQL_TYPE_LONG;
    results[11].buffer = &valid;
    results[11].buffer_length = sizeof(valid);
    results[11].length = &valid_length;
    results[11].is_null = &valid_null;

    if (mysql_stmt_bind_result(stmt, results) != 0) {
        error = mysql_stmt_error(stmt);
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(stmt);
    if (fetch_result == MYSQL_NO_DATA) {
        found = false;
        return true;
    }
    if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt);
        return false;
    }

    found = !user_id_null;
    session.session_id = stringResult(session_id_buffer, session_id_length,
                                      session_id_null, sizeof(session_id_buffer) - 1);
    session.device_id = stringResult(device_id_buffer, device_id_length,
                                     device_id_null, sizeof(device_id_buffer) - 1);
    session.device_name = stringResult(device_name_buffer, device_name_length,
                                       device_name_null, sizeof(device_name_buffer) - 1);
    session.platform = stringResult(platform_buffer, platform_length,
                                    platform_null, sizeof(platform_buffer) - 1);
    session.app_version = stringResult(app_version_buffer, app_version_length,
                                       app_version_null, sizeof(app_version_buffer) - 1);
    session.caps = caps_null ? 0 : caps;
    session.created_at = stringResult(created_buffer, created_length, created_null,
                                      sizeof(created_buffer) - 1);
    session.last_seen_at = stringResult(last_seen_buffer, last_seen_length,
                                        last_seen_null, sizeof(last_seen_buffer) - 1);
    session.expires_at = stringResult(expires_buffer, expires_length, expires_null,
                                      sizeof(expires_buffer) - 1);
    session.user_id = user_id;
    session.revoked = revoked_null ? 0 : revoked;
    session.valid = !valid_null && valid != 0;
    return true;
}

}  // namespace

bool MysqlConnection::findSession(const std::string& token_hash, SessionRecord& session,
                                  bool& found, std::string& error) {
    found = false;
    session = SessionRecord{};
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "SELECT session_id, device_id, device_name, platform, app_version, caps, "
              "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s'), "
              "DATE_FORMAT(last_seen_at, '%Y-%m-%d %H:%i:%s'), "
              "DATE_FORMAT(expires_at, '%Y-%m-%d %H:%i:%s'), user_id, revoked, "
              "(expires_at > UTC_TIMESTAMP()) FROM sessions WHERE token_hash = ? LIMIT 1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    unsigned long token_length = 0;
    MYSQL_BIND param = {};
    bindString(param, token_hash, token_length);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!readSessionRecord(stmt.get(), session, found, error)) {
        return false;
    }
    if (found) {
        session.token_hash = token_hash;
    }
    return true;
}

bool MysqlConnection::setSessionId(const std::string& token_hash,
                                   const std::string& session_id,
                                   std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (session_id.empty()) {
        error = "empty session id";
        return false;
    }

    Stmt stmt(mysql_,
              "UPDATE sessions SET session_id = ? WHERE token_hash = ? "
              "AND (session_id IS NULL OR session_id = '')",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    unsigned long session_id_length = 0;
    unsigned long token_length = 0;
    MYSQL_BIND params[2] = {};
    bindString(params[0], session_id, session_id_length);
    bindString(params[1], token_hash, token_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::revokeSession(const std::string& token_hash, int reason,
                                    std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_, "UPDATE sessions SET revoked = ? WHERE token_hash = ? AND revoked = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int reason_value = reason;
    unsigned long token_length = 0;
    MYSQL_BIND params[2] = {};
    bindInt(params[0], reason_value);
    bindString(params[1], token_hash, token_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::listSessions(uint64_t user_id,
                                   std::vector<SessionRecord>& sessions,
                                   std::string& error) {
    sessions.clear();
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "SELECT session_id, device_id, device_name, platform, app_version, caps, "
              "DATE_FORMAT(created_at, '%Y-%m-%d %H:%i:%s'), "
              "DATE_FORMAT(last_seen_at, '%Y-%m-%d %H:%i:%s'), "
              "DATE_FORMAT(expires_at, '%Y-%m-%d %H:%i:%s'), user_id, revoked, "
              "(expires_at > UTC_TIMESTAMP()) FROM sessions "
              "WHERE user_id = ? AND revoked = 0 AND expires_at > UTC_TIMESTAMP() "
              "ORDER BY last_seen_at DESC",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    uint64_t id_value = user_id;
    MYSQL_BIND param = {};
    bindU64(param, id_value);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    bool found = false;
    do {
        SessionRecord session;
        if (!readSessionRecord(stmt.get(), session, found, error)) {
            return false;
        }
        if (!found) {
            break;
        }
        sessions.push_back(std::move(session));
    } while (true);
    return true;
}

bool MysqlConnection::revokeSessionById(uint64_t user_id,
                                        const std::string& session_id, int reason,
                                        std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "UPDATE sessions SET revoked = ? WHERE user_id = ? AND session_id = ? "
              "AND revoked = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int reason_value = reason;
    uint64_t id_value = user_id;
    unsigned long session_id_length = 0;
    MYSQL_BIND params[3] = {};
    bindInt(params[0], reason_value);
    bindU64(params[1], id_value);
    bindString(params[2], session_id, session_id_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_affected_rows(stmt.get()) == 0) {
        error = "session not found";
        return false;
    }
    return true;
}

bool MysqlConnection::revokeSessionsForDevice(uint64_t user_id,
                                              const std::string& device_id,
                                              int reason, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "UPDATE sessions SET revoked = ? WHERE user_id = ? AND device_id = ? "
              "AND revoked = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int reason_value = reason;
    uint64_t id_value = user_id;
    unsigned long device_length = 0;
    MYSQL_BIND params[3] = {};
    bindInt(params[0], reason_value);
    bindU64(params[1], id_value);
    bindString(params[2], device_id, device_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::revokeLegacySessions(uint64_t user_id, int reason,
                                           std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "UPDATE sessions SET revoked = ? WHERE user_id = ? AND device_id = '' "
              "AND revoked = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int reason_value = reason;
    uint64_t id_value = user_id;
    MYSQL_BIND params[2] = {};
    bindInt(params[0], reason_value);
    bindU64(params[1], id_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::countActiveNewSessions(uint64_t user_id, uint32_t& count,
                                             std::string& error) {
    count = 0;
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "SELECT COUNT(*) FROM sessions WHERE user_id = ? AND device_id <> '' "
              "AND revoked = 0 AND expires_at > UTC_TIMESTAMP()",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t id_value = user_id;
    MYSQL_BIND param = {};
    bindU64(param, id_value);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    uint64_t count_value = 0;
    unsigned long count_length = 0;
    bool count_null = false;
    MYSQL_BIND result = {};
    result.buffer_type = MYSQL_TYPE_LONGLONG;
    result.buffer = &count_value;
    result.buffer_length = sizeof(count_value);
    result.is_unsigned = true;
    result.length = &count_length;
    result.is_null = &count_null;
    if (mysql_stmt_bind_result(stmt.get(), &result) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetch_result = mysql_stmt_fetch(stmt.get());
    if (fetch_result == MYSQL_NO_DATA) {
        return true;
    }
    if (fetch_result != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!count_null) {
        count = static_cast<uint32_t>(
            std::min<uint64_t>(count_value, std::numeric_limits<uint32_t>::max()));
    }
    return true;
}

bool MysqlConnection::revokeOldestNewSession(uint64_t user_id, int reason,
                                             std::string& revoked_session_id,
                                             std::string& error) {
    revoked_session_id.clear();
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "SELECT session_id FROM sessions WHERE user_id = ? AND device_id <> '' "
              "AND revoked = 0 AND expires_at > UTC_TIMESTAMP() "
              "ORDER BY last_seen_at ASC, created_at ASC LIMIT 1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t id_value = user_id;
    MYSQL_BIND param = {};
    bindU64(param, id_value);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    char session_id_buffer[37] = {0};
    unsigned long session_id_length = 0;
    bool session_id_null = false;
    MYSQL_BIND result = {};
    result.buffer_type = MYSQL_TYPE_STRING;
    result.buffer = session_id_buffer;
    result.buffer_length = sizeof(session_id_buffer);
    result.length = &session_id_length;
    result.is_null = &session_id_null;
    if (mysql_stmt_bind_result(stmt.get(), &result) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetch_result = mysql_stmt_fetch(stmt.get());
    if (fetch_result == MYSQL_NO_DATA) {
        return true;
    }
    if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (session_id_null) {
        return true;
    }
    revoked_session_id = stringResult(session_id_buffer, session_id_length,
                                      session_id_null, sizeof(session_id_buffer) - 1);
    if (revoked_session_id.empty()) {
        return true;
    }
    return revokeSessionById(user_id, revoked_session_id, reason, error);
}

bool MysqlConnection::upgradeSessionDevice(const std::string& token_hash,
                                           const DeviceInfo& info,
                                           const std::string& expires_at,
                                           std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "UPDATE sessions SET device_id = ?, device_name = ?, platform = ?, "
              "app_version = ?, caps = ?, last_seen_at = UTC_TIMESTAMP(), expires_at = ? "
              "WHERE token_hash = ? AND revoked = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int caps_value = static_cast<int>(info.caps);
    unsigned long device_id_length = 0;
    unsigned long device_name_length = 0;
    unsigned long platform_length = 0;
    unsigned long app_version_length = 0;
    unsigned long expires_length = 0;
    unsigned long token_length = 0;
    MYSQL_BIND params[7] = {};
    bindString(params[0], info.device_id, device_id_length);
    bindString(params[1], info.device_name, device_name_length);
    bindString(params[2], info.platform, platform_length);
    bindString(params[3], info.app_version, app_version_length);
    bindInt(params[4], caps_value);
    bindString(params[5], expires_at, expires_length);
    bindString(params[6], token_hash, token_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::touchSession(const std::string& token_hash,
                                   const std::string& expires_at,
                                   std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "UPDATE sessions SET last_seen_at = UTC_TIMESTAMP(), expires_at = ? "
              "WHERE token_hash = ? AND revoked = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    unsigned long expires_length = 0;
    unsigned long token_length = 0;
    MYSQL_BIND params[2] = {};
    bindString(params[0], expires_at, expires_length);
    bindString(params[1], token_hash, token_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::touchSessions(const std::vector<std::string>& token_hashes,
                                    const std::string& expires_at,
                                    std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (token_hashes.empty()) {
        return true;
    }

    constexpr std::size_t kChunkSize = 200;
    for (std::size_t offset = 0; offset < token_hashes.size();
         offset += kChunkSize) {
        const std::size_t count =
            std::min(kChunkSize, token_hashes.size() - offset);
        std::string sql =
            "UPDATE sessions SET last_seen_at = UTC_TIMESTAMP(), expires_at = ? "
            "WHERE revoked = 0 AND token_hash IN (";
        for (std::size_t i = 0; i < count; ++i) {
            if (i != 0) {
                sql += ',';
            }
            sql += '?';
        }
        sql += ')';

        Stmt stmt(mysql_, sql.c_str(), error);
        if (stmt.get() == nullptr) {
            return false;
        }
        std::vector<unsigned long> lengths(count + 1, 0);
        std::vector<MYSQL_BIND> params(count + 1);
        bindString(params[0], expires_at, lengths[0]);
        for (std::size_t i = 0; i < count; ++i) {
            bindString(params[i + 1], token_hashes[offset + i], lengths[i + 1]);
        }
        if (mysql_stmt_bind_param(stmt.get(), params.data()) != 0) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        if (!execute(stmt.get(), error)) {
            return false;
        }
    }
    return true;
}

bool MysqlConnection::listRooms(std::vector<RoomRow>& rooms, std::string& error) {
    rooms.clear();
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "SELECT id, name, max_members FROM rooms "
              "WHERE status = 0 ORDER BY is_default DESC, id ASC",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    int id = 0;
    char name_buffer[129] = {0};
    int max_members = 0;
    unsigned long id_length = 0;
    unsigned long name_length = 0;
    unsigned long max_length = 0;
    bool id_null = false;
    bool name_null = false;
    bool max_null = false;

    MYSQL_BIND results[3] = {};
    results[0].buffer_type = MYSQL_TYPE_LONG;
    results[0].buffer = &id;
    results[0].buffer_length = sizeof(id);
    results[0].length = &id_length;
    results[0].is_null = &id_null;
    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = name_buffer;
    results[1].buffer_length = sizeof(name_buffer) - 1;
    results[1].length = &name_length;
    results[1].is_null = &name_null;
    results[2].buffer_type = MYSQL_TYPE_LONG;
    results[2].buffer = &max_members;
    results[2].buffer_length = sizeof(max_members);
    results[2].length = &max_length;
    results[2].is_null = &max_null;

    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    while (true) {
        const int fetch_result = mysql_stmt_fetch(stmt.get());
        if (fetch_result == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        RoomRow row;
        row.id = id;
        row.name = stringResult(name_buffer, name_length, name_null,
                                sizeof(name_buffer) - 1);
        row.max_members = max_null ? 0 : max_members;
        rooms.push_back(std::move(row));
    }
    error.clear();
    return true;
}

bool MysqlConnection::insertMessage(int room_id, uint64_t sender_id,
                                    const std::string& client_msg_id,
                                    const std::string& content, uint64_t reply_to_id,
                                    const std::string& reply_sender_name,
                                    const std::string& reply_content,
                                    const std::string& created_at, uint64_t& message_id,
                                    bool& duplicate, std::string& error) {
    message_id = 0;
    duplicate = false;
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "INSERT INTO messages "
              "(room_id, sender_id, client_msg_id, content, reply_to_id, "
              "reply_sender_name, reply_content, created_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id)",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    int room_value = room_id;
    uint64_t sender_value = sender_id;
    uint64_t reply_value = reply_to_id;
    unsigned long client_length = 0;
    unsigned long content_length = 0;
    unsigned long reply_sender_length = 0;
    unsigned long reply_content_length = 0;
    unsigned long created_length = 0;
    MYSQL_BIND params[8] = {};
    bindInt(params[0], room_value);
    bindU64(params[1], sender_value);
    bindString(params[2], client_msg_id, client_length);
    bindString(params[3], content, content_length);
    bindU64(params[4], reply_value);
    bindString(params[5], reply_sender_name, reply_sender_length);
    bindString(params[6], reply_content, reply_content_length);
    bindString(params[7], created_at, created_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    message_id = mysql_stmt_insert_id(stmt.get());
    duplicate = (mysql_stmt_affected_rows(stmt.get()) == 0);
    if (message_id == 0) {
        error = "insert message returned zero id";
        return false;
    }
    error.clear();
    return true;
}

bool MysqlConnection::insertPrivateMessage(uint64_t sender_id, uint64_t receiver_id,
                                           const std::string& client_msg_id,
                                           const std::string& content,
                                           uint64_t reply_to_id,
                                           const std::string& reply_sender_name,
                                           const std::string& reply_content,
                                           const std::string& created_at,
                                           uint64_t& message_id, bool& duplicate,
                                           std::string& error) {
    message_id = 0;
    duplicate = false;
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "INSERT INTO private_messages "
              "(sender_id, receiver_id, client_msg_id, content, reply_to_id, "
              "reply_sender_name, reply_content, created_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?) ON DUPLICATE KEY UPDATE id = LAST_INSERT_ID(id)",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    uint64_t sender_value = sender_id;
    uint64_t receiver_value = receiver_id;
    uint64_t reply_value = reply_to_id;
    unsigned long client_length = 0;
    unsigned long content_length = 0;
    unsigned long reply_sender_length = 0;
    unsigned long reply_content_length = 0;
    unsigned long created_length = 0;
    MYSQL_BIND params[8] = {};
    bindU64(params[0], sender_value);
    bindU64(params[1], receiver_value);
    bindString(params[2], client_msg_id, client_length);
    bindString(params[3], content, content_length);
    bindU64(params[4], reply_value);
    bindString(params[5], reply_sender_name, reply_sender_length);
    bindString(params[6], reply_content, reply_content_length);
    bindString(params[7], created_at, created_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    message_id = mysql_stmt_insert_id(stmt.get());
    duplicate = (mysql_stmt_affected_rows(stmt.get()) == 0);
    if (message_id == 0) {
        error = "insert private message returned zero id";
        return false;
    }
    error.clear();
    return true;
}

bool MysqlConnection::listRoomHistory(int room_id, uint64_t after_id,
                                      uint64_t before_id, int limit,
                                      HistoryPage& page, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (limit <= 0) {
        limit = 1;
    }
    const uint64_t cursor =
        before_id == 0 ? std::numeric_limits<uint64_t>::max() : before_id;
    const int fetch_limit = limit + 1;

    Stmt stmt(mysql_,
              "SELECT m.id, m.sender_id, u.nickname, m.content, "
              "DATE_FORMAT(m.created_at, '%Y-%m-%d %H:%i:%s'), "
              "m.reply_to_id, m.reply_sender_name, m.reply_content, m.recalled, "
              "m.msg_type, m.file_id "
              "FROM messages m JOIN users u ON u.id = m.sender_id "
              "WHERE m.room_id = ? AND m.id > ? AND m.id < ? "
              "ORDER BY m.id DESC LIMIT ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    int room_value = room_id;
    uint64_t after_value = after_id;
    uint64_t cursor_value = cursor;
    int limit_value = fetch_limit;
    MYSQL_BIND params[4] = {};
    bindInt(params[0], room_value);
    bindU64(params[1], after_value);
    bindU64(params[2], cursor_value);
    bindInt(params[3], limit_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return readHistoryPage(stmt.get(), limit, page, error);
}

bool MysqlConnection::listPrivateHistory(uint64_t user_id, uint64_t peer_id,
                                         uint64_t before_id, int limit, HistoryPage& page,
                                         std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (limit <= 0) {
        limit = 1;
    }
    const uint64_t cursor =
        before_id == 0 ? std::numeric_limits<uint64_t>::max() : before_id;
    const int fetch_limit = limit + 1;

    Stmt stmt(mysql_,
              "SELECT m.id, m.sender_id, u.nickname, m.content, "
              "DATE_FORMAT(m.created_at, '%Y-%m-%d %H:%i:%s'), "
              "m.reply_to_id, m.reply_sender_name, m.reply_content, m.recalled, "
              "m.msg_type, m.file_id "
              "FROM private_messages m JOIN users u ON u.id = m.sender_id "
              "WHERE ((m.sender_id = ? AND m.receiver_id = ?) OR "
              "(m.sender_id = ? AND m.receiver_id = ?)) AND m.id < ? "
              "ORDER BY m.id DESC LIMIT ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    uint64_t user_a = user_id;
    uint64_t peer_a = peer_id;
    uint64_t user_b = user_id;
    uint64_t peer_b = peer_id;
    uint64_t cursor_value = cursor;
    int limit_value = fetch_limit;
    MYSQL_BIND params[6] = {};
    bindU64(params[0], user_a);
    bindU64(params[1], peer_a);
    bindU64(params[2], peer_b);
    bindU64(params[3], user_b);
    bindU64(params[4], cursor_value);
    bindInt(params[5], limit_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return readHistoryPage(stmt.get(), limit, page, error);
}

bool MysqlConnection::listRoomHistoryAfter(int room_id, uint64_t after_id,
                                           int limit, HistoryPage& page,
                                           std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (limit <= 0) {
        limit = 1;
    }
    const int fetch_limit = limit + 1;
    Stmt stmt(mysql_,
              "SELECT m.id, m.sender_id, u.nickname, m.content, "
              "DATE_FORMAT(m.created_at, '%Y-%m-%d %H:%i:%s'), "
              "m.reply_to_id, m.reply_sender_name, m.reply_content, m.recalled, "
              "m.msg_type, m.file_id "
              "FROM messages m JOIN users u ON u.id = m.sender_id "
              "WHERE m.room_id = ? AND m.id > ? "
              "ORDER BY m.id ASC LIMIT ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int room_value = room_id;
    uint64_t after_value = after_id;
    int limit_value = fetch_limit;
    MYSQL_BIND params[3] = {};
    bindInt(params[0], room_value);
    bindU64(params[1], after_value);
    bindInt(params[2], limit_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return readHistoryPage(stmt.get(), limit, page, error, true);
}

bool MysqlConnection::listPrivateHistoryAfter(uint64_t user_id, uint64_t peer_id,
                                              uint64_t after_id, int limit,
                                              HistoryPage& page,
                                              std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (limit <= 0) {
        limit = 1;
    }
    const int fetch_limit = limit + 1;
    Stmt stmt(mysql_,
              "SELECT m.id, m.sender_id, u.nickname, m.content, "
              "DATE_FORMAT(m.created_at, '%Y-%m-%d %H:%i:%s'), "
              "m.reply_to_id, m.reply_sender_name, m.reply_content, m.recalled, "
              "m.msg_type, m.file_id "
              "FROM private_messages m JOIN users u ON u.id = m.sender_id "
              "WHERE ((m.sender_id = ? AND m.receiver_id = ?) OR "
              "(m.sender_id = ? AND m.receiver_id = ?)) AND m.id > ? "
              "ORDER BY m.id ASC LIMIT ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t user_a = user_id;
    uint64_t peer_a = peer_id;
    uint64_t user_b = user_id;
    uint64_t peer_b = peer_id;
    uint64_t after_value = after_id;
    int limit_value = fetch_limit;
    MYSQL_BIND params[6] = {};
    bindU64(params[0], user_a);
    bindU64(params[1], peer_a);
    bindU64(params[2], peer_b);
    bindU64(params[3], user_b);
    bindU64(params[4], after_value);
    bindInt(params[5], limit_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return readHistoryPage(stmt.get(), limit, page, error, true);
}

bool MysqlConnection::listConversations(uint64_t user_id,
                                        std::vector<ConversationRow>& conversations,
                                        std::string& error) {
    conversations.clear();
    if (!ensureConnected(error)) {
        return false;
    }

    Stmt stmt(mysql_,
              "SELECT x.peer_id, u.nickname, m.id, m.content, "
              "DATE_FORMAT(m.created_at, '%Y-%m-%d %H:%i:%s'), "
              "(SELECT COUNT(*) FROM private_messages pm "
              " WHERE pm.sender_id = x.peer_id AND pm.receiver_id = ? "
              " AND pm.id > COALESCE(rs.last_read_message_id, 0)) AS unread "
              "FROM ("
              "  SELECT peer_id, MAX(last_id) AS last_id FROM ("
              "    SELECT receiver_id AS peer_id, MAX(id) AS last_id "
              "    FROM private_messages WHERE sender_id = ? GROUP BY receiver_id "
              "    UNION ALL "
              "    SELECT sender_id AS peer_id, MAX(id) AS last_id "
              "    FROM private_messages WHERE receiver_id = ? GROUP BY sender_id"
              "  ) merged GROUP BY peer_id"
              ") x "
              "LEFT JOIN private_read_state rs "
              "  ON rs.user_id = ? AND rs.peer_id = x.peer_id "
              "JOIN private_messages m ON m.id = x.last_id "
              "JOIN users u ON u.id = x.peer_id "
              "ORDER BY m.id DESC LIMIT 50",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    uint64_t first_user = user_id;
    uint64_t second_user = user_id;
    uint64_t unread_user = user_id;
    uint64_t read_user = user_id;
    MYSQL_BIND params[4] = {};
    bindU64(params[0], first_user);
    bindU64(params[1], second_user);
    bindU64(params[2], unread_user);
    bindU64(params[3], read_user);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    uint64_t peer_id = 0;
    char peer_nickname[129] = {0};
    uint64_t last_id = 0;
    char last_content[2048] = {0};
    char last_time[32] = {0};
    uint64_t unread_count = 0;
    unsigned long peer_id_length = 0;
    unsigned long nickname_length = 0;
    unsigned long last_id_length = 0;
    unsigned long content_length = 0;
    unsigned long time_length = 0;
    unsigned long unread_length = 0;
    bool peer_id_null = false;
    bool nickname_null = false;
    bool last_id_null = false;
    bool content_null = false;
    bool time_null = false;
    bool unread_null = false;

    MYSQL_BIND results[6] = {};
    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &peer_id;
    results[0].buffer_length = sizeof(peer_id);
    results[0].is_unsigned = true;
    results[0].length = &peer_id_length;
    results[0].is_null = &peer_id_null;
    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = peer_nickname;
    results[1].buffer_length = sizeof(peer_nickname) - 1;
    results[1].length = &nickname_length;
    results[1].is_null = &nickname_null;
    results[2].buffer_type = MYSQL_TYPE_LONGLONG;
    results[2].buffer = &last_id;
    results[2].buffer_length = sizeof(last_id);
    results[2].is_unsigned = true;
    results[2].length = &last_id_length;
    results[2].is_null = &last_id_null;
    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = last_content;
    results[3].buffer_length = sizeof(last_content) - 1;
    results[3].length = &content_length;
    results[3].is_null = &content_null;
    results[4].buffer_type = MYSQL_TYPE_STRING;
    results[4].buffer = last_time;
    results[4].buffer_length = sizeof(last_time) - 1;
    results[4].length = &time_length;
    results[4].is_null = &time_null;
    results[5].buffer_type = MYSQL_TYPE_LONGLONG;
    results[5].buffer = &unread_count;
    results[5].buffer_length = sizeof(unread_count);
    results[5].is_unsigned = true;
    results[5].length = &unread_length;
    results[5].is_null = &unread_null;

    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    while (true) {
        const int fetch_result = mysql_stmt_fetch(stmt.get());
        if (fetch_result == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        ConversationRow row;
        row.peer_id = peer_id;
        row.nickname = stringResult(peer_nickname, nickname_length, nickname_null,
                                    sizeof(peer_nickname) - 1);
        row.last_message_id = last_id;
        row.last_content = stringResult(last_content, content_length, content_null,
                                        sizeof(last_content) - 1);
        row.last_time = stringResult(last_time, time_length, time_null,
                                     sizeof(last_time) - 1);
        row.unread = unread_null
                         ? 0
                         : static_cast<uint32_t>(std::min<uint64_t>(
                               unread_count, std::numeric_limits<uint32_t>::max()));
        conversations.push_back(std::move(row));
    }
    error.clear();
    return true;
}

bool MysqlConnection::markPrivateRead(uint64_t user_id, uint64_t peer_id,
                                      uint64_t message_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "INSERT INTO private_read_state (user_id, peer_id, last_read_message_id) "
              "VALUES (?, ?, ?) ON DUPLICATE KEY UPDATE "
              "last_read_message_id = GREATEST(last_read_message_id, VALUES(last_read_message_id))",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t user_value = user_id;
    uint64_t peer_value = peer_id;
    uint64_t message_value = message_id;
    MYSQL_BIND params[3] = {};
    bindU64(params[0], user_value);
    bindU64(params[1], peer_value);
    bindU64(params[2], message_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::searchRoomMessages(int room_id, uint64_t after_id,
                                         const std::string& keyword, int limit,
                                         std::vector<SearchResultRow>& results,
                                         std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (limit <= 0 || limit > 50) {
        limit = 20;
    }
    Stmt stmt(mysql_,
              "SELECT m.id, m.sender_id, u.nickname, m.content, "
              "DATE_FORMAT(m.created_at, '%Y-%m-%d %H:%i:%s'), m.recalled, "
              "m.msg_type, m.file_id "
              "FROM messages m JOIN users u ON u.id = m.sender_id "
              "WHERE m.room_id = ? AND m.id > ? AND m.content <> '' "
              "AND m.content LIKE ? ESCAPE '!' "
              "ORDER BY m.id DESC LIMIT ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int room_value = room_id;
    uint64_t after_value = after_id;
    int limit_value = limit;
    const std::string pattern = likePattern(keyword);
    unsigned long pattern_length = 0;
    MYSQL_BIND params[4] = {};
    bindInt(params[0], room_value);
    bindU64(params[1], after_value);
    bindString(params[2], pattern, pattern_length);
    bindInt(params[3], limit_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    return readSearchRows(stmt.get(), limit, results, error);
}

bool MysqlConnection::searchPrivateMessages(uint64_t user_id, uint64_t peer_id,
                                            const std::string& keyword, int limit,
                                            std::vector<SearchResultRow>& results,
                                            std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (limit <= 0 || limit > 50) {
        limit = 20;
    }
    Stmt stmt(mysql_,
              "SELECT m.id, m.sender_id, u.nickname, m.content, "
              "DATE_FORMAT(m.created_at, '%Y-%m-%d %H:%i:%s'), m.recalled, "
              "m.msg_type, m.file_id "
              "FROM private_messages m JOIN users u ON u.id = m.sender_id "
              "WHERE ((m.sender_id = ? AND m.receiver_id = ?) OR "
              "(m.sender_id = ? AND m.receiver_id = ?)) "
              "AND m.content <> '' AND m.content LIKE ? ESCAPE '!' "
              "ORDER BY m.id DESC LIMIT ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t user_a = user_id;
    uint64_t peer_a = peer_id;
    uint64_t user_b = user_id;
    uint64_t peer_b = peer_id;
    int limit_value = limit;
    const std::string pattern = likePattern(keyword);
    unsigned long pattern_length = 0;
    MYSQL_BIND params[6] = {};
    bindU64(params[0], user_a);
    bindU64(params[1], peer_a);
    bindU64(params[2], peer_b);
    bindU64(params[3], user_b);
    bindString(params[4], pattern, pattern_length);
    bindInt(params[5], limit_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    return readSearchRows(stmt.get(), limit, results, error);
}

bool MysqlConnection::recallRoomMessage(int room_id, uint64_t message_id,
                                        uint64_t user_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "UPDATE messages SET recalled = 1, content = '' "
              "WHERE id = ? AND room_id = ? AND sender_id = ? AND recalled = 0 "
              "AND created_at >= UTC_TIMESTAMP() - INTERVAL 2 MINUTE",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t message_value = message_id;
    int room_value = room_id;
    uint64_t user_value = user_id;
    MYSQL_BIND params[3] = {};
    bindU64(params[0], message_value);
    bindInt(params[1], room_value);
    bindU64(params[2], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_affected_rows(stmt.get()) == 0) {
        error = "message cannot be recalled";
        return false;
    }
    error.clear();
    return true;
}

bool MysqlConnection::recallPrivateMessage(uint64_t user_id, uint64_t peer_id,
                                           uint64_t message_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "UPDATE private_messages SET recalled = 1, content = '' "
              "WHERE id = ? AND sender_id = ? AND receiver_id = ? AND recalled = 0 "
              "AND created_at >= UTC_TIMESTAMP() - INTERVAL 2 MINUTE",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t message_value = message_id;
    uint64_t user_value = user_id;
    uint64_t peer_value = peer_id;
    MYSQL_BIND params[3] = {};
    bindU64(params[0], message_value);
    bindU64(params[1], user_value);
    bindU64(params[2], peer_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_affected_rows(stmt.get()) == 0) {
        error = "message cannot be recalled";
        return false;
    }
    error.clear();
    return true;
}

bool MysqlConnection::areFriends(uint64_t user_a, uint64_t user_b, bool& friends,
                                 std::string& error) {
    friends = false;
    if (!ensureConnected(error)) {
        return false;
    }
    const uint64_t low = std::min(user_a, user_b);
    const uint64_t high = std::max(user_a, user_b);
    Stmt stmt(mysql_,
              "SELECT 1 FROM friendships WHERE user_low = ? AND user_high = ? LIMIT 1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t low_value = low;
    uint64_t high_value = high;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], low_value);
    bindU64(params[1], high_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetch_result = mysql_stmt_fetch(stmt.get());
    friends = fetch_result == 0;
    if (fetch_result != 0 && fetch_result != MYSQL_NO_DATA) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    error.clear();
    return true;
}

bool MysqlConnection::createFriendRequest(uint64_t requester_id, uint64_t target_id,
                                          bool& already_pending, bool& reverse_pending,
                                          std::string& error) {
    already_pending = false;
    reverse_pending = false;
    bool friends = false;
    if (!areFriends(requester_id, target_id, friends, error)) {
        return false;
    }
    if (friends) {
        error = "already friends";
        return false;
    }

    {
        Stmt stmt(mysql_,
                  "SELECT 1 FROM friend_requests WHERE requester_id = ? AND target_id = ? "
                  "AND status = 0 LIMIT 1",
                  error);
        if (stmt.get() == nullptr) {
            return false;
        }
        uint64_t requester_value = target_id;
        uint64_t target_value = requester_id;
        MYSQL_BIND params[2] = {};
        bindU64(params[0], requester_value);
        bindU64(params[1], target_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        if (!execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
            if (error.empty()) {
                error = mysql_stmt_error(stmt.get());
            }
            return false;
        }
        reverse_pending = mysql_stmt_fetch(stmt.get()) == 0;
        if (reverse_pending) {
            return true;
        }
    }

    {
        Stmt stmt(mysql_,
                  "SELECT 1 FROM friend_requests WHERE requester_id = ? AND target_id = ? "
                  "AND status = 0 LIMIT 1",
                  error);
        if (stmt.get() == nullptr) {
            return false;
        }
        uint64_t requester_value = requester_id;
        uint64_t target_value = target_id;
        MYSQL_BIND params[2] = {};
        bindU64(params[0], requester_value);
        bindU64(params[1], target_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        if (!execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
            if (error.empty()) {
                error = mysql_stmt_error(stmt.get());
            }
            return false;
        }
        if (mysql_stmt_fetch(stmt.get()) == 0) {
            already_pending = true;
            error.clear();
            return true;
        }
    }

    Stmt stmt(mysql_,
              "INSERT INTO friend_requests "
              "(requester_id, target_id, status, created_at, updated_at) "
              "VALUES (?, ?, 0, NOW(), NOW()) "
              "ON DUPLICATE KEY UPDATE status = 0, updated_at = NOW()",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t requester_value = requester_id;
    uint64_t target_value = target_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], requester_value);
    bindU64(params[1], target_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::respondFriendRequest(uint64_t requester_id, uint64_t target_id,
                                           bool accept, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "UPDATE friend_requests SET status = ? "
              "WHERE requester_id = ? AND target_id = ? AND status = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int status_value = accept ? 1 : 2;
    uint64_t requester_value = requester_id;
    uint64_t target_value = target_id;
    MYSQL_BIND params[3] = {};
    bindInt(params[0], status_value);
    bindU64(params[1], requester_value);
    bindU64(params[2], target_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_affected_rows(stmt.get()) == 0) {
        error = "friend request is not pending";
        return false;
    }
    if (!accept) {
        error.clear();
        return true;
    }

    const uint64_t low = std::min(requester_id, target_id);
    const uint64_t high = std::max(requester_id, target_id);
    Stmt insert(mysql_,
                "INSERT IGNORE INTO friendships (user_low, user_high, created_at) "
                "VALUES (?, ?, NOW())",
                error);
    if (insert.get() == nullptr) {
        return false;
    }
    uint64_t low_value = low;
    uint64_t high_value = high;
    MYSQL_BIND insert_params[2] = {};
    bindU64(insert_params[0], low_value);
    bindU64(insert_params[1], high_value);
    if (mysql_stmt_bind_param(insert.get(), insert_params) != 0) {
        error = mysql_stmt_error(insert.get());
        return false;
    }
    return execute(insert.get(), error);
}

bool MysqlConnection::listFriends(uint64_t user_id,
                                  std::vector<UserSearchRow>& friends,
                                  std::string& error) {
    friends.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT u.id, u.username, u.nickname, COALESCE(r.remark, ''), u.avatar_version "
              "FROM friendships f "
              "JOIN users u ON u.id = IF(f.user_low = ?, f.user_high, f.user_low) "
              "LEFT JOIN friend_remarks r ON r.owner_id = ? AND r.friend_id = u.id "
              "WHERE f.user_low = ? OR f.user_high = ? "
              "ORDER BY u.nickname",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t user_a = user_id;
    uint64_t user_b = user_id;
    uint64_t user_c = user_id;
    uint64_t user_d = user_id;
    MYSQL_BIND params[4] = {};
    bindU64(params[0], user_a);
    bindU64(params[1], user_b);
    bindU64(params[2], user_c);
    bindU64(params[3], user_d);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }

    uint64_t id = 0;
    char username_buffer[33] = {0};
    char nickname_buffer[129] = {0};
    char remark_buffer[129] = {0};
    uint32_t avatar_version = 0;
    unsigned long id_length = 0;
    unsigned long username_length = 0;
    unsigned long nickname_length = 0;
    unsigned long remark_length = 0;
    unsigned long avatar_version_length = 0;
    bool id_null = false;
    bool username_null = false;
    bool nickname_null = false;
    bool remark_null = false;
    bool avatar_version_null = false;
    MYSQL_BIND results[5] = {};
    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;
    results[0].buffer_length = sizeof(id);
    results[0].is_unsigned = true;
    results[0].length = &id_length;
    results[0].is_null = &id_null;
    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = username_buffer;
    results[1].buffer_length = sizeof(username_buffer) - 1;
    results[1].length = &username_length;
    results[1].is_null = &username_null;
    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = nickname_buffer;
    results[2].buffer_length = sizeof(nickname_buffer) - 1;
    results[2].length = &nickname_length;
    results[2].is_null = &nickname_null;
    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = remark_buffer;
    results[3].buffer_length = sizeof(remark_buffer) - 1;
    results[3].length = &remark_length;
    results[3].is_null = &remark_null;
    results[4].buffer_type = MYSQL_TYPE_LONG;
    results[4].buffer = &avatar_version;
    results[4].buffer_length = sizeof(avatar_version);
    results[4].is_unsigned = true;
    results[4].length = &avatar_version_length;
    results[4].is_null = &avatar_version_null;
    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    while (true) {
        const int fetch_result = mysql_stmt_fetch(stmt.get());
        if (fetch_result == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        UserSearchRow row;
        row.id = id_null ? 0 : id;
        row.username = stringResult(username_buffer, username_length, username_null,
                                    sizeof(username_buffer) - 1);
        row.nickname = stringResult(nickname_buffer, nickname_length, nickname_null,
                                    sizeof(nickname_buffer) - 1);
        row.remark = stringResult(remark_buffer, remark_length, remark_null,
                                  sizeof(remark_buffer) - 1);
        row.avatar_version = avatar_version_null ? 0 : avatar_version;
        friends.push_back(std::move(row));
    }
    error.clear();
    return true;
}

bool MysqlConnection::listFriendRequests(uint64_t user_id,
                                         std::vector<FriendRequestRow>& requests,
                                         std::string& error) {
    requests.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT u.id, u.username, u.nickname, "
              "DATE_FORMAT(r.created_at, '%Y-%m-%d %H:%i:%s') "
              "FROM friend_requests r JOIN users u ON u.id = r.requester_id "
              "WHERE r.target_id = ? AND r.status = 0 ORDER BY r.id DESC",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t user_value = user_id;
    MYSQL_BIND param = {};
    bindU64(param, user_value);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    uint64_t id = 0;
    char username_buffer[33] = {0};
    char nickname_buffer[129] = {0};
    char created_buffer[32] = {0};
    unsigned long id_length = 0;
    unsigned long username_length = 0;
    unsigned long nickname_length = 0;
    unsigned long created_length = 0;
    bool id_null = false;
    bool username_null = false;
    bool nickname_null = false;
    bool created_null = false;
    MYSQL_BIND results[4] = {};
    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;
    results[0].buffer_length = sizeof(id);
    results[0].is_unsigned = true;
    results[0].length = &id_length;
    results[0].is_null = &id_null;
    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = username_buffer;
    results[1].buffer_length = sizeof(username_buffer) - 1;
    results[1].length = &username_length;
    results[1].is_null = &username_null;
    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = nickname_buffer;
    results[2].buffer_length = sizeof(nickname_buffer) - 1;
    results[2].length = &nickname_length;
    results[2].is_null = &nickname_null;
    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = created_buffer;
    results[3].buffer_length = sizeof(created_buffer) - 1;
    results[3].length = &created_length;
    results[3].is_null = &created_null;
    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    while (true) {
        const int fetch_result = mysql_stmt_fetch(stmt.get());
        if (fetch_result == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        FriendRequestRow row;
        row.id = id_null ? 0 : id;
        row.username = stringResult(username_buffer, username_length, username_null,
                                    sizeof(username_buffer) - 1);
        row.nickname = stringResult(nickname_buffer, nickname_length, nickname_null,
                                    sizeof(nickname_buffer) - 1);
        row.created_at = stringResult(created_buffer, created_length, created_null,
                                      sizeof(created_buffer) - 1);
        requests.push_back(std::move(row));
    }
    error.clear();
    return true;
}

bool MysqlConnection::removeFriend(uint64_t user_id, uint64_t friend_id,
                                   std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    const uint64_t low = std::min(user_id, friend_id);
    const uint64_t high = std::max(user_id, friend_id);
    Stmt stmt(mysql_,
              "DELETE FROM friendships WHERE user_low = ? AND user_high = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t low_value = low;
    uint64_t high_value = high;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], low_value);
    bindU64(params[1], high_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::setFriendRemark(uint64_t user_id, uint64_t friend_id,
                                      const std::string& remark, std::string& error) {
    bool friends = false;
    if (!areFriends(user_id, friend_id, friends, error)) {
        return false;
    }
    if (!friends) {
        error = "not friends";
        return false;
    }
    if (remark.empty()) {
        Stmt stmt(mysql_,
                  "DELETE FROM friend_remarks WHERE owner_id = ? AND friend_id = ?",
                  error);
        if (stmt.get() == nullptr) {
            return false;
        }
        uint64_t owner_value = user_id;
        uint64_t friend_value = friend_id;
        MYSQL_BIND params[2] = {};
        bindU64(params[0], owner_value);
        bindU64(params[1], friend_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        return execute(stmt.get(), error);
    }
    Stmt stmt(mysql_,
              "INSERT INTO friend_remarks (owner_id, friend_id, remark, updated_at) "
              "VALUES (?, ?, ?, NOW()) "
              "ON DUPLICATE KEY UPDATE remark = VALUES(remark), updated_at = NOW()",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t owner_value = user_id;
    uint64_t friend_value = friend_id;
    unsigned long remark_length = 0;
    MYSQL_BIND params[3] = {};
    bindU64(params[0], owner_value);
    bindU64(params[1], friend_value);
    bindString(params[2], remark, remark_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::searchUsers(const std::string& keyword,
                                 std::vector<UserSearchRow>& users,
                                 std::string& error) {
    users.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT id, username, nickname, avatar_version FROM users "
              "WHERE status = 0 AND (username LIKE ? ESCAPE '!' "
              "OR nickname LIKE ? ESCAPE '!' OR id = ?) "
              "ORDER BY username LIMIT 20",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }

    std::string id_text = keyword;
    if (id_text.size() > 1 && (id_text[0] == 'U' || id_text[0] == 'u')) {
        id_text.erase(id_text.begin());
    }
    uint64_t numeric_id = 0;
    bool has_numeric_id = !id_text.empty();
    for (const char ch : id_text) {
        if (ch < '0' || ch > '9') {
            has_numeric_id = false;
            break;
        }
        const uint64_t digit = static_cast<uint64_t>(ch - '0');
        if (numeric_id > (std::numeric_limits<uint64_t>::max() - digit) / 10) {
            has_numeric_id = false;
            break;
        }
        numeric_id = numeric_id * 10 + digit;
    }
    if (!has_numeric_id) {
        numeric_id = 0;
    }

    std::string escaped;
    escaped.reserve(keyword.size());
    for (const char ch : keyword) {
        if (ch == '!' || ch == '%' || ch == '_') {
            escaped.push_back('!');
        }
        escaped.push_back(ch);
    }
    const std::string pattern = "%" + escaped + "%";
    std::string username_pattern = pattern;
    std::string nickname_pattern = pattern;
    unsigned long username_pattern_length = 0;
    unsigned long nickname_pattern_length = 0;
    uint64_t id_param = numeric_id;
    MYSQL_BIND params[3] = {};
    bindString(params[0], username_pattern, username_pattern_length);
    bindString(params[1], nickname_pattern, nickname_pattern_length);
    bindU64(params[2], id_param);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    uint64_t id = 0;
    char username_buffer[33] = {0};
    char nickname_buffer[129] = {0};
    uint32_t avatar_version = 0;
    unsigned long id_length = 0;
    unsigned long username_length = 0;
    unsigned long nickname_length = 0;
    unsigned long avatar_version_length = 0;
    bool id_null = false;
    bool username_null = false;
    bool nickname_null = false;
    bool avatar_version_null = false;

    MYSQL_BIND results[4] = {};
    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;
    results[0].buffer_length = sizeof(id);
    results[0].is_unsigned = true;
    results[0].length = &id_length;
    results[0].is_null = &id_null;
    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = username_buffer;
    results[1].buffer_length = sizeof(username_buffer) - 1;
    results[1].length = &username_length;
    results[1].is_null = &username_null;
    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = nickname_buffer;
    results[2].buffer_length = sizeof(nickname_buffer) - 1;
    results[2].length = &nickname_length;
    results[2].is_null = &nickname_null;
    results[3].buffer_type = MYSQL_TYPE_LONG;
    results[3].buffer = &avatar_version;
    results[3].buffer_length = sizeof(avatar_version);
    results[3].is_unsigned = true;
    results[3].length = &avatar_version_length;
    results[3].is_null = &avatar_version_null;

    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    while (true) {
        const int fetch_result = mysql_stmt_fetch(stmt.get());
        if (fetch_result == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        UserSearchRow row;
        row.id = id_null ? 0 : id;
        row.username = stringResult(username_buffer, username_length, username_null,
                                    sizeof(username_buffer) - 1);
        row.nickname = stringResult(nickname_buffer, nickname_length, nickname_null,
                                    sizeof(nickname_buffer) - 1);
        row.avatar_version = avatar_version_null ? 0 : avatar_version;
        users.push_back(std::move(row));
    }
    error.clear();
    return true;
}

bool MysqlConnection::getUserProfile(uint64_t target_id, UserProfile& profile, bool& found,
                                     std::string& error) {
    found = false;
    profile = UserProfile{};
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT id, username, nickname, signature, avatar_version, avatar "
              "FROM users WHERE id = ? LIMIT 1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    MYSQL_BIND param = {};
    bindU64(param, target_id);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    uint64_t id = 0;
    char username_buffer[33] = {0};
    char nickname_buffer[129] = {0};
    char signature_buffer[128 * 4 + 1] = {0};
    uint32_t avatar_version = 0;
    char avatar_buffer[48 * 1024 + 1] = {0};
    unsigned long id_length = 0;
    unsigned long username_length = 0;
    unsigned long nickname_length = 0;
    unsigned long signature_length = 0;
    unsigned long avatar_version_length = 0;
    unsigned long avatar_length = 0;
    bool id_null = false;
    bool username_null = false;
    bool nickname_null = false;
    bool signature_null = false;
    bool avatar_version_null = false;
    bool avatar_null = false;

    MYSQL_BIND results[6] = {};
    results[0].buffer_type = MYSQL_TYPE_LONGLONG;
    results[0].buffer = &id;
    results[0].buffer_length = sizeof(id);
    results[0].is_unsigned = true;
    results[0].length = &id_length;
    results[0].is_null = &id_null;
    results[1].buffer_type = MYSQL_TYPE_STRING;
    results[1].buffer = username_buffer;
    results[1].buffer_length = sizeof(username_buffer) - 1;
    results[1].length = &username_length;
    results[1].is_null = &username_null;
    results[2].buffer_type = MYSQL_TYPE_STRING;
    results[2].buffer = nickname_buffer;
    results[2].buffer_length = sizeof(nickname_buffer) - 1;
    results[2].length = &nickname_length;
    results[2].is_null = &nickname_null;
    results[3].buffer_type = MYSQL_TYPE_STRING;
    results[3].buffer = signature_buffer;
    results[3].buffer_length = sizeof(signature_buffer) - 1;
    results[3].length = &signature_length;
    results[3].is_null = &signature_null;
    results[4].buffer_type = MYSQL_TYPE_LONG;
    results[4].buffer = &avatar_version;
    results[4].buffer_length = sizeof(avatar_version);
    results[4].is_unsigned = true;
    results[4].length = &avatar_version_length;
    results[4].is_null = &avatar_version_null;
    results[5].buffer_type = MYSQL_TYPE_BLOB;
    results[5].buffer = avatar_buffer;
    results[5].buffer_length = sizeof(avatar_buffer) - 1;
    results[5].length = &avatar_length;
    results[5].is_null = &avatar_null;

    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(stmt.get());
    if (fetch_result == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    profile.id = id_null ? 0 : id;
    profile.username = stringResult(username_buffer, username_length, username_null,
                                    sizeof(username_buffer) - 1);
    profile.nickname = stringResult(nickname_buffer, nickname_length, nickname_null,
                                    sizeof(nickname_buffer) - 1);
    profile.signature = stringResult(signature_buffer, signature_length, signature_null,
                                     sizeof(signature_buffer) - 1);
    profile.avatar_version = avatar_version_null ? 0 : avatar_version;
    profile.avatar = stringResult(avatar_buffer, avatar_length, avatar_null,
                                  sizeof(avatar_buffer) - 1);
    found = !id_null;
    error.clear();
    return true;
}

bool MysqlConnection::getAvatar(uint64_t target_id, uint32_t& version, std::string& avatar,
                                bool& found, std::string& error) {
    found = false;
    version = 0;
    avatar.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT avatar_version, avatar FROM users WHERE id = ? LIMIT 1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    MYSQL_BIND param = {};
    bindU64(param, target_id);
    if (mysql_stmt_bind_param(stmt.get(), &param) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    uint32_t stored_version = 0;
    char avatar_buffer[48 * 1024 + 1] = {0};
    unsigned long version_length = 0;
    unsigned long avatar_length = 0;
    bool version_null = false;
    bool avatar_null = false;
    MYSQL_BIND results[2] = {};
    results[0].buffer_type = MYSQL_TYPE_LONG;
    results[0].buffer = &stored_version;
    results[0].buffer_length = sizeof(stored_version);
    results[0].is_unsigned = true;
    results[0].length = &version_length;
    results[0].is_null = &version_null;
    results[1].buffer_type = MYSQL_TYPE_BLOB;
    results[1].buffer = avatar_buffer;
    results[1].buffer_length = sizeof(avatar_buffer) - 1;
    results[1].length = &avatar_length;
    results[1].is_null = &avatar_null;

    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(stmt.get());
    if (fetch_result == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    version = version_null ? 0 : stored_version;
    avatar = stringResult(avatar_buffer, avatar_length, avatar_null,
                          sizeof(avatar_buffer) - 1);
    found = true;
    error.clear();
    return true;
}

bool MysqlConnection::updateUserProfile(uint64_t user_id, const std::string& nickname,
                                        const std::string& signature, uint8_t avatar_action,
                                        const std::string& avatar, uint32_t& version,
                                        std::string& error) {
    if (avatar_action > 2) {
        error = "invalid avatar action";
        return false;
    }
    if (!ensureConnected(error)) {
        return false;
    }

    const char* sql = nullptr;
    if (avatar_action == 0) {
        sql = "UPDATE users SET nickname = ?, signature = ? WHERE id = ?";
    } else if (avatar_action == 1) {
        sql = "UPDATE users SET nickname = ?, signature = ?, avatar = ?, "
              "avatar_version = avatar_version + 1 WHERE id = ?";
    } else {
        sql = "UPDATE users SET nickname = ?, signature = ?, avatar = ?, "
              "avatar_version = avatar_version + 1 WHERE id = ?";
    }

    Stmt stmt(mysql_, sql, error);
    if (stmt.get() == nullptr) {
        return false;
    }
    unsigned long nickname_length = 0;
    unsigned long signature_length = 0;
    unsigned long avatar_length = 0;
    uint64_t id_value = user_id;
    MYSQL_BIND params[4] = {};
    bindString(params[0], nickname, nickname_length);
    bindString(params[1], signature, signature_length);
    if (avatar_action == 0) {
        bindU64(params[2], id_value);
    } else if (avatar_action == 1) {
        bindBlob(params[2], avatar, avatar_length);
        bindU64(params[3], id_value);
    } else {
        params[2].buffer_type = MYSQL_TYPE_NULL;
        bindU64(params[3], id_value);
    }
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }

    Stmt read_stmt(mysql_,
                   "SELECT avatar_version FROM users WHERE id = ? LIMIT 1",
                   error);
    if (read_stmt.get() == nullptr) {
        return false;
    }
    MYSQL_BIND read_param = {};
    bindU64(read_param, id_value);
    if (mysql_stmt_bind_param(read_stmt.get(), &read_param) != 0) {
        error = mysql_stmt_error(read_stmt.get());
        return false;
    }
    if (!execute(read_stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(read_stmt.get()) != 0) {
        error = mysql_stmt_error(read_stmt.get());
        return false;
    }

    uint32_t stored_version = 0;
    unsigned long version_length = 0;
    bool version_null = false;
    MYSQL_BIND read_result = {};
    read_result.buffer_type = MYSQL_TYPE_LONG;
    read_result.buffer = &stored_version;
    read_result.buffer_length = sizeof(stored_version);
    read_result.is_unsigned = true;
    read_result.length = &version_length;
    read_result.is_null = &version_null;
    if (mysql_stmt_bind_result(read_stmt.get(), &read_result) != 0) {
        error = mysql_stmt_error(read_stmt.get());
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(read_stmt.get());
    if (fetch_result == MYSQL_NO_DATA) {
        error = "user not found";
        return false;
    }
    if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(read_stmt.get());
        return false;
    }
    version = version_null ? 0 : stored_version;
    error.clear();
    return true;
}

bool MysqlConnection::getProfileRelation(uint64_t viewer_id, uint64_t target_id,
                                         uint8_t& relation, std::string& remark,
                                         std::string& error) {
    relation = 2;
    remark.clear();
    if (viewer_id == target_id) {
        relation = 0;
        error.clear();
        return true;
    }
    if (!ensureConnected(error)) {
        return false;
    }

    const uint64_t low = std::min(viewer_id, target_id);
    const uint64_t high = std::max(viewer_id, target_id);
    {
        Stmt stmt(mysql_,
                  "SELECT 1 FROM friendships WHERE user_low = ? AND user_high = ? LIMIT 1",
                  error);
        if (stmt.get() == nullptr) {
            return false;
        }
        uint64_t low_value = low;
        uint64_t high_value = high;
        MYSQL_BIND params[2] = {};
        bindU64(params[0], low_value);
        bindU64(params[1], high_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        if (!execute(stmt.get(), error)) {
            return false;
        }
        if (mysql_stmt_store_result(stmt.get()) != 0) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        const int fetch_result = mysql_stmt_fetch(stmt.get());
        if (fetch_result == 0) {
            relation = 1;
        } else if (fetch_result != MYSQL_NO_DATA) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
    }

    if (relation != 1) {
        relation = 2;
        {
            Stmt stmt(mysql_,
                      "SELECT 1 FROM friend_requests WHERE requester_id = ? AND target_id = ? "
                      "AND status = 0 LIMIT 1",
                      error);
            if (stmt.get() == nullptr) {
                return false;
            }
            uint64_t requester_value = viewer_id;
            uint64_t target_value = target_id;
            MYSQL_BIND params[2] = {};
            bindU64(params[0], requester_value);
            bindU64(params[1], target_value);
            if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
                error = mysql_stmt_error(stmt.get());
                return false;
            }
            if (!execute(stmt.get(), error)) {
                return false;
            }
            if (mysql_stmt_store_result(stmt.get()) != 0) {
                error = mysql_stmt_error(stmt.get());
                return false;
            }
            const int fetch_result = mysql_stmt_fetch(stmt.get());
            if (fetch_result == 0) {
                relation = 3;
            } else if (fetch_result != MYSQL_NO_DATA) {
                error = mysql_stmt_error(stmt.get());
                return false;
            }
        }
        if (relation == 2) {
            Stmt stmt(mysql_,
                      "SELECT 1 FROM friend_requests WHERE requester_id = ? AND target_id = ? "
                      "AND status = 0 LIMIT 1",
                      error);
            if (stmt.get() == nullptr) {
                return false;
            }
            uint64_t requester_value = target_id;
            uint64_t target_value = viewer_id;
            MYSQL_BIND params[2] = {};
            bindU64(params[0], requester_value);
            bindU64(params[1], target_value);
            if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
                error = mysql_stmt_error(stmt.get());
                return false;
            }
            if (!execute(stmt.get(), error)) {
                return false;
            }
            if (mysql_stmt_store_result(stmt.get()) != 0) {
                error = mysql_stmt_error(stmt.get());
                return false;
            }
            const int fetch_result = mysql_stmt_fetch(stmt.get());
            if (fetch_result == 0) {
                relation = 4;
            } else if (fetch_result != MYSQL_NO_DATA) {
                error = mysql_stmt_error(stmt.get());
                return false;
            }
        }
    }

    if (relation != 1) {
        error.clear();
        return true;
    }

    Stmt stmt(mysql_,
              "SELECT remark FROM friend_remarks WHERE owner_id = ? AND friend_id = ? LIMIT 1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t owner_value = viewer_id;
    uint64_t friend_value = target_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], owner_value);
    bindU64(params[1], friend_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    char remark_buffer[129] = {0};
    unsigned long remark_length = 0;
    bool remark_null = false;
    MYSQL_BIND remark_result = {};
    remark_result.buffer_type = MYSQL_TYPE_STRING;
    remark_result.buffer = remark_buffer;
    remark_result.buffer_length = sizeof(remark_buffer) - 1;
    remark_result.length = &remark_length;
    remark_result.is_null = &remark_null;
    if (mysql_stmt_bind_result(stmt.get(), &remark_result) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    const int fetch_result = mysql_stmt_fetch(stmt.get());
    if (fetch_result == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    remark = stringResult(remark_buffer, remark_length, remark_null,
                          sizeof(remark_buffer) - 1);
    error.clear();
    return true;
}

bool MysqlConnection::listFriendIds(uint64_t user_id, std::vector<uint64_t>& ids,
                                    std::string& error) {
    ids.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT user_high AS friend_id FROM friendships WHERE user_low = ? "
              "UNION ALL "
              "SELECT user_low AS friend_id FROM friendships WHERE user_high = ? "
              "ORDER BY friend_id",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t low_value = user_id;
    uint64_t high_value = user_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], low_value);
    bindU64(params[1], high_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    if (!execute(stmt.get(), error)) {
        return false;
    }
    if (mysql_stmt_store_result(stmt.get()) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }

    uint64_t friend_id = 0;
    unsigned long friend_id_length = 0;
    bool friend_id_null = false;
    MYSQL_BIND result = {};
    result.buffer_type = MYSQL_TYPE_LONGLONG;
    result.buffer = &friend_id;
    result.buffer_length = sizeof(friend_id);
    result.is_unsigned = true;
    result.length = &friend_id_length;
    result.is_null = &friend_id_null;
    if (mysql_stmt_bind_result(stmt.get(), &result) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    while (true) {
        const int fetch_result = mysql_stmt_fetch(stmt.get());
        if (fetch_result == MYSQL_NO_DATA) {
            break;
        }
        if (fetch_result != 0 && fetch_result != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        if (!friend_id_null) {
            ids.push_back(friend_id);
        }
    }
    error.clear();
    return true;
}

}  // namespace chatbox
