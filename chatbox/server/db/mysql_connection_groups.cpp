#include "server/db/mysql_connection.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

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
        if (mysql_stmt_prepare(stmt_, sql,
                               static_cast<unsigned long>(std::strlen(sql))) != 0) {
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
    MYSQL_STMT* get() const { return stmt_; }

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
        return false;
    }
    return true;
}

bool beginTransaction(MYSQL* mysql, std::string& error) {
    if (mysql_query(mysql, "START TRANSACTION") != 0) {
        error = mysql_error(mysql);
        return false;
    }
    return true;
}

void rollback(MYSQL* mysql) {
    mysql_query(mysql, "ROLLBACK");
}

bool commit(MYSQL* mysql, std::string& error) {
    if (mysql_query(mysql, "COMMIT") != 0) {
        error = mysql_error(mysql);
        return false;
    }
    return true;
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

}  // namespace

bool MysqlConnection::listGroupsForUser(uint64_t user_id,
                                        std::vector<GroupInfo>& groups,
                                        std::string& error) {
    groups.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(
        mysql_,
        "SELECT r.id, r.name, r.group_no, r.owner_id, r.is_default, r.status, "
        "rm.role, "
        "(SELECT COUNT(*) FROM room_members x WHERE x.room_id = r.id), "
        "(SELECT COUNT(*) FROM messages m WHERE m.room_id = r.id "
        " AND m.sender_id <> rm.user_id AND m.id > "
        " GREATEST(rm.join_message_id, COALESCE(rr.last_read_message_id, 0))) "
        "FROM room_members rm "
        "JOIN rooms r ON r.id = rm.room_id "
        "LEFT JOIN room_read_state rr ON rr.room_id = rm.room_id "
        " AND rr.user_id = rm.user_id "
        "WHERE rm.user_id = ? "
        "ORDER BY r.is_default DESC, r.status ASC, r.id ASC",
        error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t user_value = user_id;
    MYSQL_BIND params[1] = {};
    bindU64(params[0], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) ||
        mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }

    int id = 0;
    int is_default = 0;
    int status = 0;
    int role = 0;
    int member_count = 0;
    int unread = 0;
    uint64_t owner_id = 0;
    char name_buffer[129] = {0};
    char group_no_buffer[13] = {0};
    unsigned long name_length = 0;
    unsigned long group_no_length = 0;
    bool id_null = false, owner_null = false, name_null = false;
    bool group_no_null = false, default_null = false, status_null = false;
    bool role_null = false, count_null = false, unread_null = false;
    MYSQL_BIND results[9] = {};
    results[0].buffer_type = MYSQL_TYPE_LONG; results[0].buffer = &id;
    results[1].buffer_type = MYSQL_TYPE_STRING; results[1].buffer = name_buffer;
    results[1].buffer_length = sizeof(name_buffer) - 1;
    results[1].length = &name_length; results[1].is_null = &name_null;
    results[2].buffer_type = MYSQL_TYPE_STRING; results[2].buffer = group_no_buffer;
    results[2].buffer_length = sizeof(group_no_buffer) - 1;
    results[2].length = &group_no_length; results[2].is_null = &group_no_null;
    results[3].buffer_type = MYSQL_TYPE_LONGLONG; results[3].buffer = &owner_id;
    results[3].is_unsigned = true; results[3].is_null = &owner_null;
    results[4].buffer_type = MYSQL_TYPE_LONG; results[4].buffer = &is_default;
    results[4].is_null = &default_null;
    results[5].buffer_type = MYSQL_TYPE_LONG; results[5].buffer = &status;
    results[5].is_null = &status_null;
    results[6].buffer_type = MYSQL_TYPE_LONG; results[6].buffer = &role;
    results[6].is_null = &role_null;
    results[7].buffer_type = MYSQL_TYPE_LONG; results[7].buffer = &member_count;
    results[7].is_null = &count_null;
    results[8].buffer_type = MYSQL_TYPE_LONG; results[8].buffer = &unread;
    results[8].is_null = &unread_null;
    (void)id_null;
    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    while (true) {
        const int fetched = mysql_stmt_fetch(stmt.get());
        if (fetched == MYSQL_NO_DATA) {
            break;
        }
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        GroupInfo group;
        group.room_id = static_cast<uint64_t>(id);
        group.name = stringResult(name_buffer, name_length, name_null, 128);
        group.group_no =
            stringResult(group_no_buffer, group_no_length, group_no_null, 12);
        group.owner_id = owner_null ? 0 : owner_id;
        group.is_default = !default_null && is_default != 0;
        group.status = status_null ? GroupStatus::kActive
                                   : static_cast<GroupStatus>(status);
        group.role = role_null ? GroupRole::kMember : static_cast<GroupRole>(role);
        group.member_count = count_null ? 0 : static_cast<uint32_t>(member_count);
        group.unread = unread_null ? 0 : static_cast<uint32_t>(unread);
        groups.push_back(std::move(group));
    }
    error.clear();
    return true;
}

bool MysqlConnection::findGroupByNumber(const std::string& group_no,
                                        GroupInfo& group, bool& found,
                                        std::string& error) {
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(
        mysql_,
        "SELECT r.id, r.name, r.group_no, r.owner_id, r.max_members, "
        "r.is_default, r.status, "
        "(SELECT COUNT(*) FROM room_members x WHERE x.room_id = r.id) "
        "FROM rooms r WHERE r.group_no = ? LIMIT 1",
        error);
    if (stmt.get() == nullptr) {
        return false;
    }
    unsigned long group_length = 0;
    MYSQL_BIND params[1] = {};
    bindString(params[0], group_no, group_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    int id = 0, max_members = 0, is_default = 0, status = 0, member_count = 0;
    uint64_t owner_id = 0;
    char name_buffer[129] = {0};
    char group_buffer[13] = {0};
    unsigned long name_length = 0, group_no_length = 0;
    bool id_null = false, name_null = false, group_null = false;
    bool owner_null = false, max_null = false, default_null = false;
    bool status_null = false, count_null = false;
    MYSQL_BIND results[8] = {};
    results[0].buffer_type = MYSQL_TYPE_LONG; results[0].buffer = &id;
    results[1].buffer_type = MYSQL_TYPE_STRING; results[1].buffer = name_buffer;
    results[1].buffer_length = sizeof(name_buffer) - 1;
    results[1].length = &name_length; results[1].is_null = &name_null;
    results[2].buffer_type = MYSQL_TYPE_STRING; results[2].buffer = group_buffer;
    results[2].buffer_length = sizeof(group_buffer) - 1;
    results[2].length = &group_no_length; results[2].is_null = &group_null;
    results[3].buffer_type = MYSQL_TYPE_LONGLONG; results[3].buffer = &owner_id;
    results[3].is_unsigned = true; results[3].is_null = &owner_null;
    results[4].buffer_type = MYSQL_TYPE_LONG; results[4].buffer = &max_members;
    results[4].is_null = &max_null;
    results[5].buffer_type = MYSQL_TYPE_LONG; results[5].buffer = &is_default;
    results[5].is_null = &default_null;
    results[6].buffer_type = MYSQL_TYPE_LONG; results[6].buffer = &status;
    results[6].is_null = &status_null;
    results[7].buffer_type = MYSQL_TYPE_LONG; results[7].buffer = &member_count;
    results[7].is_null = &count_null;
    (void)id_null;
    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    group.room_id = static_cast<uint64_t>(id);
    group.name = stringResult(name_buffer, name_length, name_null, 128);
    group.group_no = stringResult(group_buffer, group_no_length, group_null, 12);
    group.owner_id = owner_null ? 0 : owner_id;
    group.member_count = count_null ? 0 : static_cast<uint32_t>(member_count);
    group.is_default = !default_null && is_default != 0;
    group.status = status_null ? GroupStatus::kActive
                               : static_cast<GroupStatus>(status);
    found = true;
    error.clear();
    return true;
}

bool MysqlConnection::findGroupById(uint64_t room_id, GroupInfo& group,
                                    bool& found, std::string& error) {
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT r.id, r.name, r.group_no, r.owner_id, r.max_members, "
              "r.is_default, r.status, "
              "(SELECT COUNT(*) FROM room_members x WHERE x.room_id = r.id) "
              "FROM rooms r WHERE r.id = ? LIMIT 1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    MYSQL_BIND params[1] = {};
    bindU64(params[0], room_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    int id = 0, max_members = 0, is_default = 0, status = 0, member_count = 0;
    uint64_t owner_id = 0;
    char name_buffer[129] = {0};
    char group_buffer[13] = {0};
    unsigned long name_length = 0, group_no_length = 0;
    bool name_null = false, group_null = false, owner_null = false;
    bool max_null = false, default_null = false, status_null = false;
    bool count_null = false;
    MYSQL_BIND results[8] = {};
    results[0].buffer_type = MYSQL_TYPE_LONG; results[0].buffer = &id;
    results[1].buffer_type = MYSQL_TYPE_STRING; results[1].buffer = name_buffer;
    results[1].buffer_length = sizeof(name_buffer) - 1;
    results[1].length = &name_length; results[1].is_null = &name_null;
    results[2].buffer_type = MYSQL_TYPE_STRING; results[2].buffer = group_buffer;
    results[2].buffer_length = sizeof(group_buffer) - 1;
    results[2].length = &group_no_length; results[2].is_null = &group_null;
    results[3].buffer_type = MYSQL_TYPE_LONGLONG; results[3].buffer = &owner_id;
    results[3].is_unsigned = true; results[3].is_null = &owner_null;
    results[4].buffer_type = MYSQL_TYPE_LONG; results[4].buffer = &max_members;
    results[4].is_null = &max_null;
    results[5].buffer_type = MYSQL_TYPE_LONG; results[5].buffer = &is_default;
    results[5].is_null = &default_null;
    results[6].buffer_type = MYSQL_TYPE_LONG; results[6].buffer = &status;
    results[6].is_null = &status_null;
    results[7].buffer_type = MYSQL_TYPE_LONG; results[7].buffer = &member_count;
    results[7].is_null = &count_null;
    (void)max_null;
    if (mysql_stmt_bind_result(stmt.get(), results) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    group.room_id = static_cast<uint64_t>(id);
    group.name = stringResult(name_buffer, name_length, name_null, 128);
    group.group_no = stringResult(group_buffer, group_no_length, group_null, 12);
    group.owner_id = owner_null ? 0 : owner_id;
    group.member_count = count_null ? 0 : static_cast<uint32_t>(member_count);
    group.is_default = !default_null && is_default != 0;
    group.status = status_null ? GroupStatus::kActive
                               : static_cast<GroupStatus>(status);
    found = true;
    error.clear();
    return true;
}

bool MysqlConnection::maxRoomMessageId(uint64_t room_id, uint64_t& message_id,
                                       std::string& error) {
    message_id = 0;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT COALESCE(MAX(id), 0) FROM messages WHERE room_id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    MYSQL_BIND params[1] = {};
    bindU64(params[0], room_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    uint64_t result = 0;
    bool null = false;
    MYSQL_BIND out[1] = {};
    out[0].buffer_type = MYSQL_TYPE_LONGLONG;
    out[0].buffer = &result;
    out[0].is_unsigned = true;
    out[0].is_null = &null;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    message_id = null ? 0 : result;
    error.clear();
    return true;
}

bool MysqlConnection::createGroup(const std::string& name, uint64_t owner_id,
                                  GroupInfo& group, std::string& error) {
    group = GroupInfo{};
    if (!ensureConnected(error)) {
        return false;
    }
    if (!beginTransaction(mysql_, error)) {
        return false;
    }
    auto fail = [this]() {
        rollback(mysql_);
        return false;
    };

    uint64_t next_number = 0;
    {
        Stmt seq(mysql_, "SELECT COALESCE(MAX(id), 0) + 10000001 FROM rooms", error);
        if (seq.get() == nullptr || !execute(seq.get(), error) ||
            mysql_stmt_store_result(seq.get()) != 0) {
            if (error.empty()) {
                error = mysql_stmt_error(seq.get());
            }
            return fail();
        }
        bool null = false;
        MYSQL_BIND out[1] = {};
        out[0].buffer_type = MYSQL_TYPE_LONGLONG;
        out[0].buffer = &next_number;
        out[0].is_unsigned = true;
        out[0].is_null = &null;
        if (mysql_stmt_bind_result(seq.get(), out) != 0) {
            error = mysql_stmt_error(seq.get());
            return fail();
        }
        const int fetched = mysql_stmt_fetch(seq.get());
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(seq.get());
            return fail();
        }
        if (null || next_number == 0) {
            error = "could not allocate group number";
            return fail();
        }
    }

    Stmt insert(mysql_,
                "INSERT INTO rooms "
                "(name, max_members, group_no, owner_id, is_default, status) "
                "VALUES (?, 500, ?, ?, 0, 0)",
                error);
    if (insert.get() == nullptr) {
        return fail();
    }
    unsigned long name_length = 0;
    unsigned long group_no_length = 0;
    const std::string group_no = std::to_string(next_number);
    uint64_t owner_value = owner_id;
    MYSQL_BIND params[3] = {};
    bindString(params[0], name, name_length);
    bindString(params[1], group_no, group_no_length);
    bindU64(params[2], owner_value);
    if (mysql_stmt_bind_param(insert.get(), params) != 0 ||
        !execute(insert.get(), error)) {
        return fail();
    }
    const uint64_t room_id = mysql_stmt_insert_id(insert.get());
    if (room_id == 0) {
        error = "insert group returned zero id";
        return fail();
    }

    if (!addRoomMember(room_id, owner_id, GroupRole::kOwner, error)) {
        return fail();
    }
    if (!commit(mysql_, error)) {
        return fail();
    }

    group.room_id = room_id;
    group.name = name;
    group.group_no = group_no;
    group.owner_id = owner_id;
    group.member_count = 1;
    group.role = GroupRole::kOwner;
    group.is_default = false;
    group.status = GroupStatus::kActive;
    error.clear();
    return true;
}

bool MysqlConnection::updateGroupName(uint64_t room_id, const std::string& name,
                                      std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "UPDATE rooms SET name = ? WHERE id = ? AND status = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    unsigned long name_length = 0;
    uint64_t room_value = room_id;
    MYSQL_BIND params[2] = {};
    bindString(params[0], name, name_length);
    bindU64(params[1], room_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::disbandGroup(uint64_t room_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (!beginTransaction(mysql_, error)) {
        return false;
    }
    auto fail = [this]() {
        rollback(mysql_);
        return false;
    };
    {
        Stmt room(mysql_,
                  "UPDATE rooms SET status = 1, dissolved_at = NOW(3) "
                  "WHERE id = ? AND status = 0",
                  error);
        if (room.get() == nullptr) {
            return fail();
        }
        uint64_t room_value = room_id;
        MYSQL_BIND params[1] = {};
        bindU64(params[0], room_value);
        if (mysql_stmt_bind_param(room.get(), params) != 0 ||
            !execute(room.get(), error)) {
            return fail();
        }
    }
    const char* deletes[] = {
        "DELETE FROM messages WHERE room_id = ?",
        "DELETE FROM room_join_requests WHERE room_id = ?",
        "DELETE FROM room_invites WHERE room_id = ?",
    };
    for (const char* sql : deletes) {
        Stmt stmt(mysql_, sql, error);
        if (stmt.get() == nullptr) {
            return fail();
        }
        uint64_t room_value = room_id;
        MYSQL_BIND params[1] = {};
        bindU64(params[0], room_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
            !execute(stmt.get(), error)) {
            return fail();
        }
    }
    return commit(mysql_, error);
}

bool MysqlConnection::addRoomMember(uint64_t room_id, uint64_t user_id,
                                    GroupRole role, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    int max_members = 0;
    {
        Stmt lock(mysql_,
                  "SELECT max_members FROM rooms "
                  "WHERE id = ? AND status = 0 FOR UPDATE",
                  error);
        if (lock.get() == nullptr) {
            return false;
        }
        uint64_t room_value = room_id;
        MYSQL_BIND params[1] = {};
        bindU64(params[0], room_value);
        if (mysql_stmt_bind_param(lock.get(), params) != 0 ||
            !execute(lock.get(), error) ||
            mysql_stmt_store_result(lock.get()) != 0) {
            if (error.empty()) {
                error = mysql_stmt_error(lock.get());
            }
            return false;
        }
        bool null = false;
        MYSQL_BIND out[1] = {};
        out[0].buffer_type = MYSQL_TYPE_LONG;
        out[0].buffer = &max_members;
        out[0].is_null = &null;
        if (mysql_stmt_bind_result(lock.get(), out) != 0) {
            error = mysql_stmt_error(lock.get());
            return false;
        }
        const int fetched = mysql_stmt_fetch(lock.get());
        if (fetched == MYSQL_NO_DATA) {
            error = "group not found";
            return false;
        }
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(lock.get());
            return false;
        }
        if (null) {
            max_members = 0;
        }
    }
    if (max_members > 0) {
        Stmt count_stmt(mysql_,
                        "SELECT COUNT(*) FROM room_members WHERE room_id = ?",
                        error);
        if (count_stmt.get() == nullptr) {
            return false;
        }
        uint64_t room_value = room_id;
        MYSQL_BIND params[1] = {};
        bindU64(params[0], room_value);
        if (mysql_stmt_bind_param(count_stmt.get(), params) != 0 ||
            !execute(count_stmt.get(), error) ||
            mysql_stmt_store_result(count_stmt.get()) != 0) {
            if (error.empty()) {
                error = mysql_stmt_error(count_stmt.get());
            }
            return false;
        }
        int member_count = 0;
        bool null = false;
        MYSQL_BIND out[1] = {};
        out[0].buffer_type = MYSQL_TYPE_LONG;
        out[0].buffer = &member_count;
        out[0].is_null = &null;
        if (mysql_stmt_bind_result(count_stmt.get(), out) != 0) {
            error = mysql_stmt_error(count_stmt.get());
            return false;
        }
        const int fetched = mysql_stmt_fetch(count_stmt.get());
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(count_stmt.get());
            return false;
        }
        if (!null && member_count >= max_members) {
            error = "group is full";
            return false;
        }
    }
    uint64_t cursor = 0;
    if (!maxRoomMessageId(room_id, cursor, error)) {
        return false;
    }
    Stmt insert(
        mysql_,
        "INSERT INTO room_members "
        "(room_id, user_id, role, joined_at, join_message_id) "
        "VALUES (?, ?, ?, NOW(3), ?)",
        error);
    if (insert.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    uint64_t user_value = user_id;
    uint64_t cursor_value = cursor;
    int role_value = static_cast<int>(role);
    MYSQL_BIND params[4] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], user_value);
    bindInt(params[2], role_value);
    bindU64(params[3], cursor_value);
    if (mysql_stmt_bind_param(insert.get(), params) != 0 ||
        !execute(insert.get(), error)) {
        return false;
    }

    Stmt read(mysql_,
              "INSERT INTO room_read_state "
              "(room_id, user_id, last_read_message_id) VALUES (?, ?, ?) "
              "ON DUPLICATE KEY UPDATE last_read_message_id = VALUES(last_read_message_id)",
              error);
    if (read.get() == nullptr) {
        return false;
    }
    uint64_t read_room = room_id;
    uint64_t read_user = user_id;
    uint64_t read_message = cursor;
    MYSQL_BIND read_params[3] = {};
    bindU64(read_params[0], read_room);
    bindU64(read_params[1], read_user);
    bindU64(read_params[2], read_message);
    if (mysql_stmt_bind_param(read.get(), read_params) != 0) {
        error = mysql_stmt_error(read.get());
        return false;
    }
    return execute(read.get(), error);
}

bool MysqlConnection::removeRoomMember(uint64_t room_id, uint64_t user_id,
                                       std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (!beginTransaction(mysql_, error)) {
        return false;
    }
    auto fail = [this]() {
        rollback(mysql_);
        return false;
    };
    Stmt stmt(mysql_, "DELETE FROM room_members WHERE room_id = ? AND user_id = ?",
              error);
    if (stmt.get() == nullptr) {
        return fail();
    }
    uint64_t room_value = room_id;
    uint64_t user_value = user_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error)) {
        return fail();
    }
    if (!cancelPendingMembershipRequests(room_id, user_id, error)) {
        return fail();
    }
    return commit(mysql_, error);
}

bool MysqlConnection::cancelPendingMembershipRequests(uint64_t room_id,
                                                      uint64_t user_id,
                                                      std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    const char* statements[] = {
        "DELETE FROM room_join_requests "
        "WHERE room_id = ? AND user_id = ? AND status = 0",
        "DELETE FROM room_invites "
        "WHERE room_id = ? AND invitee_id = ? AND status = 0",
    };
    for (const char* sql : statements) {
        Stmt stmt(mysql_, sql, error);
        if (stmt.get() == nullptr) {
            return false;
        }
        uint64_t room_value = room_id;
        uint64_t user_value = user_id;
        MYSQL_BIND params[2] = {};
        bindU64(params[0], room_value);
        bindU64(params[1], user_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
            !execute(stmt.get(), error)) {
            return false;
        }
    }
    error.clear();
    return true;
}

bool MysqlConnection::listRoomMembers(uint64_t room_id,
                                      std::vector<GroupMemberInfo>& members,
                                      std::string& error) {
    members.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT rm.user_id, u.nickname, rm.role "
              "FROM room_members rm JOIN users u ON u.id = rm.user_id "
              "WHERE rm.room_id = ? ORDER BY rm.role DESC, rm.user_id ASC",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    MYSQL_BIND params[1] = {};
    bindU64(params[0], room_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    uint64_t user_id = 0;
    int role = 0;
    char nickname[129] = {0};
    unsigned long nickname_length = 0;
    bool user_null = false, role_null = false, nickname_null = false;
    MYSQL_BIND out[3] = {};
    out[0].buffer_type = MYSQL_TYPE_LONGLONG;
    out[0].buffer = &user_id;
    out[0].is_unsigned = true;
    out[0].is_null = &user_null;
    out[1].buffer_type = MYSQL_TYPE_STRING;
    out[1].buffer = nickname;
    out[1].buffer_length = sizeof(nickname) - 1;
    out[1].length = &nickname_length;
    out[1].is_null = &nickname_null;
    out[2].buffer_type = MYSQL_TYPE_LONG;
    out[2].buffer = &role;
    out[2].is_null = &role_null;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    while (true) {
        const int fetched = mysql_stmt_fetch(stmt.get());
        if (fetched == MYSQL_NO_DATA) {
            break;
        }
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        GroupMemberInfo member;
        member.user_id = user_null ? 0 : user_id;
        member.nickname =
            stringResult(nickname, nickname_length, nickname_null, 128);
        member.role = role_null ? GroupRole::kMember
                                : static_cast<GroupRole>(role);
        members.push_back(std::move(member));
    }
    error.clear();
    return true;
}

bool MysqlConnection::getRoomMemberRole(uint64_t room_id, uint64_t user_id,
                                        GroupRole& role, bool& found,
                                        std::string& error) {
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "SELECT role FROM room_members WHERE room_id = ? AND user_id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    uint64_t user_value = user_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    int role_value = 0;
    bool null = false;
    MYSQL_BIND out[1] = {};
    out[0].buffer_type = MYSQL_TYPE_LONG;
    out[0].buffer = &role_value;
    out[0].is_null = &null;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    role = null ? GroupRole::kMember : static_cast<GroupRole>(role_value);
    found = true;
    error.clear();
    return true;
}

bool MysqlConnection::setRoomMemberRole(uint64_t room_id, uint64_t user_id,
                                        GroupRole role, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "UPDATE room_members SET role = ? WHERE room_id = ? AND user_id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int role_value = static_cast<int>(role);
    uint64_t room_value = room_id;
    uint64_t user_value = user_id;
    MYSQL_BIND params[3] = {};
    bindInt(params[0], role_value);
    bindU64(params[1], room_value);
    bindU64(params[2], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::transferRoomOwner(uint64_t room_id, uint64_t new_owner,
                                        std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (!beginTransaction(mysql_, error)) {
        return false;
    }
    auto fail = [this]() {
        rollback(mysql_);
        return false;
    };
    {
        Stmt stmt(mysql_, "UPDATE rooms SET owner_id = ? WHERE id = ? AND status = 0",
                  error);
        if (stmt.get() == nullptr) {
            return fail();
        }
        uint64_t owner_value = new_owner;
        uint64_t room_value = room_id;
        MYSQL_BIND params[2] = {};
        bindU64(params[0], owner_value);
        bindU64(params[1], room_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
            !execute(stmt.get(), error)) {
            return fail();
        }
    }
    {
        Stmt stmt(mysql_, "UPDATE room_members SET role = 0 WHERE room_id = ? AND role = 2",
                  error);
        if (stmt.get() == nullptr) {
            return fail();
        }
        uint64_t room_value = room_id;
        MYSQL_BIND params[1] = {};
        bindU64(params[0], room_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
            !execute(stmt.get(), error)) {
            return fail();
        }
    }
    {
        Stmt stmt(mysql_,
                  "UPDATE room_members SET role = 2 WHERE room_id = ? AND user_id = ?",
                  error);
        if (stmt.get() == nullptr) {
            return fail();
        }
        uint64_t room_value = room_id;
        uint64_t user_value = new_owner;
        MYSQL_BIND params[2] = {};
        bindU64(params[0], room_value);
        bindU64(params[1], user_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
            !execute(stmt.get(), error)) {
            return fail();
        }
        if (mysql_stmt_affected_rows(stmt.get()) == 0) {
            error = "new owner is not a member";
            return fail();
        }
    }
    return commit(mysql_, error);
}

bool MysqlConnection::hasPendingJoinRequest(uint64_t room_id, uint64_t user_id,
                                            bool& pending, std::string& error) {
    pending = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT COUNT(*) FROM room_join_requests "
              "WHERE room_id = ? AND user_id = ? AND status = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    uint64_t user_value = user_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    int count = 0;
    bool null = false;
    MYSQL_BIND out[1] = {};
    out[0].buffer_type = MYSQL_TYPE_LONG;
    out[0].buffer = &count;
    out[0].is_null = &null;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    pending = !null && count > 0;
    error.clear();
    return true;
}

bool MysqlConnection::findJoinRequest(uint64_t request_id, uint64_t& room_id,
                                      uint64_t& user_id, int& status,
                                      bool& found, std::string& error) {
    room_id = 0;
    user_id = 0;
    status = 0;
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT room_id, user_id, status FROM room_join_requests WHERE id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t request_value = request_id;
    MYSQL_BIND params[1] = {};
    bindU64(params[0], request_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    MYSQL_BIND out[3] = {};
    out[0].buffer_type = MYSQL_TYPE_LONGLONG;
    out[0].buffer = &room_id;
    out[0].is_unsigned = true;
    out[1].buffer_type = MYSQL_TYPE_LONGLONG;
    out[1].buffer = &user_id;
    out[1].is_unsigned = true;
    out[2].buffer_type = MYSQL_TYPE_LONG;
    out[2].buffer = &status;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    found = true;
    error.clear();
    return true;
}

bool MysqlConnection::createJoinRequest(uint64_t room_id, uint64_t user_id,
                                        const std::string& message,
                                        uint64_t& request_id,
                                        std::string& error) {
    request_id = 0;
    bool pending = false;
    if (!hasPendingJoinRequest(room_id, user_id, pending, error)) {
        return false;
    }
    if (pending) {
        error = "join request already pending";
        return false;
    }
    Stmt stmt(mysql_,
              "INSERT INTO room_join_requests (room_id, user_id, message) "
              "VALUES (?, ?, ?)",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    uint64_t user_value = user_id;
    unsigned long message_length = 0;
    MYSQL_BIND params[3] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], user_value);
    bindString(params[2], message, message_length);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error)) {
        return false;
    }
    request_id = mysql_stmt_insert_id(stmt.get());
    if (request_id == 0) {
        error = "insert join request returned zero id";
        return false;
    }
    return true;
}

bool MysqlConnection::listJoinRequests(uint64_t room_id,
                                       std::vector<GroupRequestInfo>& requests,
                                       std::string& error) {
    requests.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(
        mysql_,
        "SELECT jr.id, jr.user_id, u.nickname, jr.message, jr.status, "
        "DATE_FORMAT(jr.created_at, '%Y-%m-%d %H:%i:%s') "
        "FROM room_join_requests jr JOIN users u ON u.id = jr.user_id "
        "WHERE jr.room_id = ? AND jr.status = 0 ORDER BY jr.id ASC LIMIT 50",
        error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    MYSQL_BIND params[1] = {};
    bindU64(params[0], room_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    uint64_t request_id = 0, user_id = 0;
    int status = 0;
    char nickname[129] = {0};
    char message[801] = {0};
    char created[32] = {0};
    unsigned long nickname_length = 0, message_length = 0, created_length = 0;
    bool request_null = false, user_null = false, nickname_null = false;
    bool message_null = false, status_null = false, created_null = false;
    MYSQL_BIND out[6] = {};
    out[0].buffer_type = MYSQL_TYPE_LONGLONG;
    out[0].buffer = &request_id;
    out[0].is_unsigned = true;
    out[0].is_null = &request_null;
    out[1].buffer_type = MYSQL_TYPE_LONGLONG;
    out[1].buffer = &user_id;
    out[1].is_unsigned = true;
    out[1].is_null = &user_null;
    out[2].buffer_type = MYSQL_TYPE_STRING;
    out[2].buffer = nickname;
    out[2].buffer_length = sizeof(nickname) - 1;
    out[2].length = &nickname_length;
    out[2].is_null = &nickname_null;
    out[3].buffer_type = MYSQL_TYPE_STRING;
    out[3].buffer = message;
    out[3].buffer_length = sizeof(message) - 1;
    out[3].length = &message_length;
    out[3].is_null = &message_null;
    out[4].buffer_type = MYSQL_TYPE_LONG;
    out[4].buffer = &status;
    out[4].is_null = &status_null;
    out[5].buffer_type = MYSQL_TYPE_STRING;
    out[5].buffer = created;
    out[5].buffer_length = sizeof(created) - 1;
    out[5].length = &created_length;
    out[5].is_null = &created_null;
    (void)request_null;
    (void)user_null;
    (void)status_null;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    while (true) {
        const int fetched = mysql_stmt_fetch(stmt.get());
        if (fetched == MYSQL_NO_DATA) {
            break;
        }
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        GroupRequestInfo request;
        request.request_id = request_id;
        request.room_id = room_id;
        request.user_id = user_id;
        request.nickname = stringResult(nickname, nickname_length, nickname_null, 128);
        request.message = stringResult(message, message_length, message_null, 800);
        request.status = status_null ? 0 : status;
        request.created_at =
            stringResult(created, created_length, created_null, 31);
        requests.push_back(std::move(request));
    }
    error.clear();
    return true;
}

bool MysqlConnection::respondJoinRequest(uint64_t request_id,
                                         uint64_t reviewer_id, bool accept,
                                         std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (!beginTransaction(mysql_, error)) {
        return false;
    }
    auto fail = [this]() {
        rollback(mysql_);
        return false;
    };

    uint64_t room_id = 0, user_id = 0;
    int status = 0;
    {
        Stmt stmt(mysql_,
                  "SELECT room_id, user_id, status FROM room_join_requests "
                  "WHERE id = ? FOR UPDATE",
                  error);
        if (stmt.get() == nullptr) {
            return fail();
        }
        uint64_t request_value = request_id;
        MYSQL_BIND params[1] = {};
        bindU64(params[0], request_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
            !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
            if (error.empty()) {
                error = mysql_stmt_error(stmt.get());
            }
            return fail();
        }
        MYSQL_BIND out[3] = {};
        out[0].buffer_type = MYSQL_TYPE_LONGLONG;
        out[0].buffer = &room_id;
        out[0].is_unsigned = true;
        out[1].buffer_type = MYSQL_TYPE_LONGLONG;
        out[1].buffer = &user_id;
        out[1].is_unsigned = true;
        out[2].buffer_type = MYSQL_TYPE_LONG;
        out[2].buffer = &status;
        if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
            error = mysql_stmt_error(stmt.get());
            return fail();
        }
        const int fetched = mysql_stmt_fetch(stmt.get());
        if (fetched == MYSQL_NO_DATA) {
            error = "join request not found";
            return fail();
        }
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return fail();
        }
        if (status != 0) {
            error = "join request already handled";
            return fail();
        }
    }

    {
        Stmt stmt(mysql_,
                  "UPDATE room_join_requests SET status = ?, reviewed_by = ?, "
                  "reviewed_at = NOW(3) WHERE id = ?",
                  error);
        if (stmt.get() == nullptr) {
            return fail();
        }
        int status_value = accept ? 1 : 2;
        uint64_t reviewer_value = reviewer_id;
        uint64_t request_value = request_id;
        MYSQL_BIND params[3] = {};
        bindInt(params[0], status_value);
        bindU64(params[1], reviewer_value);
        bindU64(params[2], request_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
            !execute(stmt.get(), error)) {
            return fail();
        }
    }
    if (accept) {
        if (!addRoomMember(room_id, user_id, GroupRole::kMember, error) ||
            !cancelPendingMembershipRequests(room_id, user_id, error)) {
            return fail();
        }
    }
    return commit(mysql_, error);
}

bool MysqlConnection::hasPendingInvite(uint64_t room_id, uint64_t invitee_id,
                                       bool& pending, std::string& error) {
    pending = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT COUNT(*) FROM room_invites "
              "WHERE room_id = ? AND invitee_id = ? AND status = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    uint64_t user_value = invitee_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    int count = 0;
    bool null = false;
    MYSQL_BIND out[1] = {};
    out[0].buffer_type = MYSQL_TYPE_LONG;
    out[0].buffer = &count;
    out[0].is_null = &null;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    pending = !null && count > 0;
    error.clear();
    return true;
}

bool MysqlConnection::findInvite(uint64_t invite_id, uint64_t& room_id,
                                 uint64_t& invitee_id, int& status, bool& found,
                                 std::string& error) {
    room_id = 0;
    invitee_id = 0;
    status = 0;
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT room_id, invitee_id, status FROM room_invites WHERE id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t invite_value = invite_id;
    MYSQL_BIND params[1] = {};
    bindU64(params[0], invite_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    MYSQL_BIND out[3] = {};
    out[0].buffer_type = MYSQL_TYPE_LONGLONG;
    out[0].buffer = &room_id;
    out[0].is_unsigned = true;
    out[1].buffer_type = MYSQL_TYPE_LONGLONG;
    out[1].buffer = &invitee_id;
    out[1].is_unsigned = true;
    out[2].buffer_type = MYSQL_TYPE_LONG;
    out[2].buffer = &status;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    found = true;
    error.clear();
    return true;
}

bool MysqlConnection::createInvite(uint64_t room_id, uint64_t inviter_id,
                                   uint64_t invitee_id, uint64_t& invite_id,
                                   std::string& error) {
    invite_id = 0;
    bool pending = false;
    if (!hasPendingInvite(room_id, invitee_id, pending, error)) {
        return false;
    }
    if (pending) {
        error = "invite already pending";
        return false;
    }
    Stmt stmt(mysql_,
              "INSERT INTO room_invites (room_id, inviter_id, invitee_id) "
              "VALUES (?, ?, ?)",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    uint64_t inviter_value = inviter_id;
    uint64_t invitee_value = invitee_id;
    MYSQL_BIND params[3] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], inviter_value);
    bindU64(params[2], invitee_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error)) {
        return false;
    }
    invite_id = mysql_stmt_insert_id(stmt.get());
    if (invite_id == 0) {
        error = "insert invite returned zero id";
        return false;
    }
    return true;
}

bool MysqlConnection::respondInvite(uint64_t invite_id, uint64_t invitee_id,
                                    bool accept, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    if (!beginTransaction(mysql_, error)) {
        return false;
    }
    auto fail = [this]() {
        rollback(mysql_);
        return false;
    };
    uint64_t room_id = 0, stored_invitee = 0;
    int status = 0;
    {
        Stmt stmt(mysql_,
                  "SELECT room_id, invitee_id, status FROM room_invites "
                  "WHERE id = ? FOR UPDATE",
                  error);
        if (stmt.get() == nullptr) {
            return fail();
        }
        uint64_t invite_value = invite_id;
        MYSQL_BIND params[1] = {};
        bindU64(params[0], invite_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
            !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
            if (error.empty()) {
                error = mysql_stmt_error(stmt.get());
            }
            return fail();
        }
        MYSQL_BIND out[3] = {};
        out[0].buffer_type = MYSQL_TYPE_LONGLONG;
        out[0].buffer = &room_id;
        out[0].is_unsigned = true;
        out[1].buffer_type = MYSQL_TYPE_LONGLONG;
        out[1].buffer = &stored_invitee;
        out[1].is_unsigned = true;
        out[2].buffer_type = MYSQL_TYPE_LONG;
        out[2].buffer = &status;
        if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
            error = mysql_stmt_error(stmt.get());
            return fail();
        }
        const int fetched = mysql_stmt_fetch(stmt.get());
        if (fetched == MYSQL_NO_DATA) {
            error = "invite not found";
            return fail();
        }
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return fail();
        }
        if (stored_invitee != invitee_id) {
            error = "invite does not belong to this user";
            return fail();
        }
        if (status != 0) {
            error = "invite already handled";
            return fail();
        }
    }
    {
        Stmt stmt(mysql_,
                  "UPDATE room_invites SET status = ?, responded_at = NOW(3) "
                  "WHERE id = ?",
                  error);
        if (stmt.get() == nullptr) {
            return fail();
        }
        int status_value = accept ? 1 : 2;
        uint64_t invite_value = invite_id;
        MYSQL_BIND params[2] = {};
        bindInt(params[0], status_value);
        bindU64(params[1], invite_value);
        if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
            !execute(stmt.get(), error)) {
            return fail();
        }
    }
    if (accept) {
        if (!addRoomMember(room_id, invitee_id, GroupRole::kMember, error) ||
            !cancelPendingMembershipRequests(room_id, invitee_id, error)) {
            return fail();
        }
    }
    return commit(mysql_, error);
}

bool MysqlConnection::listInvitesForUser(
    uint64_t user_id, std::vector<GroupRequestInfo>& invites,
    std::string& error) {
    invites.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(
        mysql_,
        "SELECT i.id, i.room_id, i.inviter_id, u.nickname, r.name, i.status, "
        "DATE_FORMAT(i.created_at, '%Y-%m-%d %H:%i:%s') "
        "FROM room_invites i "
        "JOIN users u ON u.id = i.inviter_id "
        "JOIN rooms r ON r.id = i.room_id "
        "WHERE i.invitee_id = ? AND i.status = 0 ORDER BY i.id ASC LIMIT 50",
        error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t user_value = user_id;
    MYSQL_BIND params[1] = {};
    bindU64(params[0], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    uint64_t invite_id = 0, room_id = 0, inviter_id = 0;
    int status = 0;
    char nickname[129] = {0}, room_name[129] = {0}, created[32] = {0};
    unsigned long nickname_length = 0, room_length = 0, created_length = 0;
    bool nickname_null = false, room_null = false, created_null = false;
    bool invite_null = false, room_id_null = false, inviter_null = false;
    bool status_null = false;
    MYSQL_BIND out[7] = {};
    out[0].buffer_type = MYSQL_TYPE_LONGLONG; out[0].buffer = &invite_id;
    out[0].is_unsigned = true; out[0].is_null = &invite_null;
    out[1].buffer_type = MYSQL_TYPE_LONGLONG; out[1].buffer = &room_id;
    out[1].is_unsigned = true; out[1].is_null = &room_id_null;
    out[2].buffer_type = MYSQL_TYPE_LONGLONG; out[2].buffer = &inviter_id;
    out[2].is_unsigned = true; out[2].is_null = &inviter_null;
    out[3].buffer_type = MYSQL_TYPE_STRING; out[3].buffer = nickname;
    out[3].buffer_length = sizeof(nickname) - 1;
    out[3].length = &nickname_length; out[3].is_null = &nickname_null;
    out[4].buffer_type = MYSQL_TYPE_STRING; out[4].buffer = room_name;
    out[4].buffer_length = sizeof(room_name) - 1;
    out[4].length = &room_length; out[4].is_null = &room_null;
    out[5].buffer_type = MYSQL_TYPE_LONG; out[5].buffer = &status;
    out[5].is_null = &status_null;
    out[6].buffer_type = MYSQL_TYPE_STRING; out[6].buffer = created;
    out[6].buffer_length = sizeof(created) - 1;
    out[6].length = &created_length; out[6].is_null = &created_null;
    (void)invite_null; (void)room_id_null; (void)inviter_null; (void)status_null;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    while (true) {
        const int fetched = mysql_stmt_fetch(stmt.get());
        if (fetched == MYSQL_NO_DATA) {
            break;
        }
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return false;
        }
        GroupRequestInfo invite;
        invite.request_id = invite_id;
        invite.room_id = room_id;
        invite.user_id = inviter_id;
        invite.nickname = stringResult(nickname, nickname_length, nickname_null, 128);
        invite.room_name = stringResult(room_name, room_length, room_null, 128);
        invite.status = status_null ? 0 : status;
        invite.created_at = stringResult(created, created_length, created_null, 31);
        invites.push_back(std::move(invite));
    }
    error.clear();
    return true;
}

bool MysqlConnection::markRoomRead(uint64_t room_id, uint64_t user_id,
                                   uint64_t message_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "INSERT INTO room_read_state "
              "(room_id, user_id, last_read_message_id) VALUES (?, ?, ?) "
              "ON DUPLICATE KEY UPDATE "
              "last_read_message_id = GREATEST(last_read_message_id, VALUES(last_read_message_id))",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    uint64_t user_value = user_id;
    uint64_t message_value = message_id;
    MYSQL_BIND params[3] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], user_value);
    bindU64(params[2], message_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::unreadCount(uint64_t room_id, uint64_t user_id,
                                  uint32_t& unread, std::string& error) {
    unread = 0;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(
        mysql_,
        "SELECT COUNT(*) FROM messages m "
        "JOIN room_members rm ON rm.room_id = m.room_id AND rm.user_id = ? "
        "LEFT JOIN room_read_state rr ON rr.room_id = rm.room_id "
        " AND rr.user_id = rm.user_id "
        "WHERE m.room_id = ? AND m.sender_id <> rm.user_id AND m.id > "
        " GREATEST(rm.join_message_id, COALESCE(rr.last_read_message_id, 0))",
        error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t user_value = user_id;
    uint64_t room_value = room_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], user_value);
    bindU64(params[1], room_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    int count = 0;
    bool null = false;
    MYSQL_BIND out[1] = {};
    out[0].buffer_type = MYSQL_TYPE_LONG;
    out[0].buffer = &count;
    out[0].is_null = &null;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    unread = null ? 0 : static_cast<uint32_t>(count);
    error.clear();
    return true;
}

bool MysqlConnection::isRoomMember(uint64_t room_id, uint64_t user_id,
                                   bool& member, std::string& error) {
    member = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "SELECT 1 FROM room_members WHERE room_id = ? AND user_id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    uint64_t user_value = user_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    int one = 0;
    MYSQL_BIND out[1] = {};
    out[0].buffer_type = MYSQL_TYPE_LONG;
    out[0].buffer = &one;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched == MYSQL_NO_DATA) {
        error.clear();
        return true;
    }
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    member = true;
    error.clear();
    return true;
}

bool MysqlConnection::roomJoinMessageId(uint64_t room_id, uint64_t user_id,
                                        uint64_t& message_id,
                                        std::string& error) {
    message_id = 0;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT join_message_id FROM room_members "
              "WHERE room_id = ? AND user_id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room_value = room_id;
    uint64_t user_value = user_id;
    MYSQL_BIND params[2] = {};
    bindU64(params[0], room_value);
    bindU64(params[1], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    uint64_t result = 0;
    MYSQL_BIND out[1] = {};
    out[0].buffer_type = MYSQL_TYPE_LONGLONG;
    out[0].buffer = &result;
    out[0].is_unsigned = true;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched == MYSQL_NO_DATA) {
        error = "not a group member";
        return false;
    }
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    message_id = result;
    error.clear();
    return true;
}

namespace {

bool countForUser(MYSQL* mysql, const char* sql, uint64_t user_id,
                  uint32_t& count, std::string& error) {
    Stmt stmt(mysql, sql, error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t user_value = user_id;
    MYSQL_BIND params[1] = {};
    bindU64(params[0], user_value);
    if (mysql_stmt_bind_param(stmt.get(), params) != 0 ||
        !execute(stmt.get(), error) || mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    int value = 0;
    bool null = false;
    MYSQL_BIND out[1] = {};
    out[0].buffer_type = MYSQL_TYPE_LONG;
    out[0].buffer = &value;
    out[0].is_null = &null;
    if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt.get());
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    count = null ? 0 : static_cast<uint32_t>(value);
    error.clear();
    return true;
}

}  // namespace

bool MysqlConnection::countOwnedGroupsToday(uint64_t user_id, uint32_t& count,
                                            std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    return countForUser(
        mysql_,
        "SELECT COUNT(*) FROM rooms WHERE owner_id = ? AND created_at >= CURDATE()",
        user_id, count, error);
}

bool MysqlConnection::countJoinRequestsToday(uint64_t user_id, uint32_t& count,
                                             std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    return countForUser(
        mysql_,
        "SELECT COUNT(*) FROM room_join_requests "
        "WHERE user_id = ? AND created_at >= CURDATE()",
        user_id, count, error);
}

bool MysqlConnection::countInvitesToday(uint64_t user_id, uint32_t& count,
                                        std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    return countForUser(
        mysql_,
        "SELECT COUNT(*) FROM room_invites "
        "WHERE inviter_id = ? AND created_at >= CURDATE()",
        user_id, count, error);
}

bool MysqlConnection::createUserWithDefaultGroup(
    const std::string& username, const std::string& nickname,
    const std::string& password_hash, uint64_t& user_id, bool& duplicate,
    std::string& error) {
    user_id = 0;
    duplicate = false;
    if (!ensureConnected(error)) {
        return false;
    }
    if (!beginTransaction(mysql_, error)) {
        return false;
    }
    auto fail = [this]() {
        rollback(mysql_);
        return false;
    };
    if (!createUser(username, nickname, password_hash, user_id, duplicate,
                    error)) {
        return fail();
    }
    if (duplicate) {
        rollback(mysql_);
        return true;
    }

    uint64_t owner_id = 0;
    {
        Stmt stmt(mysql_, "SELECT owner_id FROM rooms WHERE id = 1 FOR UPDATE",
                  error);
        if (stmt.get() == nullptr || !execute(stmt.get(), error) ||
            mysql_stmt_store_result(stmt.get()) != 0) {
            if (error.empty()) {
                error = mysql_stmt_error(stmt.get());
            }
            return fail();
        }
        bool null = false;
        MYSQL_BIND out[1] = {};
        out[0].buffer_type = MYSQL_TYPE_LONGLONG;
        out[0].buffer = &owner_id;
        out[0].is_unsigned = true;
        out[0].is_null = &null;
        if (mysql_stmt_bind_result(stmt.get(), out) != 0) {
            error = mysql_stmt_error(stmt.get());
            return fail();
        }
        const int fetched = mysql_stmt_fetch(stmt.get());
        if (fetched == MYSQL_NO_DATA) {
            error = "default group missing";
            return fail();
        }
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(stmt.get());
            return fail();
        }
        if (null) {
            owner_id = 0;
        }
    }

    GroupRole role = GroupRole::kMember;
    if (owner_id == 0) {
        Stmt owner_stmt(mysql_, "UPDATE rooms SET owner_id = ? WHERE id = 1",
                        error);
        if (owner_stmt.get() == nullptr) {
            return fail();
        }
        uint64_t owner_value = user_id;
        MYSQL_BIND params[1] = {};
        bindU64(params[0], owner_value);
        if (mysql_stmt_bind_param(owner_stmt.get(), params) != 0 ||
            !execute(owner_stmt.get(), error)) {
            return fail();
        }
        role = GroupRole::kOwner;
    }
    if (!addRoomMember(1, user_id, role, error)) {
        return fail();
    }
    return commit(mysql_, error);
}

}  // namespace chatbox
