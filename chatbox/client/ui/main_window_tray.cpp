#include "ui/main_window_support.h"

void MainWindow::refreshUnreadTitle() {
    int total = 0;
    if (conversation_list_ != nullptr) {
        for (int i = 0; i < conversation_list_->count(); ++i) {
            total += conversation_list_->item(i)->data(Qt::UserRole + 2).toInt();
        }
    }
    for (const chatbox::client::GroupInfo& group : group_store_.active()) {
        total += std::max(0, group.unread);
    }
    setWindowTitle(total > 0 ? QString("ChatBox (%1)").arg(total)
                             : QString("ChatBox"));
}

void MainWindow::showPrivateNotification(const QString& nickname,
                                         const QString& content) {
    if (!notifications_enabled_) {
        return;
    }
    QApplication::alert(this, 3000);
    if (system_popup_enabled_ && tray_ != nullptr) {
        tray_->showMessage("私聊消息", nickname + ": " + content,
                           QSystemTrayIcon::Information, 3000);
    }
}

void MainWindow::showMentionNotification(const QString& nickname,
                                          const QString& content) {
    if (!notifications_enabled_) {
        return;
    }
    QApplication::alert(this, 3000);
    if (system_popup_enabled_ && tray_ != nullptr) {
        tray_->showMessage("有人@你", nickname + ": " + content,
                           QSystemTrayIcon::Information, 3000);
    }
}
