#ifndef CHATBOX_CLIENT_CORE_CHAT_MESSAGE_H
#define CHATBOX_CLIENT_CORE_CHAT_MESSAGE_H

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <cstdint>

namespace chatbox {
namespace client {

enum class ChatContext {
    kRoom = 0,
    kPrivate = 1,
};

enum class MessageStatus {
    kSending = 0,
    kSent = 1,
    kFailed = 2,
};

enum class MessageType {
    kText = 0,
    kFile = 1,
};

struct FileMeta {
    quint64 file_id = 0;
    QString name;
    quint64 size = 0;
    QString mime;
    QString sha256;
    bool thumb_available = false;
    quint32 width = 0;
    quint32 height = 0;
    quint32 duration_ms = 0;
};

struct ChatMessage {
    QString client_msg_id;
    quint64 message_id = 0;
    ChatContext context = ChatContext::kRoom;
    quint64 target_id = 0;
    quint64 sender_id = 0;
    QString sender_name;
    QString content;
    QDateTime created_at_utc;
    uint64_t reply_to_id = 0;
    QString reply_sender_name;
    QString reply_content;
    bool recalled = false;
    bool outgoing = false;
    MessageStatus status = MessageStatus::kSent;
    MessageType type = MessageType::kText;
    FileMeta file;
    QString transfer_state;
    quint64 transfer_progress = 0;
    QString local_path;
};

// Identifies one rendered conversation bubble: the server message id when
// the message has one, otherwise the client message id of a message that is
// still pending or failed.
struct MessageKey {
    quint64 message_id = 0;
    QString client_msg_id;
    // Filled by the renderer with the bubble surface colour for this theme.
    // ChatView uses it when masking the square table corners into rounded
    // corners; the text document itself has no border-radius support.
    QString bubble_color;

    bool isNull() const { return message_id == 0 && client_msg_id.isEmpty(); }
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_CHAT_MESSAGE_H
