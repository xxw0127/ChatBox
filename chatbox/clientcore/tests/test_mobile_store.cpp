#include "test_mobile_store.h"

#include <QtTest>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "store/mobile_store.h"

using chatbox::clientcore::ConversationRow;
using chatbox::clientcore::MessageRow;
using chatbox::clientcore::MobileStore;
using chatbox::clientcore::OutboxRow;

namespace {

QString databasePath(const QTemporaryDir& dir) {
    return dir.filePath(QStringLiteral("chatbox.db"));
}

void fillMessage(MessageRow& row, quint64 message_id, const QString& content) {
    row.kind = 2;
    row.target_id = 100;
    row.message_id = message_id;
    row.sender_id = 7;
    row.nickname = QStringLiteral("Alice");
    row.content = content;
    row.created_at = QStringLiteral("2026-10-29 10:00:00");
}

}  // namespace

void TestMobileStore::schemaCreated() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = databasePath(dir);
    MobileStore store;
    QString error;
    QVERIFY2(store.open(path, &error), qPrintable(error));
    QVERIFY(store.isOpen());
    store.close();
    QVERIFY(!store.isOpen());

    const QString connection = QStringLiteral("schema_inspect");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   connection);
        db.setDatabaseName(path);
        QVERIFY2(db.open(), qPrintable(db.lastError().text()));
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE type = 'table'")));
        QStringList tables;
        while (query.next()) {
            tables.push_back(query.value(0).toString());
        }
        QVERIFY(tables.contains(QStringLiteral("conversations")));
        QVERIFY(tables.contains(QStringLiteral("messages")));
        QVERIFY(tables.contains(QStringLiteral("outbox")));
        QVERIFY(tables.contains(QStringLiteral("meta")));
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

void TestMobileStore::conversationUpsert() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MobileStore store;
    QString error;
    QVERIFY2(store.open(databasePath(dir), &error), qPrintable(error));

    ConversationRow row;
    row.kind = 2;
    row.target_id = 100;
    row.title = QStringLiteral("Alice");
    row.last_message_id = 10;
    row.last_content = QStringLiteral("hello");
    row.last_time = QStringLiteral("2026-10-29 10:00:00");
    row.unread = 2;
    QVERIFY2(store.upsertConversation(row, &error), qPrintable(error));

    QVector<ConversationRow> rows;
    QVERIFY2(store.loadConversations(rows, &error), qPrintable(error));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].title, QStringLiteral("Alice"));
    QCOMPARE(rows[0].last_message_id, static_cast<quint64>(10));
    QCOMPARE(rows[0].unread, static_cast<quint32>(2));

    row.last_message_id = 12;
    row.last_content = QStringLiteral("newer");
    row.last_time = QStringLiteral("2026-10-29 10:02:00");
    row.unread = 0;
    QVERIFY2(store.upsertConversation(row, &error), qPrintable(error));

    row.last_message_id = 8;
    row.last_content = QStringLiteral("stale");
    row.last_time = QStringLiteral("2026-10-29 09:00:00");
    row.unread = 5;
    QVERIFY2(store.upsertConversation(row, &error), qPrintable(error));

    QVERIFY2(store.loadConversations(rows, &error), qPrintable(error));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].last_message_id, static_cast<quint64>(12));
    QCOMPARE(rows[0].last_content, QStringLiteral("newer"));
    QCOMPARE(rows[0].unread, static_cast<quint32>(5));
}

void TestMobileStore::messageDedupeByMessageId() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MobileStore store;
    QString error;
    QVERIFY2(store.open(databasePath(dir), &error), qPrintable(error));

    MessageRow row;
    fillMessage(row, 5, QStringLiteral("first"));
    QVERIFY2(store.saveMessage(row, &error), qPrintable(error));
    row.content = QStringLiteral("second");
    row.msg_type = 1;
    row.file_id = 99;
    QVERIFY2(store.saveMessage(row, &error), qPrintable(error));

    QVector<MessageRow> rows;
    QVERIFY2(store.loadRecentMessages(2, 100, 50, rows, &error),
             qPrintable(error));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].content, QStringLiteral("second"));
    QCOMPARE(rows[0].msg_type, static_cast<quint8>(1));
    QCOMPARE(rows[0].file_id, static_cast<quint64>(99));

    row.message_id = 0;
    QVERIFY(!store.saveMessage(row, &error));
    QVERIFY(!error.isEmpty());
}

void TestMobileStore::recentMessagesAscending() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MobileStore store;
    QString error;
    QVERIFY2(store.open(databasePath(dir), &error), qPrintable(error));

    for (quint64 id : {quint64(3), quint64(1), quint64(5), quint64(2), quint64(4)}) {
        MessageRow row;
        fillMessage(row, id, QStringLiteral("m%1").arg(id));
        QVERIFY2(store.saveMessage(row, &error), qPrintable(error));
    }

    QVector<MessageRow> rows;
    QVERIFY2(store.loadRecentMessages(2, 100, 3, rows, &error),
             qPrintable(error));
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows[0].message_id, static_cast<quint64>(3));
    QCOMPARE(rows[1].message_id, static_cast<quint64>(4));
    QCOMPARE(rows[2].message_id, static_cast<quint64>(5));

    QVector<MessageRow> other;
    QVERIFY2(store.loadRecentMessages(2, 200, 10, other, &error),
             qPrintable(error));
    QVERIFY(other.isEmpty());
}

void TestMobileStore::outboxLifecycle() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MobileStore store;
    QString error;
    QVERIFY2(store.open(databasePath(dir), &error), qPrintable(error));

    OutboxRow first;
    first.client_msg_id = QStringLiteral("c1");
    first.kind = 2;
    first.target_id = 100;
    first.content = QStringLiteral("one");
    first.created_at = QStringLiteral("2026-10-29 10:00:00");
    OutboxRow second = first;
    second.client_msg_id = QStringLiteral("c2");
    second.content = QStringLiteral("two");
    second.created_at = QStringLiteral("2026-10-29 10:00:01");
    QVERIFY2(store.enqueueOutbox(first, &error), qPrintable(error));
    QVERIFY2(store.enqueueOutbox(second, &error), qPrintable(error));

    QVector<OutboxRow> rows;
    QVERIFY2(store.pendingOutbox(rows, &error), qPrintable(error));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].client_msg_id, QStringLiteral("c1"));
    QCOMPARE(rows[1].client_msg_id, QStringLiteral("c2"));

    QVERIFY2(store.markOutboxSent(QStringLiteral("c1"), 42, &error),
             qPrintable(error));
    QVERIFY2(store.pendingOutbox(rows, &error), qPrintable(error));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].client_msg_id, QStringLiteral("c2"));
}

void TestMobileStore::metaRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MobileStore store;
    QString error;
    QVERIFY2(store.open(databasePath(dir), &error), qPrintable(error));

    QCOMPARE(store.meta(QStringLiteral("cursor")), QString());
    QVERIFY2(store.setMeta(QStringLiteral("cursor"), QStringLiteral("42"), &error),
             qPrintable(error));
    QCOMPARE(store.meta(QStringLiteral("cursor")), QStringLiteral("42"));
    QVERIFY2(store.setMeta(QStringLiteral("cursor"), QStringLiteral("43"), &error),
             qPrintable(error));
    QCOMPARE(store.meta(QStringLiteral("cursor")), QStringLiteral("43"));
    QVERIFY(!store.setMeta(QString(), QStringLiteral("x"), &error));
}

void TestMobileStore::recalledMessageContentCleared() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MobileStore store;
    QString error;
    QVERIFY2(store.open(databasePath(dir), &error), qPrintable(error));

    MessageRow row;
    fillMessage(row, 9, QStringLiteral("secret"));
    QVERIFY2(store.saveMessage(row, &error), qPrintable(error));
    row.recalled = true;
    QVERIFY2(store.saveMessage(row, &error), qPrintable(error));

    QVector<MessageRow> rows;
    QVERIFY2(store.loadRecentMessages(2, 100, 10, rows, &error),
             qPrintable(error));
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows[0].recalled);
    QVERIFY(rows[0].content.isEmpty());
}

void TestMobileStore::recallClearsConversationPreview() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    MobileStore store;
    QString error;
    QVERIFY2(store.open(databasePath(dir), &error), qPrintable(error));

    MessageRow message;
    fillMessage(message, 9, QStringLiteral("secret"));
    QVERIFY2(store.saveMessage(message, &error), qPrintable(error));
    ConversationRow row;
    row.kind = 2;
    row.target_id = 100;
    row.title = QStringLiteral("Alice");
    row.last_message_id = 9;
    row.last_content = QStringLiteral("secret");
    QVERIFY2(store.upsertConversation(row, &error), qPrintable(error));

    QVERIFY2(store.markMessageRecalled(2, 100, 9, &error), qPrintable(error));
    QVector<MessageRow> messages;
    QVERIFY2(store.loadRecentMessages(2, 100, 10, messages, &error),
             qPrintable(error));
    QCOMPARE(messages.size(), 1);
    QVERIFY(messages[0].recalled);
    QVERIFY(messages[0].content.isEmpty());

    QVector<ConversationRow> conversations;
    QVERIFY2(store.loadConversations(conversations, &error), qPrintable(error));
    QCOMPARE(conversations.size(), 1);
    QVERIFY(conversations[0].last_content.isEmpty());
}
