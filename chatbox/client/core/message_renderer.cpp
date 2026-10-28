#include "core/message_renderer.h"

#include <QStringList>

#include "core/identity.h"
#include "core/message_time.h"
#include "core/ui_theme.h"

namespace chatbox {
namespace client {

namespace {

QString senderLabel(const ChatMessage& message) {
    if (!message.sender_name.isEmpty()) {
        return message.sender_name;
    }
    return QString::number(message.sender_id);
}

int visualTextUnits(const QString& text) {
    int units = 0;
    for (const QChar ch : text) {
        // CJK/emoji glyphs occupy roughly two Latin character widths at the
        // same font size. Counting units keeps the width cap deterministic
        // without laying out the document twice.
        units += ch.unicode() >= 0x2E80 ? 2 : 1;
    }
    return units;
}

bool messageNeedsWidthCap(const ChatMessage& message) {
    constexpr int kWideMessageUnits = 88;
    constexpr int kQuoteUnits = 40;
    int units = visualTextUnits(message.content);
    int quote_units = 0;
    if (message.reply_to_id != 0) {
        quote_units = visualTextUnits(message.reply_content);
        units += quote_units;
    }
    // A quote with anything longer than a short phrase should be bounded even
    // when the reply body itself is one word; otherwise a long quoted ID can
    // stretch the entire bubble to the transcript width.
    return units > kWideMessageUnits ||
           (message.reply_to_id != 0 && quote_units > kQuoteUnits);
}

QString messageMeta(const ChatMessage& message) {
    QString meta = formatLocalTime(message.created_at_utc);
    switch (message.status) {
        case MessageStatus::kSending:
            meta += "  发送中...";
            break;
        case MessageStatus::kFailed:
            meta += "  <span style=\"color: " +
                    QString::fromLatin1(theme::kSemanticDown) + ";\">发送失败</span> <a href=\"retry:" +
                    message.client_msg_id.toHtmlEscaped() +
                    "\" style=\"color: " + QString::fromLatin1(theme::kAccent) +
                    "; text-decoration: none;\">重发</a>";
            break;
        case MessageStatus::kSent:
            break;
    }
    return meta;
}

}  // namespace

QString avatarSrc(quint64 user_id, quint32 version) {
    return QStringLiteral("chatbox-avatar://%1?v=%2").arg(user_id).arg(version);
}

MessageActions messageActions(const ChatMessage& message, quint64 self_user_id,
                              const QDateTime& now_utc) {
    MessageActions actions;
    const bool outgoing =
        message.outgoing || (message.sender_id != 0 && message.sender_id == self_user_id);
    if (message.status == MessageStatus::kFailed && !message.client_msg_id.isEmpty()) {
        actions.can_retry = true;
    }
    if (message.message_id != 0 && !message.recalled) {
        actions.can_reply = true;
        const bool within_window =
            message.created_at_utc.isValid() &&
            message.created_at_utc.secsTo(now_utc) <= kRecallWindowSeconds;
        actions.can_recall = outgoing && within_window;
    }
    return actions;
}

QString messageBubbleColor(const ChatMessage& message, quint64 self_user_id) {
    const bool outgoing =
        message.outgoing || (message.sender_id != 0 && message.sender_id == self_user_id);
    if (message.recalled) {
        return QString::fromLatin1(theme::kSurfaceStrong);
    }
    return QString::fromLatin1(outgoing ? theme::kBubbleOwn : theme::kBubbleOther);
}

bool shouldShowSenderName(const ChatMessage* previous, const ChatMessage& current) {
    if (previous == nullptr || current.recalled) {
        return false;
    }
    if (previous->sender_id != current.sender_id) {
        return true;
    }
    if (!previous->created_at_utc.isValid() || !current.created_at_utc.isValid()) {
        return true;
    }
    const qint64 seconds =
        previous->created_at_utc.secsTo(current.created_at_utc);
    return seconds < 0 || seconds >= 180;
}

QString renderMessageFragment(const ChatMessage& message, quint64 self_user_id,
                              bool include_date_header, const QString& date_label,
                              bool show_sender_name, const AvatarSrcMap& avatar_srcs) {
    QString html;
    if (include_date_header) {
        html += "<div align=\"center\" style=\"color: " +
                QString::fromLatin1(theme::kMuted) +
                "; font-size: 11px; margin: 16px 0;\">" +
                date_label.toHtmlEscaped() + "</div>";
    }

    const bool outgoing =
        message.outgoing || (message.sender_id != 0 && message.sender_id == self_user_id);
    const QString alignment = outgoing ? "right" : "left";
    const QString background = messageBubbleColor(message, self_user_id);
    const QString quote_background = QString::fromLatin1(theme::kQuoteBg);

    QString content = message.recalled ? "消息已撤回" : message.content.toHtmlEscaped();
    content.replace("\n", "<br>");

    const QString avatar_src = avatar_srcs.value(message.sender_id);
    const QString avatar_cell =
        avatar_src.isEmpty()
            ? QString()
            : "<td valign=\"top\"><img src=\"" + avatar_src.toHtmlEscaped() +
                  "\" width=\"36\" height=\"36\"></td>";

    const int top_margin = show_sender_name ? 12 : 4;
    html += "<div style=\"margin-top: " + QString::number(top_margin) + "px;\">";
    // Once content would exceed roughly 620px at 14px, pin the whole row to
    // 72% of the viewport; the avatar column takes its 36px out of that budget.
    const QString width_attr =
        messageNeedsWidthCap(message) ? " width=\"72%\"" : QString();
    html += "<table align=\"" + alignment + "\"" + width_attr +
            " cellspacing=\"0\" cellpadding=\"4\"><tr>";
    if (!outgoing) {
        html += avatar_cell;
    }
    html += "<td>";
    if (!outgoing && !message.recalled && show_sender_name) {
        QString identity = senderLabel(message).toHtmlEscaped();
        if (message.sender_id != 0) {
            identity += "&nbsp;&nbsp;<span style=\"color: " +
                        QString::fromLatin1(theme::kMuted) +
                        "; font-size: 11px;\">" +
                        publicUserId(message.sender_id).toHtmlEscaped() + "</span>";
        }
        html += "<div style=\"color: " + QString::fromLatin1(theme::kBody) +
                "; font-size: 12px; margin-bottom: 2px;\">" + identity + "</div>";
    }
    // Nested tables are the only reliable way to create the 1px inset gap in
    // Qt's rich-text engine (border-radius and box-shadow are unsupported).
    // The visible border is painted by ChatView; a square bgcolor here would
    // leave a frame outside the corners.
    html += "<table cellspacing=\"0\" cellpadding=\"1\" bgcolor=\"" +
            QString::fromLatin1(theme::kSurfaceSoft) + "\"><tr><td>";
    html += "<table cellspacing=\"0\" cellpadding=\"12\" bgcolor=\"" + background +
            "\"><tr><td>";
    if (message.reply_to_id != 0) {
        QString quote = message.reply_content.toHtmlEscaped();
        quote.replace("\n", "<br>");
        if (quote.isEmpty()) {
            quote = "消息已撤回";
        }
        html += "<table cellspacing=\"0\" cellpadding=\"6\" bgcolor=\"" +
                quote_background + "\"><tr><td><div style=\"color: " +
                QString::fromLatin1(theme::kMuted) +
                "; font-size: 12px;\">回复 " +
                (message.reply_sender_name.isEmpty()
                     ? QString()
                     : message.reply_sender_name.toHtmlEscaped() + ": ") +
                quote + "</div></td></tr></table>";
        html += "<div style=\"font-size: 6px; line-height: 6px;\">&nbsp;</div>";
    }
    if (message.recalled) {
        html += "<div style=\"font-size: 14px; color: " +
                QString::fromLatin1(theme::kBubbleMeta) + ";\"><i>" + content +
                "</i></div>";
    } else {
        // 110% keeps the last line's leading close to the bottom padding; at
        // 150% the extra half-line made short bubbles look top-aligned.
        html += "<div style=\"font-size: 14px; line-height: 110%;\">" + content +
                "</div>";
    }
    html += "</td></tr></table>";
    html += "</td></tr></table>";
    html += "</td>";
    if (outgoing) {
        html += avatar_cell;
    }
    html += "</tr></table>";
    html += "<div style=\"color: " + QString::fromLatin1(theme::kBubbleMeta) +
            "; font-size: 11px; margin: 4px 4px 8px 4px; text-align: " +
            (outgoing ? "right" : "left") + ";\">" + messageMeta(message) + "</div>";
    html += "</div>";
    return html;
}

QString renderConversationHtml(const QVector<ChatMessage>& messages,
                               quint64 self_user_id,
                               QVector<MessageKey>* rendered_messages,
                               const AvatarSrcMap& avatar_srcs) {
    if (rendered_messages != nullptr) {
        rendered_messages->clear();
    }
    QStringList html;
    html << "<html><body style=\"font-family: 'Microsoft YaHei', sans-serif;\">";

    QString current_date;
    for (int index = 0; index < messages.size(); ++index) {
        const ChatMessage& message = messages.at(index);
        if (rendered_messages != nullptr) {
            MessageKey key;
            key.message_id = message.message_id;
            key.client_msg_id = message.client_msg_id;
            key.bubble_color = messageBubbleColor(message, self_user_id);
            rendered_messages->append(key);
        }
        const QString date = formatLocalDate(message.created_at_utc);
        const bool include_date_header = date != current_date;
        if (include_date_header) {
            current_date = date;
        }
        const ChatMessage* previous = index > 0 ? &messages.at(index - 1) : nullptr;
        const bool show_sender_name =
            include_date_header || shouldShowSenderName(previous, message);
        html << renderMessageFragment(message, self_user_id, include_date_header, date,
                                      show_sender_name, avatar_srcs);
    }

    html << "</body></html>";
    return html.join(QString());
}

}  // namespace client
}  // namespace chatbox
