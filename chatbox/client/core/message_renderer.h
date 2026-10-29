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

// URL used by the renderer for a sender's avatar; version 0 means the
// fallback initial image.
QString avatarSrc(quint64 user_id, quint32 version);

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

// Messages from the same sender inside this window form one visual run: only
// the first bubble carries the sender name and avatar.
constexpr int kMessageGroupGapSeconds = 300;

// True when the conversation, a calendar-day change, or a gap of at least
// kMessageGroupGapSeconds calls for a centered time separator.
bool shouldShowTimeSeparator(const ChatMessage* previous,
                             const ChatMessage& current);

// Label for the centered separator: HH:mm today, MM-dd HH:mm on an older
// date, and yyyy-MM-dd HH:mm when the year differs.
QString timeSeparatorLabel(const ChatMessage& message);

// Rendering decisions for one message relative to the previous bubble.
struct MessageGrouping {
    bool show_separator = false;
    QString separator_label;
    bool show_sender_name = false;
};

// One source of truth for the full render and incremental appends. previous
// must point at the message before current, not at current itself.
MessageGrouping messageGrouping(const ChatMessage* previous,
                                const ChatMessage& current);

// Renders one message (and optionally its centered time separator). The full
// renderer uses this same function so the whole conversation and incremental
// appends produce identical markup.
bool shouldShowSenderName(const ChatMessage* previous, const ChatMessage& current);

QString renderMessageFragment(const ChatMessage& message, quint64 self_user_id,
                              bool include_time_separator,
                              const QString& separator_label,
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
