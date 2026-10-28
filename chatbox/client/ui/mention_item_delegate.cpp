#include "ui/mention_item_delegate.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

#include "core/avatar_utils.h"
#include "core/ui_theme.h"

namespace theme = chatbox::client::theme;  // Coinbase palette tokens

MentionItemDelegate::MentionItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {}

void MentionItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    QRect row = option.rect.adjusted(4, 2, -4, -2);
    if (selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(theme::kSelectionBg));
        painter->drawRoundedRect(row, 8, 8);
    } else if (hovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(theme::kSurfaceSoft));
        painter->drawRoundedRect(row, 8, 8);
    }

    const quint64 user_id = index.data(kUserIdRole).toULongLong();
    const QString name = index.data(kNameRole).toString();
    const QString public_id = index.data(kPublicIdRole).toString();
    const bool online = index.data(kOnlineRole).toBool();

    const int avatar_size = 28;
    const QRect avatar_rect(row.left() + 8, row.center().y() - avatar_size / 2,
                            avatar_size, avatar_size);
    const QPixmap avatar = index.data(kAvatarRole).value<QPixmap>();
    if (!avatar.isNull()) {
        QPainterPath clip;
        clip.addEllipse(avatar_rect);
        painter->save();
        painter->setClipPath(clip);
        painter->drawPixmap(avatar_rect, avatar);
        painter->restore();
    } else if (user_id == 0) {
        // "所有人": brand room badge instead of a letter.
        painter->drawPixmap(avatar_rect,
                            QPixmap::fromImage(
                                chatbox::client::roomAvatar(avatar_size)));
    } else {
        const QColor avatar_bg = chatbox::client::fallbackAvatarBackground(user_id);
        painter->setPen(Qt::NoPen);
        painter->setBrush(avatar_bg);
        painter->drawEllipse(avatar_rect);
        QFont avatar_font = painter->font();
        avatar_font.setPixelSize(13);
        avatar_font.setBold(true);
        painter->setFont(avatar_font);
        painter->setPen(chatbox::client::readableForeground(avatar_bg));
        painter->drawText(avatar_rect, Qt::AlignCenter,
                          name.isEmpty() ? QString("?") : name.left(1).toUpper());
    }

    if (online || user_id == 0) {
        painter->setBrush(QColor(theme::kOnlineDot));
        painter->setPen(QPen(QColor(theme::kSurfaceSoft), 2));
        painter->drawEllipse(QPoint(avatar_rect.right() - 1, avatar_rect.bottom() - 1),
                             4, 4);
    }

    const QColor primary =
        selected ? QColor(theme::kOnPrimary) : QColor(theme::kInk);
    QColor secondary(theme::kMuted);
    if (selected) {
        secondary = QColor(theme::kOnPrimary);
        secondary.setAlpha(190);
    }

    const int text_left = avatar_rect.right() + 10;
    const int text_right = row.right() - 8;
    QFont name_font = painter->font();
    name_font.setPixelSize(13);
    name_font.setBold(true);
    painter->setFont(name_font);
    painter->setPen(primary);
    const QRect name_rect =
        user_id == 0 ? QRect(text_left, row.top(), text_right - text_left,
                             row.height())
                     : QRect(text_left, row.top() + 6, text_right - text_left, 18);
    painter->drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                      QFontMetrics(name_font).elidedText(name, Qt::ElideRight,
                                                         name_rect.width()));

    if (user_id != 0) {
        QFont id_font = painter->font();
        id_font.setPixelSize(11);
        id_font.setBold(false);
        painter->setFont(id_font);
        painter->setPen(secondary);
        const QRect id_rect(text_left, row.top() + 24, text_right - text_left, 15);
        painter->drawText(id_rect, Qt::AlignLeft | Qt::AlignVCenter,
                          public_id.isEmpty() ? QStringLiteral("ID -") : public_id);
    }

    painter->restore();
}

QSize MentionItemDelegate::sizeHint(const QStyleOptionViewItem&,
                                    const QModelIndex&) const {
    return QSize(0, 46);
}
