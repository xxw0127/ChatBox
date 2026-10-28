#include "ui/group_notify_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

GroupNotifyDialog::GroupNotifyDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("GroupNotifyDialog"));
    setWindowTitle(QStringLiteral("群通知"));
    setMinimumSize(460, 360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("GroupNotifyList"));
    layout->addWidget(list_, 1);

    auto* actions = new QHBoxLayout();
    accept_button_ = new QPushButton(QStringLiteral("同意"), this);
    accept_button_->setObjectName(QStringLiteral("GroupNotifyAccept"));
    reject_button_ = new QPushButton(QStringLiteral("拒绝"), this);
    reject_button_->setObjectName(QStringLiteral("GroupNotifyReject"));
    actions->addWidget(accept_button_);
    actions->addWidget(reject_button_);
    actions->addStretch(1);
    layout->addLayout(actions);

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);

    connect(list_, &QListWidget::itemSelectionChanged, this,
            &GroupNotifyDialog::updateActionState);
    auto act_on_current = [this](bool accept) {
        QListWidgetItem* item = list_->currentItem();
        if (item == nullptr) {
            return;
        }
        const int type = item->data(Qt::UserRole).toInt();
        const quint64 id = item->data(Qt::UserRole + 1).toULongLong();
        delete list_->takeItem(list_->row(item));
        updateActionState();
        emit actionRequested(type, id, accept);
    };
    connect(accept_button_, &QPushButton::clicked, this,
            [act_on_current] { act_on_current(true); });
    connect(reject_button_, &QPushButton::clicked, this,
            [act_on_current] { act_on_current(false); });
    updateActionState();
}

void GroupNotifyDialog::setData(
    const QVector<chatbox::client::GroupRequestInfo>& requests,
    const QVector<chatbox::client::GroupRequestInfo>& invites, bool admin) {
    Q_UNUSED(admin);
    list_->clear();
    for (const chatbox::client::GroupRequestInfo& request : requests) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 申请加入%2")
                .arg(request.nickname)
                .arg(request.message.isEmpty() ? QString()
                                                : QStringLiteral("：%1").arg(request.message)),
            list_);
        item->setData(Qt::UserRole, 0);
        item->setData(Qt::UserRole + 1, QVariant::fromValue<qulonglong>(request.request_id));
    }
    for (const chatbox::client::GroupRequestInfo& invite : invites) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 邀请你加入 %2")
                .arg(invite.nickname, invite.room_name),
            list_);
        item->setData(Qt::UserRole, 1);
        item->setData(Qt::UserRole + 1, QVariant::fromValue<qulonglong>(invite.request_id));
    }
    updateActionState();
}

void GroupNotifyDialog::updateActionState() {
    const bool has_selection = list_->currentItem() != nullptr;
    accept_button_->setEnabled(has_selection);
    reject_button_->setEnabled(has_selection);
    if (!has_selection) {
        return;
    }
    const int type = list_->currentItem()->data(Qt::UserRole).toInt();
    accept_button_->setText(type == 0 ? QStringLiteral("同意")
                                      : QStringLiteral("接受"));
    reject_button_->setText(type == 0 ? QStringLiteral("拒绝")
                                      : QStringLiteral("拒绝"));
}
