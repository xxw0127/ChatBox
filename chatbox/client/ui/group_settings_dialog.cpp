#include "ui/group_settings_dialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString roleText(int role) {
    if (role == 2) {
        return QStringLiteral(" · 群主");
    }
    if (role == 1) {
        return QStringLiteral(" · 管理员");
    }
    return {};
}

}  // namespace

GroupSettingsDialog::GroupSettingsDialog(
    const chatbox::client::GroupInfo& group,
    const QVector<chatbox::client::GroupMemberInfo>& members,
    quint64 current_user_id, QWidget* parent)
    : QDialog(parent),
      group_(group),
      members_(members),
      current_user_id_(current_user_id) {
    setObjectName(QStringLiteral("GroupSettingsDialog"));
    setWindowTitle(QStringLiteral("群设置"));
    setMinimumSize(480, 500);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(8);

    title_label_ = new QLabel(this);
    title_label_->setObjectName(QStringLiteral("GroupSettingsTitle"));
    layout->addWidget(title_label_);

    auto* rename_row = new QHBoxLayout();
    name_edit_ = new QLineEdit(group_.name, this);
    name_edit_->setObjectName(QStringLiteral("GroupNameEdit"));
    rename_button_ = new QPushButton(QStringLiteral("改名"), this);
    rename_button_->setObjectName(QStringLiteral("GroupRenameButton"));
    rename_row->addWidget(name_edit_, 1);
    rename_row->addWidget(rename_button_);
    layout->addLayout(rename_row);

    auto* members_label = new QLabel(QStringLiteral("成员"), this);
    layout->addWidget(members_label);
    member_list_ = new QListWidget(this);
    member_list_->setObjectName(QStringLiteral("GroupMemberAdminList"));
    member_list_->setIconSize(QSize(28, 28));
    layout->addWidget(member_list_, 1);

    auto* actions = new QHBoxLayout();
    set_admin_button_ = new QPushButton(QStringLiteral("设为管理员"), this);
    set_admin_button_->setObjectName(QStringLiteral("GroupSetAdminButton"));
    unset_admin_button_ = new QPushButton(QStringLiteral("取消管理员"), this);
    unset_admin_button_->setObjectName(QStringLiteral("GroupUnsetAdminButton"));
    remove_button_ = new QPushButton(QStringLiteral("移出群"), this);
    remove_button_->setObjectName(QStringLiteral("GroupRemoveMemberButton"));
    transfer_button_ = new QPushButton(QStringLiteral("转让群主"), this);
    transfer_button_->setObjectName(QStringLiteral("GroupTransferOwnerButton"));
    actions->addWidget(set_admin_button_);
    actions->addWidget(unset_admin_button_);
    actions->addWidget(remove_button_);
    actions->addWidget(transfer_button_);
    actions->addStretch(1);
    layout->addLayout(actions);

    auto* footer = new QHBoxLayout();
    auto* requests = new QPushButton(QStringLiteral("群申请 / 邀请"), this);
    requests->setObjectName(QStringLiteral("GroupRequestsButton"));
    requests->setVisible(group_.role >= 1);
    disband_button_ = new QPushButton(QStringLiteral("解散群"), this);
    disband_button_->setObjectName(QStringLiteral("GroupDisbandButton"));
    disband_button_->setVisible(group_.role == 2 && !group_.is_default);
    footer->addWidget(requests);
    footer->addStretch(1);
    footer->addWidget(disband_button_);
    layout->addLayout(footer);

    auto* close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);

    connect(rename_button_, &QPushButton::clicked, this,
            &GroupSettingsDialog::renameGroup);
    connect(set_admin_button_, &QPushButton::clicked, this, [this] {
        emit memberActionRequested(group_.room_id, selectedUserId(), 2);
    });
    connect(unset_admin_button_, &QPushButton::clicked, this, [this] {
        emit memberActionRequested(group_.room_id, selectedUserId(), 3);
    });
    connect(remove_button_, &QPushButton::clicked, this, [this] {
        emit memberActionRequested(group_.room_id, selectedUserId(), 1);
    });
    connect(transfer_button_, &QPushButton::clicked, this, [this] {
        emit memberActionRequested(group_.room_id, selectedUserId(), 4);
    });
    connect(disband_button_, &QPushButton::clicked, this,
            &GroupSettingsDialog::disbandGroup);
    connect(requests, &QPushButton::clicked, this,
            [this] { emit requestsRequested(group_.room_id); });
    connect(member_list_, &QListWidget::itemSelectionChanged, this,
            &GroupSettingsDialog::updateActionState);

    title_label_->setText(QStringLiteral("%1 · 群号 %2").arg(group_.name, group_.group_no));
    name_edit_->setEnabled(group_.role >= 1);
    rename_button_->setEnabled(group_.role >= 1);
    refreshMembers();
}

void GroupSettingsDialog::setMembers(
    const QVector<chatbox::client::GroupMemberInfo>& members) {
    members_ = members;
    refreshMembers();
}

void GroupSettingsDialog::refreshMembers() {
    if (member_list_ == nullptr) {
        return;
    }
    const quint64 selected = selectedUserId();
    member_list_->clear();
    for (const chatbox::client::GroupMemberInfo& member : members_) {
        auto* item = new QListWidgetItem(
            QString("%1%2").arg(member.nickname, roleText(member.role)),
            member_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(member.user_id));
        item->setData(Qt::UserRole + 1, member.role);
        if (member.user_id == selected) {
            member_list_->setCurrentItem(item);
        }
    }
    updateActionState();
}

void GroupSettingsDialog::updateActionState() {
    const int target_role = selectedRole();
    const quint64 target_id = selectedUserId();
    const bool can_manage = group_.role >= 1;
    const bool is_self = target_id == current_user_id_;
    set_admin_button_->setEnabled(group_.role == 2 && target_id != 0 &&
                                  target_role == 0 && !is_self);
    unset_admin_button_->setEnabled(group_.role == 2 && target_role == 1 &&
                                    !is_self);
    remove_button_->setEnabled(can_manage && !group_.is_default &&
                               target_id != 0 && target_role < 2 && !is_self);
    transfer_button_->setEnabled(group_.role == 2 && target_id != 0 &&
                                 target_role < 2 && !is_self);
}

void GroupSettingsDialog::renameGroup() {
    const QString name = name_edit_->text().trimmed();
    if (name.isEmpty() || name.toUcs4().size() > 32) {
        QMessageBox::warning(this, QStringLiteral("群名"),
                             QStringLiteral("群名需要 1–32 个字符"));
        return;
    }
    emit renameRequested(group_.room_id, name);
}

void GroupSettingsDialog::disbandGroup() {
    if (QMessageBox::question(
            this, QStringLiteral("解散群"),
            QStringLiteral("解散后服务器上的群消息和文件会被删除，成员本地记录保留。确定解散？"),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
        return;
    }
    emit disbandRequested(group_.room_id);
}

quint64 GroupSettingsDialog::selectedUserId() const {
    if (member_list_ == nullptr || member_list_->currentItem() == nullptr) {
        return 0;
    }
    return member_list_->currentItem()
        ->data(Qt::UserRole)
        .toULongLong();
}

int GroupSettingsDialog::selectedRole() const {
    if (member_list_ == nullptr || member_list_->currentItem() == nullptr) {
        return -1;
    }
    return member_list_->currentItem()->data(Qt::UserRole + 1).toInt();
}
