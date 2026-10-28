#ifndef CHATBOX_CLIENT_UI_MENTION_ITEM_DELEGATE_H
#define CHATBOX_CLIENT_UI_MENTION_ITEM_DELEGATE_H

#include <QStyledItemDelegate>

// One row of the mention picker: 28px avatar, nickname, public id and an
// online dot. Mirrors FriendItemDelegate so the two lists read as one system.
class MentionItemDelegate : public QStyledItemDelegate {
public:
    static constexpr int kUserIdRole = Qt::UserRole;
    static constexpr int kNameRole = Qt::UserRole + 1;
    static constexpr int kPublicIdRole = Qt::UserRole + 2;
    static constexpr int kOnlineRole = Qt::UserRole + 3;
    static constexpr int kAvatarRole = Qt::UserRole + 4;

    explicit MentionItemDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
};

#endif  // CHATBOX_CLIENT_UI_MENTION_ITEM_DELEGATE_H
