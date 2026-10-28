#ifndef CHATBOX_CLIENT_UI_CHAT_VIEW_H
#define CHATBOX_CLIENT_UI_CHAT_VIEW_H

#include <QHash>
#include <QImage>
#include <QRect>
#include <QTextBrowser>
#include <QVector>

#include "core/chat_message.h"

class BubbleOverlay;
class QKeyEvent;
class QPainter;
class QTextTable;

// Read-only conversation view. Besides rendering the conversation it maps
// viewport positions back to messages so the window can offer a per-message
// context menu. After every render it also normalises link styling (theme
// blue, no underline).
class ChatView : public QTextBrowser {
    Q_OBJECT
    friend class BubbleOverlay;

public:
    explicit ChatView(QWidget* parent = nullptr);

    // Replaces the document and rebuilds the position -> message map.
    void setConversationHtml(
        const QString& html,
        const QVector<chatbox::client::MessageKey>& message_keys,
        const QHash<QString, QImage>& avatar_images = {});

    // Appends one rendered message at the end without rebuilding the document.
    // Existing ranges keep their positions because nothing before them moves.
    void appendFragment(const QString& html, const chatbox::client::MessageKey& key,
                        const QHash<QString, QImage>& avatar_images = {});

    // Clears the document and all message ranges.
    void clearConversation();

    // Server message id at a viewport position, or 0 when the position is
    // not inside a mapped message bubble or the bubble has no server id yet.
    quint64 messageIdAt(const QPoint& viewport_pos) const;

    // Full key (server id or client message id) of the bubble at a viewport
    // position; false when the position is not on a message.
    bool messageKeyAt(const QPoint& viewport_pos,
                      chatbox::client::MessageKey* key) const;

    // Rect (viewport coordinates) of the message's meta line (time/status),
    // used to place the hover action bar. Null when the id is unknown.
    QRect messageAnchorRect(quint64 message_id) const;

    // Rect (viewport coordinates) of the message's bubble itself. Used to
    // place the hover action bar next to the bubble. Null when the id is
    // unknown or the document structure does not expose the bubble.
    QRect messageBubbleRect(quint64 message_id) const;

    // Outer 1px-bordered table rect (viewport coordinates), including the
    // rounded-corner painting bounds. Primarily useful for visual guards.
    QRect messageOuterBubbleRect(quint64 message_id) const;

signals:
    void messageMenuRequested(quint64 message_id, const QString& client_msg_id,
                              const QPoint& global_pos);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct MessageRange {
        int first = 0;
        int last = 0;
        chatbox::client::MessageKey key;
        QTextTable* frame = nullptr;          // top-level row table
        QTextTable* bubble_wrapper = nullptr; // 1px surface band around the bubble
        QTextTable* bubble = nullptr;         // coloured bubble table
        QString bubble_color;
    };

    void buildMessageRanges(const QVector<chatbox::client::MessageKey>& message_keys);
    void registerAvatarImages(const QHash<QString, QImage>& avatar_images);
    QRect outerBubbleRect(const MessageRange& range) const;
    void paintRoundedBubbleCorners(QPainter& painter);
    bool messageKeyAtPosition(int position,
                              chatbox::client::MessageKey* key) const;
    void styleAnchors();
    void styleAnchorsInRange(int start, int end);

    QVector<MessageRange> message_ranges_;
    QWidget* bubble_overlay_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_CHAT_VIEW_H
