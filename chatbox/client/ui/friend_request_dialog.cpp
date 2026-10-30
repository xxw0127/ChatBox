#include "ui/friend_request_dialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/identity.h"
#include "core/message_time.h"

namespace {

QString friendRequestText(const FriendRequestInfo& request) {
    QString text = request.nickname;
    if (!request.username.isEmpty()) {
        text += " (" + request.username + ")";
    }
    text += "  " + chatbox::client::publicUserId(request.user_id);
    if (!request.created_at.isEmpty()) {
        const QDateTime utc =
            chatbox::client::parseServerUtc(request.created_at);
        text += "  " + (utc.isValid()
                            ? chatbox::client::formatLocalDate(utc) + " " +
                                  chatbox::client::formatLocalTime(utc)
                            : request.created_at);
    }
    return text;
}

QString groupInviteText(const chatbox::client::GroupRequestInfo& invite) {
    QString text = QStringLiteral("%1 邀请你加入 %2")
                       .arg(invite.nickname, invite.room_name);
    if (!invite.message.isEmpty()) {
        text += QStringLiteral("：%1").arg(invite.message);
    }
    return text;
}

}  // namespace

FriendRequestDialog::FriendRequestDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("NotificationsDialog"));
    setWindowTitle(QStringLiteral("通知中心"));
    resize(520, 440);

    auto* layout = new QVBoxLayout(this);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("NotificationsList"));
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(list_, 1);

    auto* footer = new QHBoxLayout();
    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("MutedLabel"));
    footer->addWidget(status_label_, 1);
    auto* refresh = new QPushButton(QStringLiteral("刷新"), this);
    refresh->setObjectName(QStringLiteral("NotificationsRefreshButton"));
    footer->addWidget(refresh);
    layout->addLayout(footer);

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);

    connect(refresh, &QPushButton::clicked, this,
            &FriendRequestDialog::refreshRequested);
}

void FriendRequestDialog::setRequests(
    const QVector<FriendRequestInfo>& requests) {
    setNotifications(requests, {});
}

void FriendRequestDialog::setNotifications(
    const QVector<FriendRequestInfo>& requests,
    const QVector<chatbox::client::GroupRequestInfo>& invites) {
    list_->clear();

    if (!requests.isEmpty()) {
        auto* header = new QListWidgetItem(QStringLiteral("好友申请"), list_);
        header->setFlags(Qt::NoItemFlags);
    }
    for (const FriendRequestInfo& request : requests) {
        auto* row = new QWidget(list_);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(4, 3, 4, 3);
        row_layout->setSpacing(6);
        auto* text = new QLabel(friendRequestText(request), row);
        text->setWordWrap(true);
        text->setTextFormat(Qt::PlainText);
        row_layout->addWidget(text, 1);

        auto* profile = new QPushButton(QStringLiteral("资料"), row);
        profile->setObjectName(QStringLiteral("FriendProfileButton"));
        auto* reject = new QPushButton(QStringLiteral("拒绝"), row);
        reject->setObjectName(QStringLiteral("FriendRejectButton"));
        auto* accept = new QPushButton(QStringLiteral("接受"), row);
        accept->setObjectName(QStringLiteral("PrimaryButton"));
        accept->setAccessibleName(QStringLiteral("FriendAcceptButton"));
        row_layout->addWidget(profile);
        row_layout->addWidget(reject);
        row_layout->addWidget(accept);

        auto* item = new QListWidgetItem(list_);
        item->setSizeHint(row->sizeHint());
        list_->setItemWidget(item, row);
        connect(profile, &QPushButton::clicked, this,
                [this, user_id = request.user_id] {
                    emit profileRequested(user_id);
                });
        connect(accept, &QPushButton::clicked, this,
                [this, accept, reject, user_id = request.user_id] {
                    accept->setEnabled(false);
                    reject->setEnabled(false);
                    status_label_->setText(
                        QStringLiteral("已提交，等待服务器确认..."));
                    emit respondRequested(user_id, true);
                });
        connect(reject, &QPushButton::clicked, this,
                [this, accept, reject, user_id = request.user_id] {
                    accept->setEnabled(false);
                    reject->setEnabled(false);
                    status_label_->setText(
                        QStringLiteral("已提交，等待服务器确认..."));
                    emit respondRequested(user_id, false);
                });
    }

    if (!invites.isEmpty()) {
        auto* header = new QListWidgetItem(QStringLiteral("群邀请"), list_);
        header->setFlags(Qt::NoItemFlags);
    }
    for (const chatbox::client::GroupRequestInfo& invite : invites) {
        auto* row = new QWidget(list_);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(4, 3, 4, 3);
        row_layout->setSpacing(6);
        auto* text = new QLabel(groupInviteText(invite), row);
        text->setWordWrap(true);
        text->setTextFormat(Qt::PlainText);
        row_layout->addWidget(text, 1);

        auto* reject = new QPushButton(QStringLiteral("拒绝"), row);
        reject->setObjectName(QStringLiteral("GroupInviteRejectButton"));
        auto* accept = new QPushButton(QStringLiteral("接受"), row);
        accept->setObjectName(QStringLiteral("PrimaryButton"));
        accept->setAccessibleName(QStringLiteral("GroupInviteAcceptButton"));
        row_layout->addWidget(reject);
        row_layout->addWidget(accept);

        auto* item = new QListWidgetItem(list_);
        item->setSizeHint(row->sizeHint());
        list_->setItemWidget(item, row);
        connect(accept, &QPushButton::clicked, this,
                [this, accept, reject, invite_id = invite.request_id] {
                    accept->setEnabled(false);
                    reject->setEnabled(false);
                    status_label_->setText(
                        QStringLiteral("已提交，等待服务器确认..."));
                    emit groupInviteRespondRequested(invite_id, true);
                });
        connect(reject, &QPushButton::clicked, this,
                [this, accept, reject, invite_id = invite.request_id] {
                    accept->setEnabled(false);
                    reject->setEnabled(false);
                    status_label_->setText(
                        QStringLiteral("已提交，等待服务器确认..."));
                    emit groupInviteRespondRequested(invite_id, false);
                });
    }

    const int total = requests.size() + invites.size();
    status_label_->setText(total == 0
                               ? QStringLiteral("没有待处理通知")
                               : QStringLiteral("有 %1 条待处理通知").arg(total));
}

QString FriendRequestDialog::currentDisplayName() const {
    return QString();
}
