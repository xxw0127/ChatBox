#ifndef CHATBOX_CLIENT_UI_FRIEND_REQUEST_DIALOG_H
#define CHATBOX_CLIENT_UI_FRIEND_REQUEST_DIALOG_H

#include <QDialog>
#include <QVector>

#include "core/group_types.h"

class QLabel;
class QListWidget;
class QPushButton;

struct FriendRequestInfo {
    quint64 user_id = 0;
    QString username;
    QString nickname;
    QString created_at;
};

// Unified notification center for friend requests and group invitations.
// Every row carries its own Accept/Reject buttons so the user never has to
// discover that a row must be selected first.
class FriendRequestDialog : public QDialog {
    Q_OBJECT

public:
    explicit FriendRequestDialog(QWidget* parent = nullptr);

    void setRequests(const QVector<FriendRequestInfo>& requests);
    void setNotifications(
        const QVector<FriendRequestInfo>& requests,
        const QVector<chatbox::client::GroupRequestInfo>& invites);
    QString currentDisplayName() const;

signals:
    void respondRequested(quint64 requester_id, bool accept);
    void groupInviteRespondRequested(quint64 invite_id, bool accept);
    void refreshRequested();
    void profileRequested(quint64 user_id);

private:
    QListWidget* list_ = nullptr;
    QLabel* status_label_ = nullptr;
};

#endif  // CHATBOX_CLIENT_UI_FRIEND_REQUEST_DIALOG_H
