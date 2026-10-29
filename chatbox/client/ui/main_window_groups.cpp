#include "ui/main_window_support.h"

#include <limits>

#include "common/group_protocol.h"
#include "core/group_types.h"
#include "ui/group_create_dialog.h"
#include "ui/group_join_dialog.h"
#include "ui/group_notify_dialog.h"
#include "ui/group_settings_dialog.h"
#include "core/group_types.h"



QVector<chatbox::client::GroupRequestInfo>
MainWindow::pendingGroupRequests() const {
    QVector<chatbox::client::GroupRequestInfo> requests;
    for (auto it = group_requests_by_room_.constBegin();
         it != group_requests_by_room_.constEnd(); ++it) {
        requests += it.value();
    }
    return requests;
}

void MainWindow::onGroupClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 room_id =
        item->data(Qt::UserRole).toULongLong();
    const int status = item->data(Qt::UserRole + 2).toInt();
    if (offline_mode_) {
        if (room_id != 0) {
            selectRoom(static_cast<int>(room_id),
                       item->data(Qt::UserRole + 1).toString());
        }
        return;
    }
    if (status != 0) {
        if (room_id != 0) {
            selectDissolvedGroup(room_id, item->data(Qt::UserRole + 1).toString());
        }
        return;
    }
    if (room_id != 0) {
        selectGroup(room_id, item->data(Qt::UserRole + 1).toString());
    }
}

void MainWindow::selectDissolvedGroup(quint64 room_id, const QString& name) {
    if (room_id > static_cast<quint64>(std::numeric_limits<int>::max())) {
        return;
    }
    context_type_ = ContextType::Room;
    clearStaleListSelection(ContextType::Room);
    current_room_id_ = static_cast<int>(room_id);
    current_peer_id_ = 0;
    current_peer_name_ = name;
    const chatbox::client::GroupInfo* group = group_store_.find(room_id);
    const QString tombstone = (group != nullptr && group->status == 2)
                                  ? QStringLiteral("已退出")
                                  : QStringLiteral("已解散");
    context_label_->setText(QString("%1（%2）").arg(name, tombstone));
    chat_view_->clear();
    input_edit_->setEnabled(false);
    send_button_->setEnabled(false);
    mention_button_->hide();
    message_store_.reset(chatbox::client::ChatContext::kRoom, room_id);
    has_more_ = false;
    loading_older_history_ = false;
    pending_jump_message_id_ = 0;
    history_request_seq_ = 0;
    history_request_is_jump_ = false;
    jump_history_pending_ = false;
    next_before_id_ = 0;
    loadInitialHistory(chatbox::client::ChatContext::kRoom, room_id);
    renderCurrentConversation(ScrollMode::kBottom);
    if (member_count_label_ != nullptr) {
        member_count_label_->setText(name);
    }
    if (member_list_ != nullptr) {
        member_list_->clear();
    }
    updateRightPanelVisibility();
    setStatus(QString("群%1 · 仅显示本地记录").arg(tombstone));
}

void MainWindow::selectGroup(quint64 room_id, const QString& name) {
    const chatbox::client::GroupInfo* group = group_store_.find(room_id);
    if (group == nullptr || group->status != 0) {
        return;
    }
    if (room_id > static_cast<quint64>(std::numeric_limits<int>::max())) {
        return;
    }
    selectRoom(static_cast<int>(room_id), name);
    requestGroupMembers(room_id);
    if (group->role >= 1) {
        requestGroupRequests(room_id);
    }
    quint64 last_message_id = 0;
    if (!message_store_.messages().isEmpty()) {
        last_message_id = message_store_.messages().back().message_id;
    }
    markGroupRead(room_id, last_message_id);
    group_store_.setUnread(room_id, 0);
    refreshGroupListView();
}

void MainWindow::refreshGroupListView() {
    if (group_list_ == nullptr) {
        return;
    }
    group_list_->clear();
    const QVector<chatbox::client::GroupInfo> active = group_store_.active();
    for (const chatbox::client::GroupInfo& group : active) {
        auto* item = new QListWidgetItem(
            chatbox::client::groupListTitle(group), group_list_);
        item->setIcon(QIcon(QPixmap::fromImage(chatbox::client::roomAvatar(30))));
        item->setSizeHint(QSize(0, 48));
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(group.room_id));
        item->setData(Qt::UserRole + 1, group.name);
        item->setData(Qt::UserRole + 2, group.status);
        item->setData(Qt::UserRole + 3, group.role);
        item->setData(Qt::UserRole + 4, group.unread);
        item->setData(Qt::AccessibleTextRole,
                      QString("%1，%2 人").arg(group.name).arg(group.member_count));
        if (group.room_id == static_cast<quint64>(current_room_id_)) {
            group_list_->setCurrentItem(item);
        }
    }

    const QVector<chatbox::client::GroupInfo> dissolved = group_store_.dissolved();
    if (!dissolved.isEmpty()) {
        auto* header = new QListWidgetItem("已退出 / 已解散", group_list_);
        header->setFlags(Qt::NoItemFlags);
        header->setData(Qt::AccessibleTextRole, "已退出或已解散的群");
        for (const chatbox::client::GroupInfo& group : dissolved) {
            const QString tombstone = group.status == 2 ? QStringLiteral("已退出")
                                                        : QStringLiteral("已解散");
            auto* item = new QListWidgetItem(
                chatbox::client::groupListTitle(group), group_list_);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(group.room_id));
            item->setData(Qt::UserRole + 1, group.name);
            item->setData(Qt::UserRole + 2, group.status);
            item->setData(Qt::AccessibleTextRole,
                          QString("%1，%2").arg(group.name, tombstone));
        }
    }
    updateGroupNoticeBadge();
    refreshUnreadTitle();
}

void MainWindow::openCreateGroupDialog() {
    if (offline_mode_) {
        setStatus("离线模式下不能创建群", true);
        return;
    }
    if (group_create_dialog_ == nullptr) {
        group_create_dialog_ = new GroupCreateDialog(this);
        connect(group_create_dialog_, &GroupCreateDialog::createRequested, this,
                [this](const QString& name) {
                    chatbox::ByteWriter writer;
                    const QByteArray utf8 = name.toUtf8();
                    writer.putString(
                        std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
                    sendGroupCommand(chatbox::kCmdGroupCreate, writer.data());
                    group_create_dialog_->hide();
                    setStatus("正在创建群...");
                });
    }
    group_create_dialog_->show();
    group_create_dialog_->raise();
    group_create_dialog_->activateWindow();
}

void MainWindow::openJoinGroupDialog() {
    if (offline_mode_) {
        setStatus("离线模式下不能加入群", true);
        return;
    }
    if (group_join_dialog_ == nullptr) {
        group_join_dialog_ = new GroupJoinDialog(this);
        connect(group_join_dialog_, &GroupJoinDialog::searchRequested, this,
                [this](const QString& group_no) {
                    chatbox::ByteWriter writer;
                    const QByteArray utf8 = group_no.toUtf8();
                    writer.putString(
                        std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
                    sendGroupCommand(chatbox::kCmdGroupSearch, writer.data());
                    setStatus("正在搜索群...");
                });
        connect(group_join_dialog_, &GroupJoinDialog::joinRequested, this,
                [this](const QString& group_no, const QString& message) {
                    chatbox::ByteWriter writer;
                    const QByteArray group_utf8 = group_no.toUtf8();
                    const QByteArray message_utf8 = message.toUtf8();
                    writer.putString(std::string(
                        group_utf8.constData(),
                        static_cast<std::size_t>(group_utf8.size())));
                    writer.putString(std::string(
                        message_utf8.constData(),
                        static_cast<std::size_t>(message_utf8.size())));
                    sendGroupCommand(chatbox::kCmdGroupJoinRequest, writer.data());
                    group_join_dialog_->hide();
                    setStatus("已提交入群申请，等待管理员审批");
                });
    }
    group_join_dialog_->show();
    group_join_dialog_->raise();
    group_join_dialog_->activateWindow();
}

void MainWindow::openGroupSettings() {
    if (context_type_ != ContextType::Room || current_room_id_ == 0) {
        return;
    }
    const chatbox::client::GroupInfo* group =
        group_store_.find(static_cast<quint64>(current_room_id_));
    if (group == nullptr || group->role < 1) {
        return;
    }
    requestGroupMembers(static_cast<quint64>(current_room_id_));
    if (group_settings_dialog_ != nullptr) {
        group_settings_dialog_->disconnect(this);
        group_settings_dialog_->deleteLater();
        group_settings_dialog_ = nullptr;
    }
    group_settings_dialog_ = new GroupSettingsDialog(
        *group, group_store_.members(static_cast<quint64>(current_room_id_)),
        user_id_, this);
    group_settings_dialog_->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(group_settings_dialog_, &QObject::destroyed, this,
            [this] { group_settings_dialog_ = nullptr; });
    connect(group_settings_dialog_, &GroupSettingsDialog::renameRequested, this,
            [this](quint64 room_id, const QString& name) {
                chatbox::ByteWriter writer;
                writer.putU32(static_cast<uint32_t>(room_id));
                const QByteArray utf8 = name.toUtf8();
                writer.putString(std::string(
                    utf8.constData(), static_cast<std::size_t>(utf8.size())));
                sendGroupCommand(chatbox::kCmdGroupUpdate, writer.data());
            });
    connect(group_settings_dialog_,
            &GroupSettingsDialog::memberActionRequested, this,
            [this](quint64 room_id, quint64 user_id, int action) {
                chatbox::ByteWriter writer;
                writer.putU32(static_cast<uint32_t>(room_id));
                writer.putU64(user_id);
                writer.putU8(static_cast<uint8_t>(action));
                sendGroupCommand(chatbox::kCmdGroupMemberUpdate, writer.data());
            });
    connect(group_settings_dialog_, &GroupSettingsDialog::disbandRequested, this,
            [this](quint64 room_id) {
                chatbox::ByteWriter writer;
                writer.putU32(static_cast<uint32_t>(room_id));
                sendGroupCommand(chatbox::kCmdGroupDisband, writer.data());
            });
    connect(group_settings_dialog_, &GroupSettingsDialog::requestsRequested, this,
            [this](quint64) { openGroupNotifications(); });
    group_settings_dialog_->show();
    group_settings_dialog_->raise();
    group_settings_dialog_->activateWindow();
}

void MainWindow::openGroupInviteDialog(quint64 room_id) {
    if (offline_mode_ || room_id == 0) {
        setStatus("离线模式下不能邀请成员", true);
        return;
    }
    const chatbox::client::GroupInfo* group = group_store_.find(room_id);
    if (group == nullptr || group->status != 0) {
        setStatus("群不存在或已解散", true);
        return;
    }
    QSet<quint64> members;
    for (const chatbox::client::GroupMemberInfo& member :
         group_store_.members(room_id)) {
        members.insert(member.user_id);
    }

    QVector<QPair<QString, quint64>> candidates;
    for (auto it = friend_views_.constBegin(); it != friend_views_.constEnd();
         ++it) {
        if (it.key() == user_id_ || members.contains(it.key())) {
            continue;
        }
        QString name = it.value().remark.trimmed();
        if (name.isEmpty()) {
            name = it.value().nickname.trimmed();
        }
        if (name.isEmpty()) {
            name = it.value().username;
        }
        candidates.push_back(
            {QString("%1 (%2)").arg(name, chatbox::client::publicUserId(it.key())),
             it.key()});
    }
    if (candidates.isEmpty()) {
        setStatus("没有可邀请的好友", true);
        return;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const QPair<QString, quint64>& a,
                 const QPair<QString, quint64>& b) { return a.first < b.first; });
    QStringList labels;
    for (const auto& candidate : candidates) {
        labels.push_back(candidate.first);
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, "邀请成员", "选择好友", labels, 0, false, &ok);
    if (!ok || chosen.isEmpty()) {
        return;
    }
    const int index = labels.indexOf(chosen);
    if (index < 0) {
        return;
    }
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    writer.putU64(candidates.at(index).second);
    sendGroupCommand(chatbox::kCmdGroupInvite, writer.data());
    setStatus(QString("已向 %1 发送入群邀请").arg(chosen));
}

void MainWindow::leaveGroup(quint64 room_id) {
    if (offline_mode_ || room_id == 0) {
        setStatus("离线模式下不能退出群", true);
        return;
    }
    const chatbox::client::GroupInfo* group = group_store_.find(room_id);
    if (group == nullptr || group->status != 0) {
        setStatus("群不存在或已解散", true);
        return;
    }
    if (group->is_default) {
        setStatus("默认群不能退出", true);
        return;
    }
    if (group->role == 2) {
        setStatus("请先转让群主再退出", true);
        return;
    }
    if (QMessageBox::question(
            this, "退出群",
            QString("退出“%1”后，服务器上的群消息和文件将不再可见。确定退出？")
                .arg(group->name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes) {
        return;
    }
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    sendGroupCommand(chatbox::kCmdGroupLeave, writer.data());
    setStatus("正在退出群...");
}

void MainWindow::openGroupNotifications() {
    requestGroupRequests(0);
    if (context_type_ == ContextType::Room && current_room_id_ != 0) {
        const chatbox::client::GroupInfo* group =
            group_store_.find(static_cast<quint64>(current_room_id_));
        if (group != nullptr && group->role >= 1) {
            requestGroupRequests(current_room_id_);
        }
    }
    if (group_notify_dialog_ == nullptr) {
        group_notify_dialog_ = new GroupNotifyDialog(this);
        group_notify_dialog_->setAttribute(Qt::WA_DeleteOnClose, true);
        connect(group_notify_dialog_, &QObject::destroyed, this,
                [this] { group_notify_dialog_ = nullptr; });
        connect(group_notify_dialog_, &GroupNotifyDialog::actionRequested, this,
                [this](int type, quint64 id, bool accept) {
                    chatbox::ByteWriter writer;
                    writer.putU64(id);
                    writer.putU8(accept ? 1 : 0);
                    sendGroupCommand(type == 0 ? chatbox::kCmdGroupJoinRespond
                                               : chatbox::kCmdGroupInviteRespond,
                                     writer.data());
                });
    }
    const int role = context_type_ == ContextType::Room
                         ? (group_store_.find(static_cast<quint64>(current_room_id_))
                                ? group_store_.find(static_cast<quint64>(current_room_id_))->role
                                : 0)
                         : 0;
    group_notify_dialog_->setData(pendingGroupRequests(), group_invites_,
                                  role >= 1);
    group_notify_dialog_->show();
    group_notify_dialog_->raise();
    group_notify_dialog_->activateWindow();
    updateGroupNoticeBadge();
}

void MainWindow::markGroupRead(quint64 room_id, quint64 message_id) {
    if (offline_mode_ || room_id == 0 || message_id == 0) {
        return;
    }
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    writer.putU64(message_id);
    sendGroupCommand(chatbox::kCmdGroupMarkRead, writer.data());
    group_store_.setUnread(room_id, 0);
    refreshGroupListView();
}

void MainWindow::updateGroupNoticeBadge() {
    if (group_notice_badge_ == nullptr) {
        return;
    }
    int request_count = 0;
    for (auto it = group_requests_by_room_.constBegin();
         it != group_requests_by_room_.constEnd(); ++it) {
        request_count += it.value().size();
    }
    const int count = request_count + group_invites_.size();
    group_notice_badge_->setText(QString::number(count));
    group_notice_badge_->setVisible(count > 0);
}
