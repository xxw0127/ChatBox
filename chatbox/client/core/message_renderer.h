#ifndef CHATBOX_CLIENT_CORE_MESSAGE_RENDERER_H
#define CHATBOX_CLIENT_CORE_MESSAGE_RENDERER_H

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

#include "core/chat_message.h"

namespace chatbox {
namespace client {

// user_id -> QTextDocument image URL for that sender's avatar.
using AvatarSrcMap = QHash<quint64, QString>;

// The server allows a recall only while the message is younger than this
// (SQL: created_at >= UTC_TIMESTAMP() - INTERVAL 2 MINUTE); the client uses
// the same window so it never offers an action the server would reject.
constexpr int kRecallWindowSeconds = 120;

// Actions offered for a single message by the hover action bar and the
// context menu. 撤回 is only offered for the user's own, not-yet-recalled
// messages that are still inside the recall window.
struct MessageActions {
    bool can_reply = false;
    bool can_recall = false;
    bool can_retry = false;
};

// now_utc is the reference time for the recall window; production callers
// leave it at the current time, tests pass an explicit value.
MessageActions messageActions(const ChatMessage& message, quint64 self_user_id,
                              const QDateTime& now_utc = QDateTime::currentDateTimeUtc());

// Renders one message (and optionally its date separator). The full renderer
// uses this same function so the whole conversation and incremental appends
// produce identical markup.
bool shouldShowSenderName(const ChatMessage* previous, const ChatMessage& current);

QString renderMessageFragment(const ChatMessage& message, quint64 self_user_id,
                              bool include_date_header, const QString& date_label,
                              bool show_sender_name = true,
                              const AvatarSrcMap& avatar_srcs = {});

// Surface colour used by the bubble for the current palette. ChatView also
// uses this when painting rounded corners over the square rich-text table.
QString messageBubbleColor(const ChatMessage& message, quint64 self_user_id);

// Renders the whole conversation as rich text. When rendered_messages is not
// null it receives one key per rendered bubble in document order, so the view
// can map mouse positions back to messages.
QString renderConversationHtml(const QVector<ChatMessage>& messages,
                               quint64 self_user_id,
                               QVector<MessageKey>* rendered_messages = nullptr,
                               const AvatarSrcMap& avatar_srcs = {});

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_MESSAGE_RENDERER_H
