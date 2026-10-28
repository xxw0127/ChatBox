#include "ui/chat_view.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QContextMenuEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

namespace {

// The renderer wraps each message in a top-level row table. The row may hold
// an avatar cell next to a content cell; the content cell holds the 1px
// surface band (bubble_wrapper) and, inside it, the coloured bubble table.
void findBubbleTables(QTextTable* outer, QTextTable** wrapper, QTextTable** bubble) {
    if (wrapper != nullptr) {
        *wrapper = nullptr;
    }
    if (bubble != nullptr) {
        *bubble = nullptr;
    }
    if (outer == nullptr) {
        return;
    }
    for (int row = 0; row < outer->rows(); ++row) {
        for (int col = 0; col < outer->columns(); ++col) {
            const QTextTableCell cell = outer->cellAt(row, col);
            for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it) {
                QTextTable* inner = qobject_cast<QTextTable*>(it.currentFrame());
                if (inner == nullptr) {
                    continue;
                }
                if (wrapper != nullptr) {
                    *wrapper = inner;
                }
                if (bubble != nullptr && inner->rows() > 0 && inner->columns() > 0) {
                    const QTextTableCell bubble_cell = inner->cellAt(0, 0);
                    for (QTextFrame::iterator jt = bubble_cell.begin(); !jt.atEnd();
                         ++jt) {
                        QTextTable* candidate =
                            qobject_cast<QTextTable*>(jt.currentFrame());
                        if (candidate != nullptr) {
                            *bubble = candidate;
                            break;
                        }
                    }
                }
                return;
            }
        }
    }
}

}  // namespace

// A transparent overlay above the viewport. It paints after the rich-text
// document has rendered, so it can erase the square table corners without
// fighting the QTextBrowser paint order.
class BubbleOverlay : public QWidget {
public:
    explicit BubbleOverlay(ChatView* owner) : QWidget(owner), owner_(owner) {
        setObjectName(QStringLiteral("BubbleOverlay"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        owner_->paintRoundedBubbleCorners(painter);
    }

private:
    ChatView* owner_ = nullptr;
};

ChatView::ChatView(QWidget* parent) : QTextBrowser(parent) {
    // QTextBrowser defaults to ClickFocus; keyboard users need to reach the
    // message menu through the normal Tab chain.
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName("聊天记录");
    bubble_overlay_ = new BubbleOverlay(this);
    bubble_overlay_->setGeometry(viewport()->geometry());
    bubble_overlay_->show();
    bubble_overlay_->raise();
    connect(verticalScrollBar(), &QScrollBar::valueChanged, bubble_overlay_,
            [this] { bubble_overlay_->update(); });
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, bubble_overlay_,
            [this] { bubble_overlay_->update(); });
}

void ChatView::setConversationHtml(
    const QString& html,
    const QVector<chatbox::client::MessageKey>& message_keys,
    const QHash<QString, QImage>& avatar_images) {
    registerAvatarImages(avatar_images);
    setHtml(html);
    styleAnchors();
    buildMessageRanges(message_keys);
    if (bubble_overlay_ != nullptr) {
        bubble_overlay_->update();
    }
}

void ChatView::registerAvatarImages(
    const QHash<QString, QImage>& avatar_images) {
    QTextDocument* doc = document();
    if (doc == nullptr) {
        return;
    }
    // Resources must exist before the HTML is parsed; otherwise the first
    // layout paints a broken-image placeholder and never retries.
    for (auto it = avatar_images.constBegin(); it != avatar_images.constEnd(); ++it) {
        doc->addResource(QTextDocument::ImageResource, QUrl(it.key()), it.value());
    }
}

void ChatView::appendFragment(const QString& html,
                              const chatbox::client::MessageKey& key,
                              const QHash<QString, QImage>& avatar_images) {
    QTextDocument* doc = document();
    if (doc == nullptr || html.isEmpty()) {
        return;
    }

    registerAvatarImages(avatar_images);

    // Appending at the document end cannot move any existing range: all old
    // positions stay before the insertion point. This is the safety property
    // that lets the incremental path skip buildMessageRanges().
    QTextCursor cursor(doc);
    cursor.movePosition(QTextCursor::End);
    const int start = cursor.position();
    cursor.insertHtml(html);
    const int end = cursor.position();

    styleAnchorsInRange(start, end);

    QTextTable* table = nullptr;
    const QTextFrame* root = doc->rootFrame();
    if (root != nullptr) {
        for (QTextFrame::iterator it = root->begin(); !it.atEnd(); ++it) {
            auto* candidate = qobject_cast<QTextTable*>(it.currentFrame());
            if (candidate != nullptr && candidate->lastPosition() > start) {
                table = candidate;
            }
        }
    }
    if (table != nullptr) {
        MessageRange range;
        range.first = table->firstPosition();
        range.last = table->lastPosition();
        range.key = key;
        range.frame = table;
        findBubbleTables(table, &range.bubble_wrapper, &range.bubble);
        range.bubble_color = key.bubble_color;
        message_ranges_.append(range);
    }
    if (bubble_overlay_ != nullptr) {
        bubble_overlay_->update();
    }
}

void ChatView::clearConversation() {
    clear();
    message_ranges_.clear();
    if (bubble_overlay_ != nullptr) {
        bubble_overlay_->update();
    }
}

quint64 ChatView::messageIdAt(const QPoint& viewport_pos) const {
    chatbox::client::MessageKey key;
    if (!messageKeyAt(viewport_pos, &key)) {
        return 0;
    }
    return key.message_id;
}

bool ChatView::messageKeyAt(const QPoint& viewport_pos,
                            chatbox::client::MessageKey* key) const {
    if (message_ranges_.isEmpty()) {
        return false;
    }
    return messageKeyAtPosition(cursorForPosition(viewport_pos).position(), key);
}

bool ChatView::messageKeyAtPosition(int position,
                                    chatbox::client::MessageKey* key) const {
    for (const MessageRange& range : message_ranges_) {
        if (position >= range.first && position < range.last) {
            if (key != nullptr) {
                *key = range.key;
            }
            return true;
        }
    }
    return false;
}

QRect ChatView::messageAnchorRect(quint64 message_id) const {
    if (message_id == 0) {
        return QRect();
    }
    for (const MessageRange& range : message_ranges_) {
        if (range.key.message_id != message_id) {
            continue;
        }
        const QTextBlock block =
            document()->findBlock(qMax(range.first, range.last - 1));
        if (!block.isValid()) {
            return QRect();
        }
        QTextCursor cursor(block);
        cursor.movePosition(QTextCursor::EndOfBlock);
        return cursorRect(cursor);
    }
    return QRect();
}

QRect ChatView::outerBubbleRect(const MessageRange& range) const {
    QTextTable* target =
        range.bubble_wrapper != nullptr ? range.bubble_wrapper : range.frame;
    if (target == nullptr || document() == nullptr ||
        document()->documentLayout() == nullptr) {
        return QRect();
    }
    return document()->documentLayout()
        ->frameBoundingRect(target)
        .toRect()
        .translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
}

void ChatView::paintRoundedBubbleCorners(QPainter& painter) {
    if (message_ranges_.isEmpty()) {
        return;
    }
    constexpr qreal kRadius = 12.0;
    const QColor canvas(theme::kSurfaceSoft);
    const QColor border(theme::kBubbleBorder);
    const QRect viewport_rect = viewport()->rect();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const MessageRange& range : message_ranges_) {
        if (range.bubble_color.isEmpty()) {
            continue;
        }
        QRect rect = outerBubbleRect(range);
        if (rect.isEmpty() || !rect.intersects(viewport_rect)) {
            continue;
        }
        // A right-aligned table sits exactly on the document's right edge;
        // its 1px border is then clipped by the viewport. Pull the painted
        // rect in by one pixel so the right stroke remains visible.
        const bool clipped_right = rect.right() > viewport_rect.right();
        if (clipped_right) {
            rect.setRight(viewport_rect.right() - 1);
        }

        // Qt's rich-text tables are square. Erase the four corner wedges with
        // the transcript background, then redraw the border as a rounded
        // rectangle so the bubble reads as a chat bubble rather than a table.
        QPainterPath square;
        square.addRect(QRectF(rect));
        QPainterPath rounded;
        rounded.addRoundedRect(QRectF(rect), kRadius, kRadius);
        painter.fillPath(square.subtracted(rounded), canvas);

        QPen pen(border);
        pen.setWidthF(1.0);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5),
                                kRadius, kRadius);
        if (clipped_right) {
            // Remove the 1px table-fill sliver that would otherwise sit to
            // the right of the new border at the viewport edge.
            painter.fillRect(QRect(viewport_rect.right(), rect.top(), 1,
                                   rect.height()),
                             canvas);
        }
    }
    painter.restore();
}

QRect ChatView::messageOuterBubbleRect(quint64 message_id) const {
    if (message_id == 0) {
        return QRect();
    }
    for (const MessageRange& range : message_ranges_) {
        if (range.key.message_id == message_id) {
            return outerBubbleRect(range);
        }
    }
    return QRect();
}

QRect ChatView::messageBubbleRect(quint64 message_id) const {
    if (message_id == 0) {
        return QRect();
    }
    for (const MessageRange& range : message_ranges_) {
        if (range.key.message_id != message_id) {
            continue;
        }
        QTextTable* bubble =
            range.bubble != nullptr ? range.bubble : range.bubble_wrapper;
        if (bubble == nullptr || document() == nullptr ||
            document()->documentLayout() == nullptr) {
            return QRect();
        }
        const QRect document_rect =
            document()->documentLayout()->frameBoundingRect(bubble).toRect();
        return document_rect.translated(-horizontalScrollBar()->value(),
                                        -verticalScrollBar()->value());
    }
    return QRect();
}

void ChatView::resizeEvent(QResizeEvent* event) {
    QTextBrowser::resizeEvent(event);
    if (bubble_overlay_ != nullptr) {
        bubble_overlay_->setGeometry(viewport()->geometry());
        bubble_overlay_->update();
    }
}

void ChatView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F10 || event->key() == Qt::Key_Menu) {
        const QTextCursor cursor = textCursor();
        if (cursor.hasSelection()) {
            chatbox::client::MessageKey key;
            if (messageKeyAtPosition(cursor.position(), &key)) {
                QRect anchor = messageAnchorRect(key.message_id);
                if (anchor.isNull()) {
                    anchor = cursorRect(cursor);
                }
                const QPoint global_pos = viewport()->mapToGlobal(anchor.center());
                emit messageMenuRequested(key.message_id, key.client_msg_id, global_pos);
                event->accept();
                return;
            }
        }
    }
    QTextBrowser::keyPressEvent(event);
}

void ChatView::contextMenuEvent(QContextMenuEvent* event) {
    // Ask the window for a menu when a message bubble is under the cursor;
    // anywhere else keeps the standard copy/select-all menu.
    chatbox::client::MessageKey key;
    if (messageKeyAt(viewport()->mapFromGlobal(event->globalPos()), &key)) {
        emit messageMenuRequested(key.message_id, key.client_msg_id,
                                  event->globalPos());
        event->accept();
        return;
    }
    QTextBrowser::contextMenuEvent(event);
}

void ChatView::buildMessageRanges(
    const QVector<chatbox::client::MessageKey>& message_keys) {
    message_ranges_.clear();
    const QTextFrame* root = document() != nullptr ? document()->rootFrame() : nullptr;
    if (root == nullptr || message_keys.isEmpty()) {
        return;
    }
    QVector<QTextTable*> frames;
    for (QTextFrame::iterator it = root->begin(); !it.atEnd(); ++it) {
        QTextTable* table = qobject_cast<QTextTable*>(it.currentFrame());
        if (table != nullptr) {
            frames.append(table);
        }
    }
    if (frames.size() != message_keys.size()) {
        // The document structure and the message list disagree; disable the
        // mapping instead of offering actions for the wrong message.
        return;
    }
    message_ranges_.reserve(frames.size());
    for (int i = 0; i < frames.size(); ++i) {
        MessageRange range;
        range.first = frames.at(i)->firstPosition();
        range.last = frames.at(i)->lastPosition();
        range.key = message_keys.at(i);
        range.frame = frames.at(i);
        findBubbleTables(range.frame, &range.bubble_wrapper, &range.bubble);
        range.bubble_color = message_keys.at(i).bubble_color;
        message_ranges_.append(range);
    }
}

void ChatView::styleAnchors() {
    QTextDocument* doc = document();
    if (doc == nullptr) {
        return;
    }
    styleAnchorsInRange(0, doc->characterCount());
}

void ChatView::styleAnchorsInRange(int start, int end) {
    QTextDocument* doc = document();
    if (doc == nullptr || start >= end) {
        return;
    }
    // Collect first: merging formats invalidates block/fragment iterators.
    QVector<QPair<int, int>> ranges;
    for (QTextBlock block = doc->findBlock(qMax(0, start));
         block.isValid() && block.position() < end; block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid() && fragment.position() >= start &&
                fragment.position() < end && fragment.charFormat().isAnchor()) {
                ranges.append(qMakePair(fragment.position(), fragment.length()));
            }
        }
    }
    if (ranges.isEmpty()) {
        return;
    }
    QTextCharFormat link_format;
    link_format.setForeground(QColor(theme::kAccent));
    link_format.setFontUnderline(false);
    for (const QPair<int, int>& range : ranges) {
        QTextCursor cursor(doc);
        cursor.setPosition(range.first);
        cursor.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(link_format);
    }
}
