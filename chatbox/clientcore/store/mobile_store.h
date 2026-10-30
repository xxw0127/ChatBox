#ifndef CHATBOX_CLIENTCORE_STORE_MOBILE_STORE_H
#define CHATBOX_CLIENTCORE_STORE_MOBILE_STORE_H

#include <QSqlDatabase>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace chatbox {
namespace clientcore {

struct ConversationRow {
    int kind = 0;
    quint64 target_id = 0;
    QString title;
    QString last_content;
    QString last_time;
    quint64 last_message_id = 0;
    quint32 unread = 0;
};

struct MessageRow {
    int kind = 0;
    quint64 target_id = 0;
    quint64 message_id = 0;
    quint64 sender_id = 0;
    quint64 reply_to_id = 0;
    quint64 file_id = 0;
    QString client_msg_id;
    QString nickname;
    QString content;
    QString created_at;
    QString reply_sender_name;
    QString reply_content;
    bool recalled = false;
    quint8 msg_type = 0;
};

struct OutboxRow {
    QString client_msg_id;
    QString content;
    QString created_at;
    QString state = QStringLiteral("pending");
    int kind = 0;
    quint64 target_id = 0;
    int retry_count = 0;
};

// Local SQLite mirror owned by the service process. All statements are
// prepared; schema creation is idempotent and enables WAL.
class MobileStore {
public:
    MobileStore();
    ~MobileStore();

    MobileStore(const MobileStore&) = delete;
    MobileStore& operator=(const MobileStore&) = delete;

    bool open(const QString& path, QString* error);
    void close();
    bool isOpen() const;

    bool upsertConversation(const ConversationRow& row, QString* error);
    bool loadConversations(QVector<ConversationRow>& rows, QString* error) const;
    bool saveMessage(const MessageRow& row, QString* error);
    bool loadRecentMessages(int kind, quint64 target_id, int limit,
                            QVector<MessageRow>& rows, QString* error) const;
    bool markMessageRecalled(int kind, quint64 target_id, quint64 message_id,
                             QString* error);
    bool enqueueOutbox(const OutboxRow& row, QString* error);
    bool markOutboxSent(const QString& client_msg_id, quint64 message_id,
                        QString* error);
    bool pendingOutbox(QVector<OutboxRow>& rows, QString* error) const;
    bool clearOutbox(QString* error);
    // Removes messages, conversations, outbox and meta for account switches.
    bool clearAll(QString* error);
    bool setMeta(const QString& key, const QString& value, QString* error);
    QString meta(const QString& key) const;

private:
    bool execSql(const QString& sql, QString* error) const;
    static void setError(QString* error, const QString& text);

    QSqlDatabase db_;
    QString connection_name_;
};

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_STORE_MOBILE_STORE_H
