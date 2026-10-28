#include "ui/main_window_support.h"

#include <limits>

#include "common/group_protocol.h"
#include "core/group_types.h"
#include "ui/group_create_dialog.h"
#include "ui/group_join_dialog.h"

namespace {

QString roleSuffix(int role) {
    if (role == 2) {
        return " · 群主";
    }
    if (role == 1) {
        return " · 管理员";
    }
    return {};
}

QString groupRowText(const chatbox::client::GroupInfo& group) {
    QString text = group.name;
    if (group.is_default) {
        text.prepend("默认 · ");
    }
    text += QString(" · %1人").arg(group.member_count);
    text += roleSuffix(group.role);
    if (group.unread > 0) {
        text.prepend(QString("(%1) ").arg(group.unread));
    }
    return text;
}

}  // namespace

void MainWindow::onGroupClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 room_id =
        item->data(Qt::UserRole).toULongLong();
    const int status = item->data(Qt::UserRole + 2).toInt();
    if (status != 0) {
        setStatus("该群已解散", true);
        return;
    }
    if (room_id != 0) {
        selectGroup(room_id, item->data(Qt::UserRole + 1).toString());
    }
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
        auto* item = new QListWidgetItem(groupRowText(group), group_list_);
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
        auto* header = new QListWidgetItem("已解散", group_list_);
        header->setFlags(Qt::NoItemFlags);
        header->setData(Qt::AccessibleTextRole, "已解散的群");
        for (const chatbox::client::GroupInfo& group : dissolved) {
            auto* item = new QListWidgetItem(
                QString("%1（已解散）").arg(group.name), group_list_);
            item->setFlags(Qt::NoItemFlags);
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(group.room_id));
            item->setData(Qt::UserRole + 1, group.name);
            item->setData(Qt::UserRole + 2, group.status);
            item->setData(Qt::AccessibleTextRole, group.name + "，已解散");
        }
    }
    updateGroupNoticeBadge();
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

void MainWindow::openGroupNotifications() {
    requestGroupRequests(0);
    if (context_type_ == ContextType::Room && current_room_id_ != 0) {
        requestGroupRequests(current_room_id_);
    }
    updateGroupNoticeBadge();
    setStatus(QString("群通知：%1 条待处理")
                  .arg(group_requests_.size() + group_invites_.size()));
}

void MainWindow::markGroupRead(quint64 room_id, quint64 message_id) {
    if (offline_mode_ || room_id == 0 || message_id == 0) {
        return;
    }
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    writer.putU64(message_id);
    sendGroupCommand(chatbox::kCmdGroupMarkRead, writer.data());
}

void MainWindow::updateGroupNoticeBadge() {
    if (group_notice_badge_ == nullptr) {
        return;
    }
    const int count =
        static_cast<int>(group_requests_.size() + group_invites_.size());
    group_notice_badge_->setText(QString::number(count));
    group_notice_badge_->setVisible(count > 0);
}
