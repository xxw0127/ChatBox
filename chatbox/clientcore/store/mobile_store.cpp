#include "store/mobile_store.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

namespace chatbox {
namespace clientcore {

namespace {

QVariant u64Value(quint64 value) {
    return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(value));
}

QVariant u32Value(quint32 value) {
    return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(value));
}

QString nowUtc() {
    return QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

quint64 readU64(const QSqlQuery& query, int index) {
    return static_cast<quint64>(query.value(index).toULongLong());
}

QString textValue(const QString& value) {
    return value.isNull() ? QStringLiteral("") : value;
}

}  // namespace

MobileStore::MobileStore() = default;

MobileStore::~MobileStore() {
    close();
}

void MobileStore::setError(QString* error, const QString& text) {
    if (error != nullptr) {
        *error = text;
    }
}

bool MobileStore::execSql(const QString& sql, QString* error) const {
    QSqlQuery query(db_);
    if (!query.exec(sql)) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool MobileStore::open(const QString& path, QString* error) {
    if (isOpen()) {
        return true;
    }
    if (path.isEmpty()) {
        setError(error, QStringLiteral("empty database path"));
        return false;
    }
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        setError(error, QStringLiteral("QSQLITE driver not available"));
        return false;
    }

    const QString directory = QFileInfo(path).absolutePath();
    if (!directory.isEmpty() && !QDir().mkpath(directory)) {
        setError(error, QStringLiteral("cannot create database directory"));
        return false;
    }

    connection_name_ =
        QStringLiteral("chatbox_mobile_%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_name_);
    db_.setDatabaseName(path);
    if (!db_.open()) {
        setError(error, db_.lastError().text());
        close();
        return false;
    }

    const QStringList pragmas = {
        QStringLiteral("PRAGMA journal_mode=WAL"),
        QStringLiteral("PRAGMA synchronous=NORMAL"),
    };
    for (const QString& pragma : pragmas) {
        if (!execSql(pragma, error)) {
            close();
            return false;
        }
    }

    const QStringList schema = {
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS conversations ("
            " kind INTEGER NOT NULL,"
            " target_id INTEGER NOT NULL,"
            " title TEXT NOT NULL DEFAULT '',"
            " last_message_id INTEGER NOT NULL DEFAULT 0,"
            " last_content TEXT NOT NULL DEFAULT '',"
            " last_time TEXT NOT NULL DEFAULT '',"
            " unread INTEGER NOT NULL DEFAULT 0,"
            " updated_at TEXT NOT NULL DEFAULT '',"
            " PRIMARY KEY (kind, target_id))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            " kind INTEGER NOT NULL,"
            " target_id INTEGER NOT NULL,"
            " message_id INTEGER NOT NULL,"
            " client_msg_id TEXT NOT NULL DEFAULT '',"
            " sender_id INTEGER NOT NULL DEFAULT 0,"
            " nickname TEXT NOT NULL DEFAULT '',"
            " content TEXT NOT NULL DEFAULT '',"
            " created_at TEXT NOT NULL DEFAULT '',"
            " reply_to_id INTEGER NOT NULL DEFAULT 0,"
            " reply_sender_name TEXT NOT NULL DEFAULT '',"
            " reply_content TEXT NOT NULL DEFAULT '',"
            " recalled INTEGER NOT NULL DEFAULT 0,"
            " msg_type INTEGER NOT NULL DEFAULT 0,"
            " file_id INTEGER NOT NULL DEFAULT 0,"
            " PRIMARY KEY (kind, target_id, message_id))"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_messages_client"
            " ON messages(client_msg_id)"),
        QStringLiteral(
            "CREATE INDEX IF NOT EXISTS idx_messages_page"
            " ON messages(kind, target_id, message_id)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS outbox ("
            " client_msg_id TEXT PRIMARY KEY,"
            " kind INTEGER NOT NULL,"
            " target_id INTEGER NOT NULL,"
            " content TEXT NOT NULL,"
            " created_at TEXT NOT NULL,"
            " state TEXT NOT NULL DEFAULT 'pending',"
            " retry_count INTEGER NOT NULL DEFAULT 0)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS meta ("
            " key TEXT PRIMARY KEY,"
            " value TEXT NOT NULL)"),
    };
    for (const QString& statement : schema) {
        if (!execSql(statement, error)) {
            close();
            return false;
        }
    }
    return true;
}

void MobileStore::close() {
    if (db_.isValid()) {
        db_.close();
    }
    db_ = QSqlDatabase();
    if (!connection_name_.isEmpty()) {
        QSqlDatabase::removeDatabase(connection_name_);
        connection_name_.clear();
    }
}

bool MobileStore::isOpen() const {
    return db_.isValid() && db_.isOpen();
}

bool MobileStore::upsertConversation(const ConversationRow& row, QString* error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO conversations (kind, target_id, title, last_message_id,"
            " last_content, last_time, unread, updated_at)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(kind, target_id) DO UPDATE SET"
            " title = excluded.title,"
            " unread = excluded.unread,"
            " updated_at = excluded.updated_at,"
            " last_message_id = CASE"
            "   WHEN excluded.last_message_id >= conversations.last_message_id"
            "   THEN excluded.last_message_id ELSE conversations.last_message_id END,"
            " last_content = CASE"
            "   WHEN excluded.last_message_id >= conversations.last_message_id"
            "   THEN excluded.last_content ELSE conversations.last_content END,"
            " last_time = CASE"
            "   WHEN excluded.last_message_id >= conversations.last_message_id"
            "   THEN excluded.last_time ELSE conversations.last_time END"))) {
        setError(error, query.lastError().text());
        return false;
    }
    query.addBindValue(row.kind);
    query.addBindValue(u64Value(row.target_id));
    query.addBindValue(textValue(row.title));
    query.addBindValue(u64Value(row.last_message_id));
    query.addBindValue(textValue(row.last_content));
    query.addBindValue(textValue(row.last_time));
    query.addBindValue(u32Value(row.unread));
    query.addBindValue(nowUtc());
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool MobileStore::loadConversations(QVector<ConversationRow>& rows,
                                    QString* error) const {
    rows.clear();
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
            "SELECT kind, target_id, title, last_message_id, last_content,"
            " last_time, unread FROM conversations"
            " ORDER BY last_message_id DESC, target_id ASC"))) {
        setError(error, query.lastError().text());
        return false;
    }
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    while (query.next()) {
        ConversationRow row;
        row.kind = query.value(0).toInt();
        row.target_id = readU64(query, 1);
        row.title = query.value(2).toString();
        row.last_message_id = readU64(query, 3);
        row.last_content = query.value(4).toString();
        row.last_time = query.value(5).toString();
        row.unread = static_cast<quint32>(query.value(6).toULongLong());
        rows.push_back(row);
    }
    return true;
}

bool MobileStore::saveMessage(const MessageRow& row, QString* error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    if (row.message_id == 0) {
        setError(error, QStringLiteral("message_id required"));
        return false;
    }
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO messages (kind, target_id, message_id, client_msg_id,"
            " sender_id, nickname, content, created_at, reply_to_id,"
            " reply_sender_name, reply_content, recalled, msg_type, file_id)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(kind, target_id, message_id) DO UPDATE SET"
            " client_msg_id = excluded.client_msg_id,"
            " sender_id = excluded.sender_id,"
            " nickname = excluded.nickname,"
            " content = excluded.content,"
            " created_at = excluded.created_at,"
            " reply_to_id = excluded.reply_to_id,"
            " reply_sender_name = excluded.reply_sender_name,"
            " reply_content = excluded.reply_content,"
            " recalled = excluded.recalled,"
            " msg_type = excluded.msg_type,"
            " file_id = excluded.file_id"))) {
        setError(error, query.lastError().text());
        return false;
    }
    query.addBindValue(row.kind);
    query.addBindValue(u64Value(row.target_id));
    query.addBindValue(u64Value(row.message_id));
    query.addBindValue(textValue(row.client_msg_id));
    query.addBindValue(u64Value(row.sender_id));
    query.addBindValue(textValue(row.nickname));
    query.addBindValue(textValue(row.recalled ? QString() : row.content));
    query.addBindValue(textValue(row.created_at));
    query.addBindValue(u64Value(row.reply_to_id));
    query.addBindValue(textValue(row.reply_sender_name));
    query.addBindValue(textValue(row.reply_content));
    query.addBindValue(row.recalled ? 1 : 0);
    query.addBindValue(static_cast<int>(row.msg_type));
    query.addBindValue(u64Value(row.file_id));
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool MobileStore::loadRecentMessages(int kind, quint64 target_id, int limit,
                                     QVector<MessageRow>& rows,
                                     QString* error) const {
    rows.clear();
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    if (limit <= 0) {
        return true;
    }
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
            "SELECT kind, target_id, message_id, client_msg_id, sender_id,"
            " nickname, content, created_at, reply_to_id, reply_sender_name,"
            " reply_content, recalled, msg_type, file_id FROM messages"
            " WHERE kind = ? AND target_id = ?"
            " ORDER BY message_id DESC LIMIT ?"))) {
        setError(error, query.lastError().text());
        return false;
    }
    query.addBindValue(kind);
    query.addBindValue(u64Value(target_id));
    query.addBindValue(limit);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    while (query.next()) {
        MessageRow row;
        row.kind = query.value(0).toInt();
        row.target_id = readU64(query, 1);
        row.message_id = readU64(query, 2);
        row.client_msg_id = query.value(3).toString();
        row.sender_id = readU64(query, 4);
        row.nickname = query.value(5).toString();
        row.content = query.value(6).toString();
        row.created_at = query.value(7).toString();
        row.reply_to_id = readU64(query, 8);
        row.reply_sender_name = query.value(9).toString();
        row.reply_content = query.value(10).toString();
        row.recalled = query.value(11).toInt() != 0;
        row.msg_type = static_cast<quint8>(query.value(12).toInt());
        row.file_id = readU64(query, 13);
        rows.push_back(row);
    }
    std::reverse(rows.begin(), rows.end());
    return true;
}

bool MobileStore::markMessageRecalled(int kind, quint64 target_id,
                                      quint64 message_id, QString* error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
            "UPDATE messages SET recalled = 1, content = ''"
            " WHERE kind = ? AND target_id = ? AND message_id = ?"))) {
        setError(error, query.lastError().text());
        return false;
    }
    query.addBindValue(kind);
    query.addBindValue(u64Value(target_id));
    query.addBindValue(u64Value(message_id));
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    QSqlQuery summary(db_);
    if (!summary.prepare(QStringLiteral(
            "UPDATE conversations SET last_content = ''"
            " WHERE kind = ? AND target_id = ? AND last_message_id = ?"))) {
        setError(error, summary.lastError().text());
        return false;
    }
    summary.addBindValue(kind);
    summary.addBindValue(u64Value(target_id));
    summary.addBindValue(u64Value(message_id));
    if (!summary.exec()) {
        setError(error, summary.lastError().text());
        return false;
    }
    return true;
}

bool MobileStore::enqueueOutbox(const OutboxRow& row, QString* error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    if (row.client_msg_id.isEmpty()) {
        setError(error, QStringLiteral("client_msg_id required"));
        return false;
    }
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO outbox (client_msg_id, kind, target_id, content,"
            " created_at, state, retry_count) VALUES (?, ?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(client_msg_id) DO UPDATE SET"
            " kind = excluded.kind, target_id = excluded.target_id,"
            " content = excluded.content, created_at = excluded.created_at,"
            " state = excluded.state, retry_count = excluded.retry_count"))) {
        setError(error, query.lastError().text());
        return false;
    }
    query.addBindValue(textValue(row.client_msg_id));
    query.addBindValue(row.kind);
    query.addBindValue(u64Value(row.target_id));
    query.addBindValue(textValue(row.content));
    query.addBindValue(textValue(row.created_at));
    query.addBindValue(textValue(row.state));
    query.addBindValue(row.retry_count);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool MobileStore::markOutboxSent(const QString& client_msg_id,
                                 quint64 message_id, QString* error) {
    Q_UNUSED(message_id);
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    QSqlQuery query(db_);
    if (!query.prepare(
            QStringLiteral("DELETE FROM outbox WHERE client_msg_id = ?"))) {
        setError(error, query.lastError().text());
        return false;
    }
    query.addBindValue(textValue(client_msg_id));
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool MobileStore::pendingOutbox(QVector<OutboxRow>& rows, QString* error) const {
    rows.clear();
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
            "SELECT client_msg_id, kind, target_id, content, created_at,"
            " state, retry_count FROM outbox WHERE state = 'pending'"
            " ORDER BY created_at ASC, client_msg_id ASC"))) {
        setError(error, query.lastError().text());
        return false;
    }
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    while (query.next()) {
        OutboxRow row;
        row.client_msg_id = query.value(0).toString();
        row.kind = query.value(1).toInt();
        row.target_id = readU64(query, 2);
        row.content = query.value(3).toString();
        row.created_at = query.value(4).toString();
        row.state = query.value(5).toString();
        row.retry_count = query.value(6).toInt();
        rows.push_back(row);
    }
    return true;
}

bool MobileStore::clearOutbox(QString* error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    return execSql(QStringLiteral("DELETE FROM outbox"), error);
}

bool MobileStore::clearAll(QString* error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    if (!db_.transaction()) {
        setError(error, db_.lastError().text());
        return false;
    }
    const QStringList statements = {
        QStringLiteral("DELETE FROM messages"),
        QStringLiteral("DELETE FROM conversations"),
        QStringLiteral("DELETE FROM outbox"),
        QStringLiteral("DELETE FROM meta"),
    };
    for (const QString& statement : statements) {
        if (!execSql(statement, error)) {
            db_.rollback();
            return false;
        }
    }
    if (!db_.commit()) {
        setError(error, db_.lastError().text());
        db_.rollback();
        return false;
    }
    return true;
}

bool MobileStore::setMeta(const QString& key, const QString& value,
                          QString* error) {
    if (!isOpen()) {
        setError(error, QStringLiteral("database not open"));
        return false;
    }
    if (key.isEmpty()) {
        setError(error, QStringLiteral("meta key required"));
        return false;
    }
    QSqlQuery query(db_);
    if (!query.prepare(QStringLiteral(
            "INSERT INTO meta (key, value) VALUES (?, ?)"
            " ON CONFLICT(key) DO UPDATE SET value = excluded.value"))) {
        setError(error, query.lastError().text());
        return false;
    }
    query.addBindValue(textValue(key));
    query.addBindValue(textValue(value));
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

QString MobileStore::meta(const QString& key) const {
    if (!isOpen() || key.isEmpty()) {
        return QString();
    }
    QSqlQuery query(db_);
    if (!query.prepare(
            QStringLiteral("SELECT value FROM meta WHERE key = ?"))) {
        return QString();
    }
    query.addBindValue(textValue(key));
    if (!query.exec() || !query.next()) {
        return QString();
    }
    return query.value(0).toString();
}

}  // namespace clientcore
}  // namespace chatbox

