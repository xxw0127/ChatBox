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

void bindString(MYSQL_BIND& bind, const std::string& value,
                unsigned long& length) {
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

std::string text(const char* buffer, unsigned long length, bool is_null,
                 std::size_t capacity) {
    if (is_null) {
        return {};
    }
    return std::string(buffer, std::min<std::size_t>(
                                   static_cast<std::size_t>(length), capacity));
}

bool readEntry(MYSQL_STMT* stmt, FileEntry& entry) {
    uint64_t file_id = 0, uploader_id = 0, message_id = 0, size = 0;
    uint32_t width = 0, height = 0, duration = 0;
    int thumb = 0;
    char nickname[129] = {0}, name[181] = {0}, mime[129] = {0};
    char sha[65] = {0}, created[32] = {0};
    unsigned long nickname_len = 0, name_len = 0, mime_len = 0, sha_len = 0,
                  created_len = 0;
    bool nickname_null = false, name_null = false, mime_null = false,
         sha_null = false, created_null = false;
    MYSQL_BIND out[13] = {};
    out[0].buffer_type = MYSQL_TYPE_LONGLONG; out[0].buffer = &file_id;
    out[0].is_unsigned = true;
    out[1].buffer_type = MYSQL_TYPE_LONGLONG; out[1].buffer = &uploader_id;
    out[1].is_unsigned = true;
    out[2].buffer_type = MYSQL_TYPE_STRING; out[2].buffer = nickname;
    out[2].buffer_length = sizeof(nickname) - 1; out[2].length = &nickname_len;
    out[2].is_null = &nickname_null;
    out[3].buffer_type = MYSQL_TYPE_LONGLONG; out[3].buffer = &message_id;
    out[3].is_unsigned = true;
    out[4].buffer_type = MYSQL_TYPE_STRING; out[4].buffer = name;
    out[4].buffer_length = sizeof(name) - 1; out[4].length = &name_len;
    out[4].is_null = &name_null;
    out[5].buffer_type = MYSQL_TYPE_LONGLONG; out[5].buffer = &size;
    out[5].is_unsigned = true;
    out[6].buffer_type = MYSQL_TYPE_STRING; out[6].buffer = mime;
    out[6].buffer_length = sizeof(mime) - 1; out[6].length = &mime_len;
    out[6].is_null = &mime_null;
    out[7].buffer_type = MYSQL_TYPE_STRING; out[7].buffer = sha;
    out[7].buffer_length = sizeof(sha) - 1; out[7].length = &sha_len;
    out[7].is_null = &sha_null;
    out[8].buffer_type = MYSQL_TYPE_LONG; out[8].buffer = &thumb;
    out[9].buffer_type = MYSQL_TYPE_LONG; out[9].buffer = &width;
    out[10].buffer_type = MYSQL_TYPE_LONG; out[10].buffer = &height;
    out[11].buffer_type = MYSQL_TYPE_LONG; out[11].buffer = &duration;
    out[12].buffer_type = MYSQL_TYPE_STRING; out[12].buffer = created;
    out[12].buffer_length = sizeof(created) - 1; out[12].length = &created_len;
    out[12].is_null = &created_null;
    if (mysql_stmt_bind_result(stmt, out) != 0) {
        return false;
    }
    const int fetched = mysql_stmt_fetch(stmt);
    if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
        return false;
    }
    entry.file_id = file_id;
    entry.uploader_id = uploader_id;
    entry.uploader_name = text(nickname, nickname_len, nickname_null, 128);
    entry.message_id = message_id;
    entry.name = text(name, name_len, name_null, 180);
    entry.size = size;
    entry.mime = text(mime, mime_len, mime_null, 128);
    entry.sha256 = text(sha, sha_len, sha_null, 64);
    entry.thumb_available = thumb != 0;
    entry.width = width;
    entry.height = height;
    entry.duration_ms = duration;
    entry.created_at = text(created, created_len, created_null, 31);
    return true;
}

const char* const kFileEntrySelect =
    "SELECT f.id, f.uploader_id, u.nickname, f.message_id, f.name, f.size, "
    "f.mime, f.sha256, f.thumb_available, f.width, f.height, f.duration_ms, "
    "DATE_FORMAT(f.created_at, '%Y-%m-%d %H:%i:%s') "
    "FROM files f JOIN users u ON u.id = f.uploader_id ";

}  // namespace

bool MysqlConnection::createFileUpload(const UploadState& upload,
                                       std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(
        mysql_,
        "INSERT INTO file_uploads "
        "(upload_id, uploader_id, kind, room_id, private_sender_id, "
        " private_receiver_id, name, size, mime, sha256, temp_path, offset, "
        " status, expires_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, "
        " DATE_ADD(UTC_TIMESTAMP(3), INTERVAL 7 DAY))",
        error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t uploader = upload.uploader_id;
    uint64_t room = upload.kind == FileKind::kRoom ? upload.target_id : 0;
    uint64_t psender = upload.kind == FileKind::kPrivate ? upload.uploader_id : 0;
    uint64_t preceiver = upload.kind == FileKind::kPrivate ? upload.target_id : 0;
    uint64_t size = upload.size;
    uint64_t offset = upload.offset;
    int kind = static_cast<int>(upload.kind);
    unsigned long upload_id_len = 0, name_len = 0, mime_len = 0, sha_len = 0,
                  path_len = 0;
    MYSQL_BIND p[12] = {};
    bindString(p[0], upload.upload_id, upload_id_len);
    bindU64(p[1], uploader);
    bindInt(p[2], kind);
    bindU64(p[3], room);
    bindU64(p[4], psender);
    bindU64(p[5], preceiver);
    bindString(p[6], upload.name, name_len);
    bindU64(p[7], size);
    bindString(p[8], upload.mime, mime_len);
    bindString(p[9], upload.sha256, sha_len);
    bindString(p[10], upload.temp_path, path_len);
    bindU64(p[11], offset);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::findFileUpload(const std::string& upload_id,
                                     UploadState& upload, bool& found,
                                     std::string& error) {
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT upload_id, uploader_id, kind, room_id, "
              " private_sender_id, private_receiver_id, name, size, mime, "
              " sha256, temp_path, offset, status, committed_message_id, "
              " committed_file_id, "
              " DATE_FORMAT(committed_at, '%Y-%m-%d %H:%i:%s') "
              "FROM file_uploads WHERE upload_id = ? LIMIT 1",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    unsigned long id_len = 0;
    MYSQL_BIND p[1] = {};
    bindString(p[0], upload_id, id_len);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0 ||
        !execute(stmt.get(), error) ||
        mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    char id[37] = {0}, name[181] = {0}, mime[129] = {0}, sha[65] = {0},
         path[256] = {0};
    unsigned long id_l = 0, name_l = 0, mime_l = 0, sha_l = 0, path_l = 0;
    uint64_t uploader = 0, room = 0, psender = 0, preceiver = 0, size = 0,
             offset = 0, committed_message = 0, committed_file = 0;
    char committed_at[32] = {0};
    unsigned long committed_at_len = 0;
    bool committed_at_null = false;
    int kind = 0, status = 0;
    bool n1 = false, n2 = false, n3 = false, n4 = false;
    MYSQL_BIND out[16] = {};
    out[0].buffer_type = MYSQL_TYPE_STRING; out[0].buffer = id;
    out[0].buffer_length = sizeof(id) - 1; out[0].length = &id_l;
    out[1].buffer_type = MYSQL_TYPE_LONGLONG; out[1].buffer = &uploader;
    out[1].is_unsigned = true;
    out[2].buffer_type = MYSQL_TYPE_LONG; out[2].buffer = &kind;
    out[3].buffer_type = MYSQL_TYPE_LONGLONG; out[3].buffer = &room;
    out[3].is_unsigned = true;
    out[4].buffer_type = MYSQL_TYPE_LONGLONG; out[4].buffer = &psender;
    out[4].is_unsigned = true;
    out[5].buffer_type = MYSQL_TYPE_LONGLONG; out[5].buffer = &preceiver;
    out[5].is_unsigned = true;
    out[6].buffer_type = MYSQL_TYPE_STRING; out[6].buffer = name;
    out[6].buffer_length = sizeof(name) - 1; out[6].length = &name_l;
    out[6].is_null = &n1;
    out[7].buffer_type = MYSQL_TYPE_LONGLONG; out[7].buffer = &size;
    out[7].is_unsigned = true;
    out[8].buffer_type = MYSQL_TYPE_STRING; out[8].buffer = mime;
    out[8].buffer_length = sizeof(mime) - 1; out[8].length = &mime_l;
    out[8].is_null = &n2;
    out[9].buffer_type = MYSQL_TYPE_STRING; out[9].buffer = sha;
    out[9].buffer_length = sizeof(sha) - 1; out[9].length = &sha_l;
    out[9].is_null = &n3;
    out[10].buffer_type = MYSQL_TYPE_STRING; out[10].buffer = path;
    out[10].buffer_length = sizeof(path) - 1; out[10].length = &path_l;
    out[10].is_null = &n4;
    out[11].buffer_type = MYSQL_TYPE_LONGLONG; out[11].buffer = &offset;
    out[11].is_unsigned = true;
    out[12].buffer_type = MYSQL_TYPE_LONG; out[12].buffer = &status;
    out[13].buffer_type = MYSQL_TYPE_LONGLONG;
    out[13].buffer = &committed_message; out[13].is_unsigned = true;
    out[14].buffer_type = MYSQL_TYPE_LONGLONG;
    out[14].buffer = &committed_file; out[14].is_unsigned = true;
    out[15].buffer_type = MYSQL_TYPE_STRING; out[15].buffer = committed_at;
    out[15].buffer_length = sizeof(committed_at) - 1;
    out[15].length = &committed_at_len; out[15].is_null = &committed_at_null;
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
    upload.upload_id = text(id, id_l, false, 36);
    upload.uploader_id = uploader;
    upload.kind = static_cast<FileKind>(kind);
    upload.target_id = upload.kind == FileKind::kRoom
                           ? room
                           : (psender == uploader ? preceiver : psender);
    upload.name = text(name, name_l, n1, 180);
    upload.size = size;
    upload.mime = text(mime, mime_l, n2, 128);
    upload.sha256 = text(sha, sha_l, n3, 64);
    upload.temp_path = text(path, path_l, n4, 255);
    upload.offset = offset;
    upload.status = status;
    upload.committed_message_id = committed_message;
    upload.committed_file_id = committed_file;
    if (!committed_at_null) {
        upload.committed_at.assign(committed_at, committed_at_len);
    }
    found = true;
    error.clear();
    return true;
}

bool MysqlConnection::updateFileUploadOffset(const std::string& upload_id,
                                             uint64_t offset,
                                             std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "UPDATE file_uploads SET offset = ? WHERE upload_id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t offset_value = offset;
    unsigned long id_len = 0;
    MYSQL_BIND p[2] = {};
    bindU64(p[0], offset_value);
    bindString(p[1], upload_id, id_len);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::updateFileUploadStatus(const std::string& upload_id,
                                             int status, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "UPDATE file_uploads SET status = ? WHERE upload_id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int status_value = status;
    unsigned long id_len = 0;
    MYSQL_BIND p[2] = {};
    bindInt(p[0], status_value);
    bindString(p[1], upload_id, id_len);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::expireFileUploads(std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "UPDATE file_uploads SET status = 2"
              " WHERE status = 0 AND expires_at < UTC_TIMESTAMP(3)",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::setFileUploadCommitted(
    const std::string& upload_id, uint64_t message_id, uint64_t file_id,
    const std::string& created_at, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "UPDATE file_uploads SET status = 1, committed_message_id = ?,"
              " committed_file_id = ?, committed_at = ? WHERE upload_id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t message = message_id;
    uint64_t file = file_id;
    unsigned long id_len = 0, created_len = 0;
    MYSQL_BIND p[4] = {};
    bindU64(p[0], message);
    bindU64(p[1], file);
    bindString(p[2], created_at, created_len);
    bindString(p[3], upload_id, id_len);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::insertFileRecord(uint64_t uploader_id, FileKind kind,
                                       uint64_t target_id, const FileMeta& meta,
                                       const std::string& storage_path,
                                       FileEntry& entry, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(
        mysql_,
        "INSERT INTO files "
        "(uploader_id, kind, room_id, private_sender_id, private_receiver_id, "
        " name, size, mime, sha256, storage_path, thumb_path, thumb_available, "
        " width, height, duration_ms, status) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, '', ?, ?, ?, ?, 0)",
        error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t uploader = uploader_id;
    uint64_t room = kind == FileKind::kRoom ? target_id : 0;
    uint64_t psender = kind == FileKind::kPrivate ? uploader_id : 0;
    uint64_t preceiver = kind == FileKind::kPrivate ? target_id : 0;
    uint64_t size = meta.size;
    int kind_value = static_cast<int>(kind);
    int thumb = meta.thumb_available ? 1 : 0;
    uint64_t width = meta.width;
    uint64_t height = meta.height;
    uint64_t duration = meta.duration_ms;
    unsigned long name_len = 0, mime_len = 0, sha_len = 0, path_len = 0;
    MYSQL_BIND p[14] = {};
    bindU64(p[0], uploader);
    bindInt(p[1], kind_value);
    bindU64(p[2], room);
    bindU64(p[3], psender);
    bindU64(p[4], preceiver);
    bindString(p[5], meta.name, name_len);
    bindU64(p[6], size);
    bindString(p[7], meta.mime, mime_len);
    bindString(p[8], meta.sha256, sha_len);
    bindString(p[9], storage_path, path_len);
    bindInt(p[10], thumb);
    bindU64(p[11], width);
    bindU64(p[12], height);
    bindU64(p[13], duration);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0 ||
        !execute(stmt.get(), error)) {
        return false;
    }
    entry = FileEntry{};
    entry.file_id = mysql_stmt_insert_id(stmt.get());
    entry.uploader_id = uploader_id;
    entry.name = meta.name;
    entry.size = meta.size;
    entry.mime = meta.mime;
    entry.sha256 = meta.sha256;
    entry.thumb_available = meta.thumb_available;
    entry.width = meta.width;
    entry.height = meta.height;
    entry.duration_ms = meta.duration_ms;
    return entry.file_id != 0;
}

bool MysqlConnection::findFileRecord(uint64_t file_id, FileEntry& entry,
                                     bool& found, std::string& error) {
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }
    const std::string sql = std::string(kFileEntrySelect) +
                            "WHERE f.id = ? AND f.status = 0 LIMIT 1";
    Stmt stmt(mysql_, sql.c_str(), error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t id = file_id;
    MYSQL_BIND p[1] = {};
    bindU64(p[0], id);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0 ||
        !execute(stmt.get(), error) ||
        mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    if (!readEntry(stmt.get(), entry)) {
        if (mysql_stmt_errno(stmt.get()) == 0) {
            error.clear();
            return true;
        }
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    found = true;
    error.clear();
    return true;
}

bool MysqlConnection::findFileAccess(uint64_t file_id, FileAccess& access,
                                     bool& found, std::string& error) {
    found = false;
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT uploader_id, kind, room_id, private_sender_id, "
              "private_receiver_id, message_id, name, size, mime, sha256, "
              "storage_path, thumb_path, status FROM files WHERE id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t id = file_id;
    MYSQL_BIND p[1] = {};
    bindU64(p[0], id);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0 ||
        !execute(stmt.get(), error) ||
        mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    uint64_t uploader = 0, room = 0, psender = 0, preceiver = 0, message = 0,
             size = 0;
    int kind = 0, status = 0;
    char name[181] = {0}, mime[129] = {0}, sha[65] = {0}, storage[256] = {0},
         thumb[256] = {0};
    unsigned long name_l = 0, mime_l = 0, sha_l = 0, storage_l = 0, thumb_l = 0;
    bool n1 = false, n2 = false, n3 = false, n4 = false, n5 = false;
    MYSQL_BIND out[13] = {};
    out[0].buffer_type = MYSQL_TYPE_LONGLONG; out[0].buffer = &uploader;
    out[0].is_unsigned = true;
    out[1].buffer_type = MYSQL_TYPE_LONG; out[1].buffer = &kind;
    out[2].buffer_type = MYSQL_TYPE_LONGLONG; out[2].buffer = &room;
    out[2].is_unsigned = true;
    out[3].buffer_type = MYSQL_TYPE_LONGLONG; out[3].buffer = &psender;
    out[3].is_unsigned = true;
    out[4].buffer_type = MYSQL_TYPE_LONGLONG; out[4].buffer = &preceiver;
    out[4].is_unsigned = true;
    out[5].buffer_type = MYSQL_TYPE_LONGLONG; out[5].buffer = &message;
    out[5].is_unsigned = true;
    out[6].buffer_type = MYSQL_TYPE_STRING; out[6].buffer = name;
    out[6].buffer_length = sizeof(name) - 1; out[6].length = &name_l;
    out[6].is_null = &n1;
    out[7].buffer_type = MYSQL_TYPE_LONGLONG; out[7].buffer = &size;
    out[7].is_unsigned = true;
    out[8].buffer_type = MYSQL_TYPE_STRING; out[8].buffer = mime;
    out[8].buffer_length = sizeof(mime) - 1; out[8].length = &mime_l;
    out[8].is_null = &n2;
    out[9].buffer_type = MYSQL_TYPE_STRING; out[9].buffer = sha;
    out[9].buffer_length = sizeof(sha) - 1; out[9].length = &sha_l;
    out[9].is_null = &n3;
    out[10].buffer_type = MYSQL_TYPE_STRING; out[10].buffer = storage;
    out[10].buffer_length = sizeof(storage) - 1; out[10].length = &storage_l;
    out[10].is_null = &n4;
    out[11].buffer_type = MYSQL_TYPE_STRING; out[11].buffer = thumb;
    out[11].buffer_length = sizeof(thumb) - 1; out[11].length = &thumb_l;
    out[11].is_null = &n5;
    out[12].buffer_type = MYSQL_TYPE_LONG; out[12].buffer = &status;
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
    access.file_id = file_id;
    access.uploader_id = uploader;
    access.kind = static_cast<FileKind>(kind);
    access.room_id = room;
    access.private_sender_id = psender;
    access.private_receiver_id = preceiver;
    access.message_id = message;
    access.name = text(name, name_l, n1, 180);
    access.size = size;
    access.mime = text(mime, mime_l, n2, 128);
    access.sha256 = text(sha, sha_l, n3, 64);
    access.storage_path = text(storage, storage_l, n4, 255);
    access.thumb_path = text(thumb, thumb_l, n5, 255);
    access.status = status;
    found = true;
    error.clear();
    return true;
}

bool MysqlConnection::listFileRecords(FileKind kind, uint64_t viewer_id,
                                      uint64_t target_id,
                                      uint64_t before_file_id, int limit,
                                      std::vector<FileEntry>& entries,
                                      uint32_t& total_count, uint64_t& used_bytes,
                                      std::string& error) {
    entries.clear();
    total_count = 0;
    used_bytes = 0;
    if (!ensureConnected(error)) {
        return false;
    }
    const bool room = kind == FileKind::kRoom;
    const std::string where =
        room ? "room_id = ? AND status = 0"
             : "((private_sender_id = ? AND private_receiver_id = ?) OR "
               "(private_sender_id = ? AND private_receiver_id = ?)) "
               "AND status = 0";
    {
        Stmt count_stmt(
            mysql_,
            room ? "SELECT COUNT(*), COALESCE(SUM(size),0) FROM files "
                   "WHERE room_id = ? AND status = 0"
                 : "SELECT COUNT(*), COALESCE(SUM(size),0) FROM files "
                   "WHERE ((private_sender_id = ? AND private_receiver_id = ?) "
                   "OR (private_sender_id = ? AND private_receiver_id = ?)) "
                   "AND status = 0",
            error);
        if (count_stmt.get() == nullptr) {
            return false;
        }
        uint64_t target = target_id, target2 = target_id;
        uint64_t viewer = viewer_id;
        MYSQL_BIND cp[4] = {};
        bindU64(cp[0], target);
        if (!room) {
            bindU64(cp[1], viewer);
            bindU64(cp[2], viewer);
            bindU64(cp[3], target2);
        }
        if (mysql_stmt_bind_param(count_stmt.get(), cp) != 0 ||
            !execute(count_stmt.get(), error) ||
            mysql_stmt_store_result(count_stmt.get()) != 0) {
            if (error.empty()) {
                error = mysql_stmt_error(count_stmt.get());
            }
            return false;
        }
        int count = 0;
        uint64_t used = 0;
        MYSQL_BIND out[2] = {};
        out[0].buffer_type = MYSQL_TYPE_LONG;
        out[0].buffer = &count;
        out[1].buffer_type = MYSQL_TYPE_LONGLONG;
        out[1].buffer = &used;
        out[1].is_unsigned = true;
        if (mysql_stmt_bind_result(count_stmt.get(), out) != 0) {
            error = mysql_stmt_error(count_stmt.get());
            return false;
        }
        const int fetched = mysql_stmt_fetch(count_stmt.get());
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(count_stmt.get());
            return false;
        }
        total_count = static_cast<uint32_t>(count);
        used_bytes = used;
    }

    const std::string id_sql =
        std::string("SELECT id FROM files WHERE ") + where +
        " AND id < ? ORDER BY id DESC LIMIT ?";
    Stmt id_stmt(mysql_, id_sql.c_str(), error);
    if (id_stmt.get() == nullptr) {
        return false;
    }
    uint64_t target = target_id, target2 = target_id;
    uint64_t viewer = viewer_id;
    uint64_t before =
        before_file_id == 0 ? std::numeric_limits<uint64_t>::max()
                            : before_file_id;
    int limit_value = limit <= 0 ? 50 : std::min(limit, 100);
    MYSQL_BIND p[6] = {};
    bindU64(p[0], target);
    int param_count = 1;
    if (!room) {
        bindU64(p[1], viewer);
        bindU64(p[2], viewer);
        bindU64(p[3], target2);
        param_count = 4;
    }
    bindU64(p[param_count], before);
    bindInt(p[param_count + 1], limit_value);
    if (mysql_stmt_bind_param(id_stmt.get(), p) != 0 ||
        !execute(id_stmt.get(), error) ||
        mysql_stmt_store_result(id_stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(id_stmt.get());
        }
        return false;
    }
    uint64_t id = 0;
    MYSQL_BIND id_out[1] = {};
    id_out[0].buffer_type = MYSQL_TYPE_LONGLONG;
    id_out[0].buffer = &id;
    id_out[0].is_unsigned = true;
    if (mysql_stmt_bind_result(id_stmt.get(), id_out) != 0) {
        error = mysql_stmt_error(id_stmt.get());
        return false;
    }
    std::vector<uint64_t> ids;
    while (true) {
        const int fetched = mysql_stmt_fetch(id_stmt.get());
        if (fetched == MYSQL_NO_DATA) {
            break;
        }
        if (fetched != 0 && fetched != MYSQL_DATA_TRUNCATED) {
            error = mysql_stmt_error(id_stmt.get());
            return false;
        }
        ids.push_back(id);
    }
    for (const uint64_t file_id : ids) {
        FileEntry entry;
        bool found = false;
        if (!findFileRecord(file_id, entry, found, error)) {
            return false;
        }
        if (found) {
            entries.push_back(std::move(entry));
        }
    }
    error.clear();
    return true;
}

bool MysqlConnection::markFileDeleted(uint64_t file_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "UPDATE files SET status = 1 WHERE id = ?", error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t id = file_id;
    MYSQL_BIND p[1] = {};
    bindU64(p[0], id);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::deleteFileRecord(uint64_t file_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "DELETE FROM files WHERE id = ?", error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t id = file_id;
    MYSQL_BIND p[1] = {};
    bindU64(p[0], id);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::listRoomFilePaths(
    uint64_t room_id, std::vector<std::pair<std::string, std::string>>& paths,
    std::string& error) {
    paths.clear();
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "SELECT storage_path, thumb_path FROM files"
              " WHERE room_id = ? AND status = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room = room_id;
    MYSQL_BIND p[1] = {};
    bindU64(p[0], room);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0 ||
        !execute(stmt.get(), error) ||
        mysql_stmt_store_result(stmt.get()) != 0) {
        if (error.empty()) {
            error = mysql_stmt_error(stmt.get());
        }
        return false;
    }
    char storage[256] = {0};
    char thumb[256] = {0};
    unsigned long storage_len = 0, thumb_len = 0;
    bool storage_null = false, thumb_null = false;
    MYSQL_BIND out[2] = {};
    out[0].buffer_type = MYSQL_TYPE_STRING;
    out[0].buffer = storage;
    out[0].buffer_length = sizeof(storage) - 1;
    out[0].length = &storage_len;
    out[0].is_null = &storage_null;
    out[1].buffer_type = MYSQL_TYPE_STRING;
    out[1].buffer = thumb;
    out[1].buffer_length = sizeof(thumb) - 1;
    out[1].length = &thumb_len;
    out[1].is_null = &thumb_null;
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
        paths.emplace_back(
            text(storage, storage_len, storage_null, 255),
            text(thumb, thumb_len, thumb_null, 255));
    }
    error.clear();
    return true;
}

bool MysqlConnection::markRoomFilesDeleted(uint64_t room_id,
                                           std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "UPDATE files SET status = 1, thumb_available = 0"
              " WHERE room_id = ? AND status = 0",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t room = room_id;
    MYSQL_BIND p[1] = {};
    bindU64(p[0], room);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::setFileMessageId(uint64_t file_id, uint64_t message_id,
                                       std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "UPDATE files SET message_id = ? WHERE id = ?", error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t file = file_id;
    uint64_t message = message_id;
    MYSQL_BIND p[2] = {};
    bindU64(p[0], message);
    bindU64(p[1], file);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::setFileStoragePath(uint64_t file_id,
                                         const std::string& path,
                                         std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_, "UPDATE files SET storage_path = ? WHERE id = ?", error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t file = file_id;
    unsigned long path_len = 0;
    MYSQL_BIND p[2] = {};
    bindString(p[0], path, path_len);
    bindU64(p[1], file);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::setFileThumbnail(uint64_t file_id,
                                       const std::string& thumb_path,
                                       uint32_t width, uint32_t height,
                                       uint32_t duration_ms,
                                       std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "UPDATE files SET thumb_path = ?, thumb_available = ?,"
              " width = ?, height = ?, duration_ms = ? WHERE id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t id = file_id;
    int thumb = thumb_path.empty() ? 0 : 1;
    uint64_t width_value = width;
    uint64_t height_value = height;
    uint64_t duration_value = duration_ms;
    unsigned long path_len = 0;
    MYSQL_BIND p[6] = {};
    bindString(p[0], thumb_path, path_len);
    bindInt(p[1], thumb);
    bindU64(p[2], width_value);
    bindU64(p[3], height_value);
    bindU64(p[4], duration_value);
    bindU64(p[5], id);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::insertRoomFileMessage(
    int room_id, uint64_t sender_id, const std::string& client_msg_id,
    const std::string& content, uint64_t file_id, const std::string& created_at,
    uint64_t& message_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "INSERT INTO messages "
              "(room_id, sender_id, client_msg_id, msg_type, file_id, content, "
              " created_at) VALUES (?, ?, ?, 1, ?, ?, ?)",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    int room = room_id;
    uint64_t sender = sender_id;
    uint64_t file = file_id;
    unsigned long client_len = 0, content_len = 0, created_len = 0;
    MYSQL_BIND p[6] = {};
    bindInt(p[0], room);
    bindU64(p[1], sender);
    bindString(p[2], client_msg_id, client_len);
    bindU64(p[3], file);
    bindString(p[4], content, content_len);
    bindString(p[5], created_at, created_len);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0 ||
        !execute(stmt.get(), error)) {
        return false;
    }
    message_id = mysql_stmt_insert_id(stmt.get());
    return message_id != 0;
}

bool MysqlConnection::insertPrivateFileMessage(
    uint64_t sender_id, uint64_t receiver_id, const std::string& client_msg_id,
    const std::string& content, uint64_t file_id, const std::string& created_at,
    uint64_t& message_id, std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              "INSERT INTO private_messages "
              "(sender_id, receiver_id, client_msg_id, msg_type, file_id, content, "
              " created_at) VALUES (?, ?, ?, 1, ?, ?, ?)",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t sender = sender_id, receiver = receiver_id, file = file_id;
    unsigned long client_len = 0, content_len = 0, created_len = 0;
    MYSQL_BIND p[6] = {};
    bindU64(p[0], sender);
    bindU64(p[1], receiver);
    bindString(p[2], client_msg_id, client_len);
    bindU64(p[3], file);
    bindString(p[4], content, content_len);
    bindString(p[5], created_at, created_len);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0 ||
        !execute(stmt.get(), error)) {
        return false;
    }
    message_id = mysql_stmt_insert_id(stmt.get());
    return message_id != 0;
}

bool MysqlConnection::deleteFileMessage(uint64_t message_id, bool room,
                                        std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              room ? "DELETE FROM messages WHERE id = ?"
                   : "DELETE FROM private_messages WHERE id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t id = message_id;
    MYSQL_BIND p[1] = {};
    bindU64(p[0], id);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

bool MysqlConnection::updateFileMessageDeleted(uint64_t message_id, bool room,
                                               std::string& error) {
    if (!ensureConnected(error)) {
        return false;
    }
    Stmt stmt(mysql_,
              room ? "UPDATE messages SET content = '文件已删除' WHERE id = ?"
                   : "UPDATE private_messages SET content = '文件已删除' WHERE id = ?",
              error);
    if (stmt.get() == nullptr) {
        return false;
    }
    uint64_t id = message_id;
    MYSQL_BIND p[1] = {};
    bindU64(p[0], id);
    if (mysql_stmt_bind_param(stmt.get(), p) != 0) {
        error = mysql_stmt_error(stmt.get());
        return false;
    }
    return execute(stmt.get(), error);
}

}  // namespace chatbox
