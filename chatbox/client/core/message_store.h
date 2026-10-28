#ifndef CHATBOX_CLIENT_CORE_MESSAGE_STORE_H
#define CHATBOX_CLIENT_CORE_MESSAGE_STORE_H

#include <QDateTime>
#include <QString>
#include <QVector>

#include "core/chat_message.h"

namespace chatbox {
namespace client {

class HistoryStore;

class MessageStore {
public:
    // 挂接本地历史库：只做写穿透（把已确认消息落库），指针不归本对象所有，
    // 也不负责关闭；传 nullptr 表示纯内存模式，行为与未挂接时完全一致。
    void setHistoryStore(HistoryStore* store);

    void reset(ChatContext context, quint64 target_id);
    void replaceHistory(const QVector<ChatMessage>& history);
    void prependHistory(const QVector<ChatMessage>& history);
    // Returns false for a duplicate message that was already stored.
    bool appendIncoming(const ChatMessage& message);
    void addPending(const ChatMessage& message);

    bool markSending(const QString& client_msg_id);
    bool markSent(const QString& client_msg_id, quint64 message_id,
                  const QDateTime& created_at_utc);
    bool markFailed(const QString& client_msg_id);
    bool updateFileTransfer(const QString& client_msg_id,
                            const QString& state, quint64 progress);
    bool confirmFileUpload(const QString& client_msg_id, quint64 message_id,
                           quint64 file_id, const QDateTime& created_at_utc);
    bool removeByClientId(const QString& client_msg_id);
    int markAllSendingFailed();
    bool markRecalled(quint64 message_id);
    bool markFileDeleted(quint64 message_id);
    bool updateFileMetadata(quint64 message_id, const FileMeta& meta);
    const ChatMessage* findMessage(quint64 message_id) const;
    const ChatMessage* findByClientId(const QString& client_msg_id) const;

    bool isCurrent(ChatContext context, quint64 target_id) const;
    const QVector<ChatMessage>& messages() const;

private:
    int indexOfClientId(const QString& client_msg_id) const;
    bool hasMessageId(quint64 message_id) const;
    // 把内存里的消息按 message_id 升序重排：已确认的消息（message_id > 0）
    // 从旧到新排列，未确认的待发/失败消息（message_id == 0）统一排在最后，
    // 并保持它们之间的相对顺序不变。
    void sortMessagesChronologically();
    // 写穿透：把一条服务端已确认的消息写进本地历史库。
    // 未挂接历史库、消息未确认（message_id == 0）或状态不是 kSent 时直接跳过。
    // 写库失败只记日志，不影响内存状态与界面。
    void persistMessage(const ChatMessage& message) const;

    ChatContext context_ = ChatContext::kRoom;
    quint64 target_id_ = 0;
    QVector<ChatMessage> messages_;
    HistoryStore* history_ = nullptr;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_MESSAGE_STORE_H
