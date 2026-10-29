#include "core/history_store.h"

#include <algorithm>

#include <QDir>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPair>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include "core/message_time.h"

namespace chatbox {
namespace client {

namespace {

const char* const kDatabaseFileName = "history.sqlite";
const int kSchemaVersion = 3;

// 建库语句与设计文档 §5.2 完全一致（注释一并保留，便于逐条比对）。
const char* const kCreateMessagesTable =
    R"(CREATE TABLE IF NOT EXISTS messages (
    id                INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id           INTEGER NOT NULL,          -- 归属账号（本机隔离）
    context           INTEGER NOT NULL,          -- 0 房间 / 1 私聊
    target_id         INTEGER NOT NULL,          -- room_id 或 peer_id
    message_id        INTEGER NOT NULL,          -- 服务端消息 id（只存已确认消息）
    sender_id         INTEGER NOT NULL,
    sender_name       TEXT    NOT NULL DEFAULT '',
    content           TEXT    NOT NULL,
    created_at_utc    TEXT    NOT NULL,          -- yyyy-MM-dd HH:mm:ss UTC
    reply_to_id       INTEGER NOT NULL DEFAULT 0,
    reply_sender_name TEXT    NOT NULL DEFAULT '',
    reply_content     TEXT    NOT NULL DEFAULT '',
    recalled          INTEGER NOT NULL DEFAULT 0,
    outgoing          INTEGER NOT NULL DEFAULT 0
);
)";

const char* const kCreateUniqueIndex =
    R"(CREATE UNIQUE INDEX IF NOT EXISTS uk_msg_server
    ON messages(user_id, context, target_id, message_id);
)";

const char* const kCreatePageIndex =
    R"(CREATE INDEX IF NOT EXISTS idx_msg_page
    ON messages(user_id, context, target_id, message_id);
)";

const char* const kCreateConversationsTable =
    R"(CREATE TABLE IF NOT EXISTS conversations (
    user_id        INTEGER NOT NULL,
    context        INTEGER NOT NULL,
    target_id      INTEGER NOT NULL,
    title          TEXT    NOT NULL,             -- 房间名/昵称/备注
    last_message_id INTEGER NOT NULL DEFAULT 0,
    last_content   TEXT    NOT NULL DEFAULT '',
    last_time_utc  TEXT    NOT NULL DEFAULT '',
    PRIMARY KEY (user_id, context, target_id)
);
)";

const char* const kCreatePendingUploadsTable =
    R"(CREATE TABLE IF NOT EXISTS pending_uploads (
    upload_id           TEXT PRIMARY KEY,
    user_id             INTEGER NOT NULL,
    context             INTEGER NOT NULL,
    target_id           INTEGER NOT NULL,
    source_path         TEXT NOT NULL,
    name                TEXT NOT NULL,
    size                INTEGER NOT NULL,
    mime                TEXT NOT NULL DEFAULT '',
    sha256              TEXT NOT NULL DEFAULT '',
    offset              INTEGER NOT NULL DEFAULT 0,
    status              TEXT NOT NULL DEFAULT 'queued',
    updated_at_utc      TEXT NOT NULL
);
)";

const char* const kCreateFilesTable =
    R"(CREATE TABLE IF NOT EXISTS files (
    file_id             INTEGER NOT NULL,
    user_id             INTEGER NOT NULL,
    message_id          INTEGER NOT NULL DEFAULT 0,
    context             INTEGER NOT NULL,
    target_id           INTEGER NOT NULL,
    name                TEXT NOT NULL,
    size                INTEGER NOT NULL,
    mime                TEXT NOT NULL DEFAULT '',
    sha256              TEXT NOT NULL DEFAULT '',
    thumb_path          TEXT NOT NULL DEFAULT '',
    local_path          TEXT NOT NULL DEFAULT '',
    save_path           TEXT NOT NULL DEFAULT '',
    transfer_state      TEXT NOT NULL DEFAULT '',
    transfer_progress   INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (user_id, file_id, context, target_id)
);
)";

const char* const kCreateGroupsTable =
    R"(CREATE TABLE IF NOT EXISTS groups (
    user_id             INTEGER NOT NULL,
    room_id             INTEGER NOT NULL,
    name                TEXT NOT NULL,
    group_no            TEXT NOT NULL DEFAULT '',
    role                INTEGER NOT NULL DEFAULT 0,
    member_count        INTEGER NOT NULL DEFAULT 0,
    status              INTEGER NOT NULL DEFAULT 0,
    last_message        TEXT NOT NULL DEFAULT '',
    unread              INTEGER NOT NULL DEFAULT 0,
    is_default          INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (user_id, room_id)
);
)";

QString databasePath(const QString& directory) {
    return QDir(directory).filePath(QString::fromLatin1(kDatabaseFileName));
}

qint64 fileSizeOf(const QString& path) {
    const QFileInfo info(path);
    return info.exists() ? info.size() : 0;
}

// 每页条数：调用方传入的 limit 会被夹到 1..200。
const int kMinPageLimit = 1;
const int kMaxPageLimit = 200;

int clampLimit(int limit) {
    if (limit < kMinPageLimit) {
        return kMinPageLimit;
    }
    if (limit > kMaxPageLimit) {
        return kMaxPageLimit;
    }
    return limit;
}

// 「清理 N 天前」的天数范围：1..365，调用方传错也不会误删全部历史。
const int kMinClearDays = 1;
const int kMaxClearDays = 365;

int clampClearDays(int days) {
    if (days < kMinClearDays) {
        return kMinClearDays;
    }
    if (days > kMaxClearDays) {
        return kMaxClearDays;
    }
    return days;
}

// 本地库文件（含 WAL 附属文件），迁移时按这个清单复制。
const char* const kDatabaseSuffixes[] = {"", "-wal", "-shm"};

// 时间列统一存 yyyy-MM-dd HH:mm:ss（UTC），与服务端字符串格式一致，可直接比较。
QString formatUtcColumn(const QDateTime& utc) {
    return utc.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

// 读取时间列：主格式与服务端一致；另兼容 ISO8601 写法。
QDateTime parseUtcColumn(const QString& text) {
    const QDateTime parsed = parseServerUtc(text);
    if (parsed.isValid()) {
        return parsed;
    }
    QDateTime fallback = QDateTime::fromString(text, Qt::ISODate);
    if (!fallback.isValid()) {
        return QDateTime();
    }
    if (fallback.timeSpec() == Qt::LocalTime) {
        fallback.setTimeSpec(Qt::UTC);
    }
    return fallback;
}

// QString() 是 null 串，绑到 NOT NULL 列上会失败，这里统一成空串。
QString textOrEmpty(const QString& text) {
    return text.isNull() ? QString::fromLatin1("") : text;
}

// LIKE 关键字转义：先转义反斜杠，再转义 % 和 _，顺序不能颠倒，
// 否则转义出来的反斜杠会被自己转义一次。
QString escapeLikeKeyword(const QString& keyword) {
    QString escaped;
    escaped.reserve(keyword.size());
    for (const QChar ch : keyword) {
        if (ch == QLatin1Char('\\') || ch == QLatin1Char('%') ||
            ch == QLatin1Char('_')) {
            escaped.append(QLatin1Char('\\'));
        }
        escaped.append(ch);
    }
    return escaped;
}

// 读取用的列清单，messageFromRow 依赖这个顺序。
const char* const kSelectColumns =
    "context, target_id, message_id, sender_id, sender_name, content,"
    " created_at_utc, reply_to_id, reply_sender_name, reply_content,"
    " recalled, outgoing, msg_type, file_id, file_name, file_size, mime,"
    " thumb_path, width, height, duration_ms, local_path, transfer_state,"
    " transfer_progress";

// 把一行数据库记录还原成 ChatMessage（列顺序见 kSelectColumns）。
ChatMessage messageFromRow(const QSqlQuery& query) {
    ChatMessage message;
    message.context = static_cast<ChatContext>(query.value(0).toInt());
    message.target_id = query.value(1).toULongLong();
    message.message_id = query.value(2).toULongLong();
    message.sender_id = query.value(3).toULongLong();
    message.sender_name = query.value(4).toString();
    message.content = query.value(5).toString();
    message.created_at_utc = parseUtcColumn(query.value(6).toString());
    message.reply_to_id = query.value(7).toULongLong();
    message.reply_sender_name = query.value(8).toString();
    message.reply_content = query.value(9).toString();
    message.recalled = query.value(10).toInt() != 0;
    message.outgoing = query.value(11).toInt() != 0;
    message.status = MessageStatus::kSent;
    message.type = query.value(12).toInt() != 0 ? MessageType::kFile
                                                : MessageType::kText;
    message.file.file_id = query.value(13).toULongLong();
    message.file.name = query.value(14).toString();
    message.file.size = query.value(15).toULongLong();
    message.file.mime = query.value(16).toString();
    message.file.thumb_available = !query.value(17).toString().isEmpty();
    message.file.width = query.value(18).toUInt();
    message.file.height = query.value(19).toUInt();
    message.file.duration_ms = query.value(20).toUInt();
    message.local_path = query.value(21).toString();
    message.transfer_state = query.value(22).toString();
    message.transfer_progress = query.value(23).toULongLong();
    return message;
}

// 执行单条 DDL / PRAGMA；失败时写入中文错误文本。
bool runStatement(QSqlDatabase& db, const QString& label, const QString& sql,
                  QString* error) {
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        if (error) {
            *error = QStringLiteral("本地历史库初始化失败（%1）：%2")
                         .arg(label, query.lastError().text());
        }
        return false;
    }
    return true;
}

bool columnExists(QSqlDatabase& db, const QString& table,
                  const QString& column) {
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        return false;
    }
    while (query.next()) {
        if (query.value(1).toString() == column) {
            return true;
        }
    }
    return false;
}

bool ensureColumn(QSqlDatabase& db, const QString& table, const QString& column,
                  const QString& definition, QString* error) {
    if (columnExists(db, table, column)) {
        return true;
    }
    return runStatement(db, QStringLiteral("迁移列 ") + column,
                        QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                            .arg(table, column, definition),
                        error);
}

// 建表 / 建索引 / 写入 user_version。所有语句都要求幂等，可在已有库上重跑。
bool applySchema(QSqlDatabase& db, QString* error) {
    if (!runStatement(db, QStringLiteral("日志模式"),
                      QStringLiteral("PRAGMA journal_mode=WAL"), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("同步级别"),
                      QStringLiteral("PRAGMA synchronous=NORMAL"), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("消息表"),
                      QString::fromUtf8(kCreateMessagesTable), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("唯一索引"),
                      QString::fromUtf8(kCreateUniqueIndex), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("分页索引"),
                      QString::fromUtf8(kCreatePageIndex), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("会话表"),
                      QString::fromUtf8(kCreateConversationsTable), error)) {
        return false;
    }
    const struct {
        const char* name;
        const char* definition;
    } message_columns[] = {
        {"msg_type", "INTEGER NOT NULL DEFAULT 0"},
        {"file_id", "INTEGER NOT NULL DEFAULT 0"},
        {"file_name", "TEXT NOT NULL DEFAULT ''"},
        {"file_size", "INTEGER NOT NULL DEFAULT 0"},
        {"mime", "TEXT NOT NULL DEFAULT ''"},
        {"thumb_path", "TEXT NOT NULL DEFAULT ''"},
        {"width", "INTEGER NOT NULL DEFAULT 0"},
        {"height", "INTEGER NOT NULL DEFAULT 0"},
        {"duration_ms", "INTEGER NOT NULL DEFAULT 0"},
        {"local_path", "TEXT NOT NULL DEFAULT ''"},
        {"transfer_state", "TEXT NOT NULL DEFAULT ''"},
        {"transfer_progress", "INTEGER NOT NULL DEFAULT 0"},
    };
    for (const auto& column : message_columns) {
        if (!ensureColumn(db, QStringLiteral("messages"),
                          QString::fromLatin1(column.name),
                          QString::fromLatin1(column.definition), error)) {
            return false;
        }
    }
    if (!runStatement(db, QStringLiteral("待上传表"),
                      QString::fromUtf8(kCreatePendingUploadsTable), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("文件表"),
                      QString::fromUtf8(kCreateFilesTable), error)) {
        return false;
    }
    if (!ensureColumn(db, QStringLiteral("files"), QStringLiteral("save_path"),
                      QStringLiteral("TEXT NOT NULL DEFAULT ''"), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("群缓存表"),
                      QString::fromUtf8(kCreateGroupsTable), error)) {
        return false;
    }
    if (!runStatement(db, QStringLiteral("版本号"),
                      QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion),
                      error)) {
        return false;
    }
    return true;
}

// 移除同名残留连接（没有任何存活实例持有的历史残留）。
void dropConnection(const QString& connection_name) {
    if (!QSqlDatabase::contains(connection_name)) {
        return;
    }
    {
        QSqlDatabase stale = QSqlDatabase::database(connection_name, false);
        if (stale.isValid()) {
            stale.close();
        }
    }
    QSqlDatabase::removeDatabase(connection_name);
}

// 进程内每个连接名的归属表：连接名 -> 持有它的 HistoryStore 实例。
// 只在 UI 线程访问（与设计一致），因此不需要额外加锁。
QHash<QString, HistoryStore*>& connectionOwners() {
    static QHash<QString, HistoryStore*> owners;
    return owners;
}

}  // namespace

HistoryStore::HistoryStore() = default;

HistoryStore::~HistoryStore() {
    close();
}

QString HistoryStore::connectionNameFor(quint64 user_id) {
    return QStringLiteral("history_") + QString::number(user_id);
}

HistoryStore* HistoryStore::connectionOwner(const QString& connection_name) {
    return connectionOwners().value(connection_name, nullptr);
}

void HistoryStore::claimConnection(const QString& connection_name,
                                   HistoryStore* owner) {
    connectionOwners().insert(connection_name, owner);
}

void HistoryStore::releaseConnection(const QString& connection_name,
                                     HistoryStore* owner) {
    if (connectionOwners().value(connection_name, nullptr) == owner) {
        connectionOwners().remove(connection_name);
    }
}

bool HistoryStore::open(const QString& directory, quint64 user_id, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (user_id == 0) {
        return fail(QStringLiteral("本地历史库账号无效"));
    }
    if (directory.trimmed().isEmpty()) {
        return fail(QStringLiteral("本地历史库目录为空"));
    }

    const QString target = QDir(directory).absolutePath();
    const QFileInfo target_info(target);
    if (target_info.exists() && !target_info.isDir()) {
        return fail(QStringLiteral("本地历史库目录不是文件夹：%1").arg(target));
    }
    if (!QDir().mkpath(target)) {
        return fail(QStringLiteral("本地历史库目录创建失败：%1").arg(target));
    }
    if (!QFileInfo(target).isDir()) {
        return fail(QStringLiteral("本地历史库目录不可用：%1").arg(target));
    }

    // 同目录同账号重复打开：直接复用现有连接。
    if (isOpen() && directory_ == target && user_id_ == user_id) {
        if (error) {
            error->clear();
        }
        return true;
    }
    // 换账号 / 换目录 / 连接已失效：先释放旧连接。
    close();

    const QString connection_name = connectionNameFor(user_id);
    // 同一个连接名同时只能被一个存活实例持有：若已被别的实例占用，
    // 必须直接失败，既不能 addDatabase 也不能 removeDatabase，
    // 否则会把对方的连接拆掉，让对方读到/写到这个库文件上。
    HistoryStore* owner = connectionOwner(connection_name);
    if (owner != nullptr && owner != this) {
        return fail(QStringLiteral("本地历史库已被其它实例占用：%1")
                        .arg(connection_name));
    }
    // 清理没有存活实例持有的残留连接。
    dropConnection(connection_name);

    QString failure;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    connection_name);
        db.setDatabaseName(databasePath(target));
        if (!db.open()) {
            failure = QStringLiteral("本地历史库打开失败：%1")
                          .arg(db.lastError().text());
        } else {
            applySchema(db, &failure);
        }
        // 失败时立即关闭；成功时保持连接，供后续读写复用。
        if (!failure.isEmpty()) {
            db.close();
        }
    }
    if (!failure.isEmpty()) {
        dropConnection(connection_name);
        return fail(failure);
    }

    directory_ = target;
    user_id_ = user_id;
    connection_name_ = connection_name;
    claimConnection(connection_name, this);
    if (error) {
        error->clear();
    }
    return true;
}

void HistoryStore::close() {
    if (!connection_name_.isEmpty()) {
        // 只释放自己登记的连接，别人的连接一律不碰。
        if (connectionOwner(connection_name_) == this) {
            releaseConnection(connection_name_, this);
            dropConnection(connection_name_);
        }
    }
    connection_name_.clear();
    directory_.clear();
    user_id_ = 0;
}

bool HistoryStore::isOpen() const {
    if (connection_name_.isEmpty()) {
        return false;
    }
    const QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    return db.isValid() && db.isOpen();
}

int HistoryStore::schemaVersion() const {
    if (!isOpen()) {
        return 0;
    }
    const QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

QString HistoryStore::directory() const {
    return directory_;
}

quint64 HistoryStore::userId() const {
    return user_id_;
}

qint64 HistoryStore::sizeBytes() const {
    if (directory_.isEmpty()) {
        return 0;
    }
    const QString base = databasePath(directory_);
    qint64 total = 0;
    total += fileSizeOf(base);
    total += fileSizeOf(base + QStringLiteral("-wal"));
    total += fileSizeOf(base + QStringLiteral("-shm"));
    return total;
}

qint64 HistoryStore::messageCount() const {
    if (!isOpen()) {
        return 0;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "SELECT COUNT(*) FROM messages WHERE user_id = ?"))) {
        return 0;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toLongLong();
}

bool HistoryStore::hasMessages() const {
    return messageCount() > 0;
}

bool HistoryStore::saveMessage(const ChatMessage& message) {
    // 未确认的消息（message_id == 0）只留在内存里，不落库。
    if (message.message_id == 0) {
        return false;
    }
    if (!isOpen()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }

    QDateTime created = message.created_at_utc;
    if (!created.isValid()) {
        created = QDateTime::currentDateTimeUtc();
    }
    // 撤回消息不保留正文。
    const QString content = textOrEmpty(message.recalled ? QString() : message.content);

    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO messages (user_id, context, target_id, message_id,"
            " sender_id, sender_name, content, created_at_utc, reply_to_id,"
            " reply_sender_name, reply_content, recalled, outgoing,"
            " msg_type, file_id, file_name, file_size, mime, thumb_path,"
            " width, height, duration_ms, local_path, transfer_state,"
            " transfer_progress)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?,"
            " ?, ?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(user_id, context, target_id, message_id) DO UPDATE SET"
            " sender_id = excluded.sender_id,"
            " sender_name = excluded.sender_name,"
            " content = excluded.content,"
            " created_at_utc = excluded.created_at_utc,"
            " reply_to_id = excluded.reply_to_id,"
            " reply_sender_name = excluded.reply_sender_name,"
            " reply_content = excluded.reply_content,"
            " recalled = excluded.recalled,"
            " outgoing = excluded.outgoing,"
            " msg_type = excluded.msg_type,"
            " file_id = excluded.file_id,"
            " file_name = excluded.file_name,"
            " file_size = excluded.file_size,"
            " mime = excluded.mime,"
            " thumb_path = excluded.thumb_path,"
            " width = excluded.width,"
            " height = excluded.height,"
            " duration_ms = excluded.duration_ms,"
            " local_path = excluded.local_path,"
            " transfer_state = excluded.transfer_state,"
            " transfer_progress = excluded.transfer_progress"))) {
        // 写库失败只记日志，不打断界面；日志里不出现任何消息正文。
        qWarning() << QStringLiteral("本地历史库写入语句准备失败：%1")
                          .arg(query.lastError().text());
        return false;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(static_cast<int>(message.context));
    query.addBindValue(QVariant::fromValue<qulonglong>(message.target_id));
    query.addBindValue(QVariant::fromValue<qulonglong>(message.message_id));
    query.addBindValue(QVariant::fromValue<qulonglong>(message.sender_id));
    query.addBindValue(textOrEmpty(message.sender_name));
    query.addBindValue(content);
    query.addBindValue(formatUtcColumn(created));
    query.addBindValue(QVariant::fromValue<qulonglong>(message.reply_to_id));
    query.addBindValue(textOrEmpty(message.reply_sender_name));
    query.addBindValue(textOrEmpty(message.reply_content));
    query.addBindValue(message.recalled ? 1 : 0);
    query.addBindValue(message.outgoing ? 1 : 0);
    query.addBindValue(message.type == MessageType::kFile ? 1 : 0);
    query.addBindValue(QVariant::fromValue<qulonglong>(message.file.file_id));
    query.addBindValue(textOrEmpty(message.file.name));
    query.addBindValue(QVariant::fromValue<qulonglong>(message.file.size));
    query.addBindValue(textOrEmpty(message.file.mime));
    query.addBindValue(message.file.thumb_available ? QStringLiteral("thumb")
                                                     : QStringLiteral(""));
    query.addBindValue(
        QVariant::fromValue<qulonglong>(message.file.width));
    query.addBindValue(
        QVariant::fromValue<qulonglong>(message.file.height));
    query.addBindValue(
        QVariant::fromValue<qulonglong>(message.file.duration_ms));
    query.addBindValue(textOrEmpty(message.local_path));
    query.addBindValue(textOrEmpty(message.transfer_state));
    query.addBindValue(QVariant::fromValue<qulonglong>(
        message.transfer_progress));
    const bool ok = query.exec();
    if (!ok) {
        qWarning() << QStringLiteral("本地历史库写入失败：%1")
                          .arg(query.lastError().text());
        return false;
    }
    query.finish();
    // 推进会话摘要：更旧的一页历史不得把 last_* 往回改。
    if (!advanceConversationSummary(message.context, message.target_id,
                                    message.message_id, content,
                                    formatUtcColumn(created))) {
        qWarning() << QStringLiteral("本地历史库会话摘要更新失败");
    }
    return true;
}

bool HistoryStore::markRecalled(ChatContext context, quint64 target_id,
                                quint64 message_id) {
    if (message_id == 0 || !isOpen()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    // 撤回必须按会话定位：房间与私聊的 message_id 各自独立编号，
    // 只按 message_id 匹配会把别的会话里同号的消息一起清空。
    if (!query.prepare(QStringLiteral(
            "UPDATE messages SET recalled = 1, content = ''"
            " WHERE user_id = ? AND context = ? AND target_id = ?"
            " AND message_id = ?"))) {
        return false;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(static_cast<int>(context));
    query.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    query.addBindValue(QVariant::fromValue<qulonglong>(message_id));
    if (!query.exec()) {
        return false;
    }
    if (query.numRowsAffected() <= 0) {
        // 没有匹配行：消息不存在，不动会话摘要。
        return false;
    }
    query.finish();

    // 撤回后正文被清空，会话摘要里的 last_content 必须同步重算，
    // 否则离线侧栏还会显示用户已经撤回的正文。
    // 摘要重算失败只记日志：消息本身已经撤回成功，不应因此报错给上层。
    if (!refreshConversationRow(context, target_id)) {
        qWarning() << QStringLiteral("本地历史库撤回后会话摘要重算失败");
    }
    return true;
}

bool HistoryStore::ensureConversationRow(ChatContext context, quint64 target_id) {
    if (!isOpen()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO conversations (user_id, context, target_id,"
            " title, last_message_id, last_content, last_time_utc)"
            " VALUES (?, ?, ?, '', 0, '', '')"))) {
        return false;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(static_cast<int>(context));
    query.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    return query.exec();
}

bool HistoryStore::refreshConversationRow(ChatContext context, quint64 target_id) {
    if (!isOpen()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    if (!ensureConversationRow(context, target_id)) {
        return false;
    }
    // 摘要取该会话里 message_id 最大的一条消息。
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "SELECT message_id, content, created_at_utc FROM messages"
            " WHERE user_id = ? AND context = ? AND target_id = ?"
            " ORDER BY message_id DESC LIMIT 1"))) {
        return false;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(static_cast<int>(context));
    query.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    if (!query.exec()) {
        return false;
    }
    quint64 last_id = 0;
    QString last_content;
    QString last_time;
    if (query.next()) {
        last_id = query.value(0).toULongLong();
        last_content = textOrEmpty(query.value(1).toString());
        last_time = textOrEmpty(query.value(2).toString());
    }
    query.finish();

    QSqlQuery update(db);
    if (!update.prepare(QStringLiteral(
            "UPDATE conversations SET last_message_id = ?, last_content = ?,"
            " last_time_utc = ?"
            " WHERE user_id = ? AND context = ? AND target_id = ?"))) {
        return false;
    }
    update.addBindValue(QVariant::fromValue<qulonglong>(last_id));
    update.addBindValue(textOrEmpty(last_content));
    update.addBindValue(textOrEmpty(last_time));
    update.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    update.addBindValue(static_cast<int>(context));
    update.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    return update.exec();
}

bool HistoryStore::advanceConversationSummary(ChatContext context, quint64 target_id,
                                              quint64 message_id, const QString& content,
                                              const QString& time_text) {
    if (!isOpen()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    if (!ensureConversationRow(context, target_id)) {
        return false;
    }
    QSqlQuery select(db);
    if (!select.prepare(QStringLiteral(
            "SELECT last_message_id FROM conversations"
            " WHERE user_id = ? AND context = ? AND target_id = ?"))) {
        return false;
    }
    select.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    select.addBindValue(static_cast<int>(context));
    select.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    if (!select.exec() || !select.next()) {
        return false;
    }
    const quint64 current = select.value(0).toULongLong();
    select.finish();
    // 更旧的消息只改正文，摘要必须始终指向最新一条，不得往回改。
    if (message_id < current) {
        return true;
    }
    QSqlQuery update(db);
    if (!update.prepare(QStringLiteral(
            "UPDATE conversations SET last_message_id = ?, last_content = ?,"
            " last_time_utc = ?"
            " WHERE user_id = ? AND context = ? AND target_id = ?"))) {
        return false;
    }
    update.addBindValue(QVariant::fromValue<qulonglong>(message_id));
    update.addBindValue(textOrEmpty(content));
    update.addBindValue(textOrEmpty(time_text));
    update.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    update.addBindValue(static_cast<int>(context));
    update.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    return update.exec();
}

bool HistoryStore::saveConversation(const ConversationRow& row) {
    if (!isOpen()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    // 先保证会话行存在（已存在时保持原样）。
    QSqlQuery insert(db);
    if (!insert.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO conversations (user_id, context, target_id,"
            " title, last_message_id, last_content, last_time_utc)"
            " VALUES (?, ?, ?, ?, 0, '', '')"))) {
        return false;
    }
    insert.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    insert.addBindValue(static_cast<int>(row.context));
    insert.addBindValue(QVariant::fromValue<qulonglong>(row.target_id));
    insert.addBindValue(textOrEmpty(row.title));
    if (!insert.exec()) {
        return false;
    }
    // 标题非空才更新，空标题不覆盖已有标题。
    if (!row.title.trimmed().isEmpty()) {
        QSqlQuery update(db);
        if (!update.prepare(QStringLiteral(
                "UPDATE conversations SET title = ?"
                " WHERE user_id = ? AND context = ? AND target_id = ?"))) {
            return false;
        }
        update.addBindValue(row.title);
        update.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
        update.addBindValue(static_cast<int>(row.context));
        update.addBindValue(QVariant::fromValue<qulonglong>(row.target_id));
        if (!update.exec()) {
            return false;
        }
    }
    // last_* 按本地已存消息重算，没有消息时保持 0。
    return refreshConversationRow(row.context, row.target_id);
}

QVector<ConversationRow> HistoryStore::conversations() const {
    QVector<ConversationRow> rows;
    if (!isOpen()) {
        return rows;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return rows;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "SELECT context, target_id, title, last_message_id, last_content,"
            " last_time_utc FROM conversations"
            " WHERE user_id = ? AND last_message_id > 0"
            " ORDER BY last_message_id DESC"))) {
        return rows;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    if (!query.exec()) {
        return rows;
    }
    while (query.next()) {
        ConversationRow row;
        row.context = static_cast<ChatContext>(query.value(0).toInt());
        row.target_id = query.value(1).toULongLong();
        row.title = textOrEmpty(query.value(2).toString());
        row.last_message_id = query.value(3).toULongLong();
        row.last_content = textOrEmpty(query.value(4).toString());
        row.last_time_utc = parseUtcColumn(query.value(5).toString());
        if (row.title.trimmed().isEmpty()) {
            // 没有标题时用类型 + 目标 id 兜底，侧栏不至于显示空白。
            row.title = (row.context == ChatContext::kPrivate)
                            ? QStringLiteral("用户 %1").arg(row.target_id)
                            : QStringLiteral("房间 %1").arg(row.target_id);
        }
        rows.append(row);
    }
    return rows;
}

bool HistoryStore::clearAll() {
    if (!isOpen()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    // 只清当前账号，其它账号的记录不受影响。
    QSqlQuery messages(db);
    if (!messages.prepare(QStringLiteral(
            "DELETE FROM messages WHERE user_id = ?"))) {
        return false;
    }
    messages.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    if (!messages.exec()) {
        return false;
    }
    QSqlQuery conversations(db);
    if (!conversations.prepare(QStringLiteral(
            "DELETE FROM conversations WHERE user_id = ?"))) {
        return false;
    }
    conversations.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    return conversations.exec();
}

bool HistoryStore::clearOlderThan(int days) {
    if (!isOpen()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    const int clamped = clampClearDays(days);
    // 截止串与落库格式一致（yyyy-MM-dd HH:mm:ss UTC），可直接按字符串比较。
    const QString cutoff =
        formatUtcColumn(QDateTime::currentDateTimeUtc().addDays(-clamped));

    QSqlQuery del(db);
    if (!del.prepare(QStringLiteral(
            "DELETE FROM messages WHERE user_id = ? AND created_at_utc < ?"))) {
        return false;
    }
    del.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    del.addBindValue(cutoff);
    if (!del.exec()) {
        return false;
    }

    // 收集当前账号的全部会话：(context, target_id)。
    QVector<QPair<int, quint64> > pairs;
    const char* const pair_queries[] = {
        "SELECT context, target_id FROM conversations WHERE user_id = ?",
        "SELECT DISTINCT context, target_id FROM messages WHERE user_id = ?",
    };
    for (const char* sql : pair_queries) {
        QSqlQuery query(db);
        if (!query.prepare(QString::fromLatin1(sql))) {
            return false;
        }
        query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
        if (!query.exec()) {
            return false;
        }
        while (query.next()) {
            pairs.append(qMakePair(query.value(0).toInt(),
                                   query.value(1).toULongLong()));
        }
    }
    // 逐个重算摘要：消息被删光的会话 last_message_id 归零。
    for (const auto& pair : pairs) {
        if (!refreshConversationRow(static_cast<ChatContext>(pair.first),
                                     pair.second)) {
            return false;
        }
    }
    // 没有消息的会话行直接删掉。
    QSqlQuery cleanup(db);
    if (!cleanup.prepare(QStringLiteral(
            "DELETE FROM conversations WHERE user_id = ? AND last_message_id = 0"))) {
        return false;
    }
    cleanup.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    return cleanup.exec();
}

bool HistoryStore::moveTo(const QString& new_directory, bool migrate, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (!isOpen()) {
        return fail(QStringLiteral("本地历史库未打开，无法切换目录"));
    }
    if (new_directory.trimmed().isEmpty()) {
        return fail(QStringLiteral("本地历史库目标目录为空"));
    }

    const QString target = QDir(new_directory).absolutePath();
    // 目标目录已有本地库时一律拒绝（迁移与不迁移都一样），避免覆盖别的账号的
    // 数据；此时当前连接仍保持在旧目录上打开。
    if (QFile::exists(databasePath(target))) {
        return fail(QStringLiteral("目标目录已存在本地历史库：%1").arg(target));
    }
    QString writable_error;
    if (!ensureWritableDirectory(target, &writable_error)) {
        return fail(writable_error);
    }

    const QString source_directory = directory_;
    const quint64 user_id = user_id_;
    const qint64 expected_count = messageCount();
    const QString source_base = databasePath(source_directory);
    const QString target_base = databasePath(target);

    // 目标目录里本次可能生成的文件：失败回滚时按这个清单清理。
    QStringList generated;
    for (const char* suffix : kDatabaseSuffixes) {
        generated << target_base + QString::fromLatin1(suffix);
    }
    const auto cleanup_target = [&generated]() {
        for (const QString& path : generated) {
            QFile::remove(path);
        }
    };
    // 回滚：重新打开旧目录，让调用方的连接继续可用。
    const auto reopen_source = [this, &source_directory, &user_id]() {
        QString reopen_error;
        if (!open(source_directory, user_id, &reopen_error)) {
            qWarning() << QStringLiteral("本地历史库回滚失败：原目录无法重新打开：%1")
                              .arg(reopen_error);
            return false;
        }
        return true;
    };

    // 先关旧连接：WAL 的 checkpoint 在最后一个连接关闭时完成，
    // 复制出来的主库才是完整的。
    close();

    if (migrate) {
        bool copied = true;
        for (const char* suffix : kDatabaseSuffixes) {
            const QString from = source_base + QString::fromLatin1(suffix);
            if (!QFile::exists(from)) {
                continue;
            }
            const QString to = target_base + QString::fromLatin1(suffix);
            if (QFile::exists(to)) {
                // 没有主库时残留的 wal/shm 没有意义，直接覆盖。
                QFile::remove(to);
            }
            if (!QFile::copy(from, to)) {
                copied = false;
                break;
            }
        }
        if (!copied) {
            cleanup_target();
            reopen_source();
            return fail(QStringLiteral("本地历史库迁移失败：复制到 %1 出错").arg(target));
        }
    }

    QString open_error;
    if (!open(target, user_id, &open_error)) {
        cleanup_target();
        reopen_source();
        return fail(open_error.isEmpty()
                        ? QStringLiteral("本地历史库迁移失败：无法打开目标目录 %1")
                              .arg(target)
                        : open_error);
    }

    // 校验：迁移时目标库的消息数必须与迁移前一致；
    // 不迁移时新目录本来就是空库，不需要校验。
    if (migrate && messageCount() != expected_count) {
        close();
        cleanup_target();
        reopen_source();
        return fail(QStringLiteral("本地历史库迁移失败：目标库消息数校验不一致"));
    }

    if (error) {
        error->clear();
    }
    return true;
}

bool HistoryStore::ensureWritableDirectory(const QString& directory, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (directory.trimmed().isEmpty()) {
        return fail(QStringLiteral("本地历史库目录为空"));
    }
    const QString path = QDir(directory).absolutePath();
    const QFileInfo info(path);
    if (info.exists() && !info.isDir()) {
        return fail(QStringLiteral("本地历史库目录不是文件夹：%1").arg(path));
    }
    if (!QDir().mkpath(path)) {
        return fail(QStringLiteral("本地历史库目录创建失败：%1").arg(path));
    }
    if (!QFileInfo(path).isDir()) {
        return fail(QStringLiteral("本地历史库目录不可用：%1").arg(path));
    }
    // 实际写一个临时文件确认可写：只看权限位在 Windows 上不可靠。
    const QString probe =
        QDir(path).filePath(QStringLiteral(".chatbox-write-test.tmp"));
    QFile file(probe);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(QStringLiteral("本地历史库目录不可写：%1").arg(path));
    }
    file.write("ok", 2);
    file.close();
    QFile::remove(probe);
    if (error) {
        error->clear();
    }
    return true;
}

QVector<ChatMessage> HistoryStore::recent(ChatContext context, quint64 target_id,
                                          int limit) const {
    return readPage(context, target_id, false, 0, limit);
}

QVector<ChatMessage> HistoryStore::before(ChatContext context, quint64 target_id,
                                          quint64 before_id, int limit) const {
    return readPage(context, target_id, true, before_id, limit);
}

QVector<ChatMessage> HistoryStore::readPage(ChatContext context,
                                             quint64 target_id, bool has_before,
                                             quint64 before_id, int limit) const {
    QVector<ChatMessage> rows;
    if (!isOpen()) {
        return rows;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return rows;
    }
    QString sql = QStringLiteral("SELECT ") + QString::fromLatin1(kSelectColumns) +
                  QStringLiteral(" FROM messages"
                                 " WHERE user_id = ? AND context = ? AND target_id = ?");
    if (has_before) {
        sql += QStringLiteral(" AND message_id < ?");
    }
    sql += QStringLiteral(" ORDER BY message_id DESC LIMIT ?");

    QSqlQuery query(db);
    if (!query.prepare(sql)) {
        return rows;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(static_cast<int>(context));
    query.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    if (has_before) {
        query.addBindValue(QVariant::fromValue<qulonglong>(before_id));
    }
    query.addBindValue(clampLimit(limit));
    if (!query.exec()) {
        return rows;
    }
    while (query.next()) {
        rows.append(messageFromRow(query));
    }
    // 取的是最新的一页，返回时反转为升序（旧→新），方便直接追加到视图尾部。
    std::reverse(rows.begin(), rows.end());
    return rows;
}

QVector<ChatMessage> HistoryStore::search(ChatContext context, quint64 target_id,
                                          const QString& keyword, int limit) const {
    QVector<ChatMessage> rows;
    if (!isOpen() || keyword.isEmpty()) {
        return rows;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return rows;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral("SELECT ") +
                       QString::fromLatin1(kSelectColumns) +
                       QStringLiteral(" FROM messages"
                                      " WHERE user_id = ? AND context = ? AND target_id = ?"
                                      " AND content LIKE ? ESCAPE '\\'"
                                      " ORDER BY message_id DESC LIMIT ?"))) {
        return rows;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(static_cast<int>(context));
    query.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    query.addBindValue(QStringLiteral("%") + escapeLikeKeyword(keyword) +
                       QStringLiteral("%"));
    query.addBindValue(clampLimit(limit));
    if (!query.exec()) {
        return rows;
    }
    while (query.next()) {
        rows.append(messageFromRow(query));
    }
    return rows;
}

bool HistoryStore::markFileDeleted(ChatContext context, quint64 target_id,
                                   quint64 message_id) {
    if (!isOpen() || message_id == 0) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "UPDATE messages SET content = ?, msg_type = 0, file_id = 0,"
            " file_name = '', file_size = 0, mime = '', thumb_path = '',"
            " transfer_state = '', transfer_progress = 0"
            " WHERE user_id = ? AND context = ? AND target_id = ?"
            " AND message_id = ?"))) {
        return false;
    }
    query.addBindValue(QStringLiteral("\u6587\u4ef6\u5df2\u5220\u9664"));
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(static_cast<int>(context));
    query.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    query.addBindValue(QVariant::fromValue<qulonglong>(message_id));
    return query.exec();
}

bool HistoryStore::savePendingUpload(const PendingUpload& upload) {
    if (!isOpen() || upload.upload_id.isEmpty()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO pending_uploads"
            " (upload_id, user_id, context, target_id, source_path, name, size,"
            "  mime, sha256, offset, status, updated_at_utc)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"))) {
        return false;
    }
    query.addBindValue(upload.upload_id);
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(static_cast<int>(upload.context));
    query.addBindValue(QVariant::fromValue<qulonglong>(upload.target_id));
    query.addBindValue(textOrEmpty(upload.source_path));
    query.addBindValue(textOrEmpty(upload.name));
    query.addBindValue(QVariant::fromValue<qulonglong>(upload.size));
    query.addBindValue(textOrEmpty(upload.mime));
    query.addBindValue(textOrEmpty(upload.sha256));
    query.addBindValue(QVariant::fromValue<qulonglong>(upload.offset));
    query.addBindValue(textOrEmpty(upload.status));
    query.addBindValue(formatUtcColumn(QDateTime::currentDateTimeUtc()));
    return query.exec();
}

bool HistoryStore::updatePendingUpload(const QString& upload_id,
                                       quint64 offset,
                                       const QString& status) {
    if (!isOpen() || upload_id.isEmpty()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "UPDATE pending_uploads SET offset = ?, status = ?,"
            " updated_at_utc = ? WHERE upload_id = ? AND user_id = ?"))) {
        return false;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(offset));
    query.addBindValue(textOrEmpty(status));
    query.addBindValue(formatUtcColumn(QDateTime::currentDateTimeUtc()));
    query.addBindValue(upload_id);
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    return query.exec();
}

bool HistoryStore::removePendingUpload(const QString& upload_id) {
    if (!isOpen() || upload_id.isEmpty()) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "DELETE FROM pending_uploads WHERE upload_id = ? AND user_id = ?"))) {
        return false;
    }
    query.addBindValue(upload_id);
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    return query.exec();
}

QVector<HistoryStore::PendingUpload> HistoryStore::pendingUploads() const {
    QVector<PendingUpload> rows;
    if (!isOpen()) {
        return rows;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return rows;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "SELECT upload_id, context, target_id, source_path, name, size,"
            " mime, sha256, offset, status FROM pending_uploads"
            " WHERE user_id = ? ORDER BY updated_at_utc ASC"))) {
        return rows;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    if (!query.exec()) {
        return rows;
    }
    while (query.next()) {
        PendingUpload upload;
        upload.upload_id = query.value(0).toString();
        upload.context = static_cast<ChatContext>(query.value(1).toInt());
        upload.target_id = query.value(2).toULongLong();
        upload.source_path = query.value(3).toString();
        upload.name = query.value(4).toString();
        upload.size = query.value(5).toULongLong();
        upload.mime = query.value(6).toString();
        upload.sha256 = query.value(7).toString();
        upload.offset = query.value(8).toULongLong();
        upload.status = query.value(9).toString();
        rows.append(upload);
    }
    return rows;
}

bool HistoryStore::saveFileTask(const FileTask& task) {
    if (!isOpen() || task.file_id == 0) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO files"
            " (file_id, user_id, message_id, context, target_id, name, size,"
            "  mime, sha256, thumb_path, local_path, save_path, transfer_state,"
            "  transfer_progress)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, '', '', ?, ?, ?, ?)"
            " ON CONFLICT(user_id, file_id, context, target_id) DO UPDATE SET"
            " message_id = excluded.message_id, name = excluded.name,"
            " size = excluded.size, mime = excluded.mime,"
            " save_path = excluded.save_path,"
            " transfer_state = excluded.transfer_state,"
            " transfer_progress = excluded.transfer_progress"))) {
        return false;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(task.file_id));
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(QVariant::fromValue<qulonglong>(task.message_id));
    query.addBindValue(static_cast<int>(task.context));
    query.addBindValue(QVariant::fromValue<qulonglong>(task.target_id));
    query.addBindValue(textOrEmpty(task.name));
    query.addBindValue(QVariant::fromValue<qulonglong>(task.total));
    query.addBindValue(textOrEmpty(task.mime));
    query.addBindValue(textOrEmpty(task.save_path));
    query.addBindValue(textOrEmpty(task.save_path));
    query.addBindValue(textOrEmpty(task.state));
    query.addBindValue(QVariant::fromValue<qulonglong>(task.received));
    return query.exec();
}

bool HistoryStore::updateFileTask(quint64 file_id, ChatContext context,
                                  quint64 target_id, const QString& state,
                                  quint64 received, quint64 total) {
    if (!isOpen() || file_id == 0) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "UPDATE files SET transfer_state = ?, transfer_progress = ?,"
            " size = ? WHERE user_id = ? AND file_id = ? AND context = ?"
            " AND target_id = ?"))) {
        return false;
    }
    query.addBindValue(textOrEmpty(state));
    query.addBindValue(QVariant::fromValue<qulonglong>(received));
    query.addBindValue(QVariant::fromValue<qulonglong>(total));
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(QVariant::fromValue<qulonglong>(file_id));
    query.addBindValue(static_cast<int>(context));
    query.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    return query.exec();
}

bool HistoryStore::removeFileTask(quint64 file_id, ChatContext context,
                                  quint64 target_id) {
    if (!isOpen() || file_id == 0) {
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return false;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "DELETE FROM files WHERE user_id = ? AND file_id = ? AND context = ?"
            " AND target_id = ?"))) {
        return false;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    query.addBindValue(QVariant::fromValue<qulonglong>(file_id));
    query.addBindValue(static_cast<int>(context));
    query.addBindValue(QVariant::fromValue<qulonglong>(target_id));
    return query.exec();
}

QVector<HistoryStore::FileTask> HistoryStore::pendingFileTasks() const {
    QVector<FileTask> rows;
    if (!isOpen()) {
        return rows;
    }
    QSqlDatabase db = QSqlDatabase::database(connection_name_, false);
    if (!db.isOpen()) {
        return rows;
    }
    QSqlQuery query(db);
    if (!query.prepare(QStringLiteral(
            "SELECT file_id, context, target_id, message_id, name, save_path,"
            " mime, transfer_state, transfer_progress, size FROM files"
            " WHERE user_id = ? AND transfer_state IN ('queued','downloading',"
            " 'paused') ORDER BY file_id ASC"))) {
        return rows;
    }
    query.addBindValue(QVariant::fromValue<qulonglong>(user_id_));
    if (!query.exec()) {
        return rows;
    }
    while (query.next()) {
        FileTask task;
        task.file_id = query.value(0).toULongLong();
        task.context = static_cast<ChatContext>(query.value(1).toInt());
        task.target_id = query.value(2).toULongLong();
        task.message_id = query.value(3).toULongLong();
        task.name = query.value(4).toString();
        task.save_path = query.value(5).toString();
        task.mime = query.value(6).toString();
        task.state = query.value(7).toString();
        task.received = query.value(8).toULongLong();
        task.total = query.value(9).toULongLong();
        rows.append(task);
    }
    return rows;
}

QString HistoryStore::defaultDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/history");
}

}  // namespace client
}  // namespace chatbox
