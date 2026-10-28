#include "ui/main_window_support.h"

void MainWindow::sendMarkRead(quint64 peer_id, quint64 message_id) {
    if (offline_mode_) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdPrivateMarkRead;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU64(peer_id);
    writer.putU64(message_id);
    frame.body = writer.data();
    sendFrame(frame);
}

QListWidgetItem* MainWindow::findConversationItem(quint64 peer_id) const {
    for (int i = 0; i < conversation_list_->count(); ++i) {
        QListWidgetItem* item = conversation_list_->item(i);
        if (item->data(Qt::UserRole).toULongLong() == peer_id) {
            return item;
        }
    }
    return nullptr;
}

void MainWindow::updateFriendView(quint64 peer_id, int unread, const QString& nickname,
                                  const QString& last_content, const QString& last_time,
                                  bool online) {
    if (!friend_ids_.contains(peer_id)) {
        return;
    }
    peer_names_.insert(peer_id, nickname);
    chatbox::client::FriendView& view = friend_views_[peer_id];
    view.user_id = peer_id;
    if (!nickname.isEmpty()) {
        view.nickname = nickname;
    }
    view.unread = unread;
    view.last_content = last_content;
    view.last_time = last_time;
    view.online = online;
}

void MainWindow::updateConversationItem(quint64 peer_id, int unread,
                                        const QString& nickname,
                                        const QString& last_content,
                                        const QString& last_time, bool online) {
    updateFriendView(peer_id, unread, nickname, last_content, last_time, online);
    refreshFriendListView();
}

void MainWindow::refreshFriendListView() {
    if (conversation_list_ == nullptr) {
        return;
    }
    const quint64 selected_id =
        context_type_ == ContextType::Private ? current_peer_id_ : 0;
    conversation_list_->clear();

    QVector<chatbox::client::FriendView> online;
    QVector<chatbox::client::FriendView> offline;
    for (const chatbox::client::FriendView& view : friend_views_) {
        if (view.online) {
            online.push_back(view);
        } else {
            offline.push_back(view);
        }
    }
    const auto by_name = [](const chatbox::client::FriendView& a,
                            const chatbox::client::FriendView& b) {
        const QString a_name = a.remark.isEmpty() ? a.nickname : a.remark;
        const QString b_name = b.remark.isEmpty() ? b.nickname : b.remark;
        return a_name.localeAwareCompare(b_name) < 0;
    };
    std::sort(online.begin(), online.end(), by_name);
    std::sort(offline.begin(), offline.end(), by_name);

    const auto add_header = [this](const QString& text) {
        auto* item = new QListWidgetItem(text, conversation_list_);
        item->setFlags(Qt::NoItemFlags);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(0));
        item->setSizeHint(QSize(0, 26));
    };
    // The section label "好友" already names the list. Only add 在线/离线
    // group headers when there are two groups to distinguish; otherwise a
    // single group would stack two consecutive 11px labels with no content
    // between them.
    const bool show_group_headers = !online.isEmpty() && !offline.isEmpty();
    const auto add_friend = [this](const chatbox::client::FriendView& view) {
        auto* item = new QListWidgetItem(conversation_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(view.user_id));
        item->setData(Qt::UserRole + 1, view.nickname);
        item->setData(Qt::UserRole + 2,
                      chatbox::client::displayedUnread(notifications_enabled_,
                                                       view.unread));
        item->setData(Qt::UserRole + 3, view.online);
        item->setData(Qt::UserRole + 4, view.last_content);
        item->setData(Qt::UserRole + 5, view.last_time);
        item->setData(Qt::UserRole + 6, view.remark);
        item->setData(
            Qt::AccessibleTextRole,
            conversationAccessibleText(
                view.remark.isEmpty() ? view.nickname : view.remark, view.online,
                chatbox::client::displayedUnread(notifications_enabled_, view.unread),
                view.last_content));
        QImage image;
        if (avatar_cache_.lookup(view.user_id, image) && !image.isNull()) {
            item->setData(FriendItemDelegate::kAvatarRole,
                          QPixmap::fromImage(
                              chatbox::client::circularAvatar(image, 36)));
        }
        item->setToolTip(QString("ID: %1\n用户名: @%2")
                             .arg(chatbox::client::publicUserId(view.user_id),
                                  view.username));
        item->setSizeHint(QSize(0, 56));
    };

    if (!online.isEmpty()) {
        if (show_group_headers) {
            add_header("在线");
        }
        for (const auto& view : online) {
            add_friend(view);
        }
    }
    if (!offline.isEmpty()) {
        if (show_group_headers) {
            add_header("离线");
        }
        for (const auto& view : offline) {
            add_friend(view);
        }
    }

    if (selected_id != 0) {
        QListWidgetItem* item = findConversationItem(selected_id);
        if (item != nullptr) {
            conversation_list_->setCurrentItem(item);
        }
    }
    if (conversation_stack_ != nullptr) {
        if (friend_views_.isEmpty()) {
            conversation_stack_->setCurrentWidget(friends_empty_state_);
        } else {
            conversation_stack_->setCurrentWidget(conversation_list_);
        }
    }
    refreshUnreadTitle();
}

void MainWindow::updateRoomMemberNickname(quint64 user_id,
                                          const QString& nickname) {
    if (member_list_ == nullptr || user_id == 0 || nickname.isEmpty()) {
        return;
    }
    for (int i = 0; i < member_list_->count(); ++i) {
        QListWidgetItem* item = member_list_->item(i);
        if (item->data(Qt::UserRole).toULongLong() == user_id) {
            item->setText(nickname);
            item->setData(Qt::AccessibleTextRole, nickname + "，在线");
            item->setIcon(memberAvatarIcon(user_id, nickname, 28));
            break;
        }
    }
}

void MainWindow::setFriendRemark(quint64 peer_id) {
    if (offline_mode_) {
        setStatus("当前离线，无法设置备注", true);
        return;
    }
    if (peer_id == 0) {
        return;
    }
    const auto it = friend_views_.constFind(peer_id);
    if (it == friend_views_.constEnd()) {
        return;
    }
    bool ok = false;
    const QString value = QInputDialog::getText(
        this, "好友备注", "备注名（留空恢复昵称）", QLineEdit::Normal, it->remark, &ok);
    if (!ok) {
        return;
    }
    const QString remark = value.trimmed();
    if (remark.toUcs4().size() > 32) {
        setStatus("好友备注不能超过 32 个字符", true);
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendSetRemark;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU64(peer_id);
    writer.putString(chatbox::toUtf8(remark));
    frame.body = writer.data();
    sendFrame(frame);
    setStatus("正在保存好友备注...");
}

void MainWindow::requestRoomList() {
    if (offline_mode_) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdRoomList;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
}

void MainWindow::requestConversations() {
    if (offline_mode_) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdPrivateConversations;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
}

void MainWindow::requestFriendList() {
    if (offline_mode_) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendList;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
}

void MainWindow::requestFriendRequests() {
    if (offline_mode_) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendRequests;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
}

void MainWindow::sendFriendRequest(quint64 user_id) {
    if (offline_mode_) {
        setStatus("当前离线，无法发送好友申请", true);
        return;
    }
    if (user_id == 0 || user_id == user_id_) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendRequest;
    const quint32 seq = next_seq_++;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU64(user_id);
    frame.body = writer.data();
    if (sendFrame(frame)) {
        friend_request_seqs_.insert(seq, user_id);
        setStatus("好友申请已发送");
    }
}

void MainWindow::sendFriendResponse(quint64 requester_id, bool accept) {
    if (offline_mode_) {
        setStatus("当前离线，无法处理好友申请", true);
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendRespond;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU64(requester_id);
    writer.putU8(accept ? 1 : 0);
    frame.body = writer.data();
    sendFrame(frame);
}

void MainWindow::sendFriendRemove(quint64 friend_id) {
    if (offline_mode_) {
        setStatus("当前离线，无法删除好友", true);
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdFriendRemove;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU64(friend_id);
    frame.body = writer.data();
    sendFrame(frame);
}

void MainWindow::openFriendRequests() {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法查看好友申请", true);
        return;
    }
    if (friend_requests_dialog_ == nullptr) {
        friend_requests_dialog_ = new FriendRequestDialog(this);
        connect(friend_requests_dialog_, &FriendRequestDialog::respondRequested, this,
                [this](quint64 requester_id, bool accept) {
                    sendFriendResponse(requester_id, accept);
                });
        connect(friend_requests_dialog_, &FriendRequestDialog::refreshRequested, this,
                &MainWindow::requestFriendRequests);
        connect(friend_requests_dialog_, &FriendRequestDialog::profileRequested, this,
                [this](quint64 user_id) {
                    const QString fallback =
                        friend_requests_dialog_ != nullptr
                            ? friend_requests_dialog_->currentDisplayName()
                            : QString();
                    openProfile(user_id, fallback);
                });
    }
    requestFriendRequests();
    friend_requests_dialog_->show();
    friend_requests_dialog_->raise();
    friend_requests_dialog_->activateWindow();
}

void MainWindow::updateFriendRequestCount() {
    if (friend_requests_button_ == nullptr) {
        return;
    }
    friend_requests_button_->setText("好友申请");
    friend_requests_button_->setAccessibleName(
        friend_request_count_ > 0
            ? QString("好友申请，%1 条未读").arg(friend_request_count_)
            : QString("好友申请"));
    if (friend_requests_badge_ == nullptr) {
        return;
    }
    friend_requests_badge_->setText(
        friend_request_count_ > 9 ? QString("9+")
                                  : QString::number(friend_request_count_));
    friend_requests_badge_->setVisible(friend_request_count_ > 0);
}

void MainWindow::requestUserSearch(const QString& keyword) {
    if (offline_mode_) {
        setStatus("当前离线，无法搜索用户", true);
        return;
    }
    if (user_search_dialog_ == nullptr) {
        return;
    }
    if (keyword.toUtf8().size() > 32) {
        user_search_dialog_->setBusy(false);
        user_search_dialog_->setStatus("关键词不能超过 32 字节", true);
        return;
    }
    const quint32 seq = next_seq_++;
    user_search_seq_ = seq;
    user_search_dialog_->setBusy(true);
    user_search_dialog_->setStatus("正在搜索...");

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdUserSearch;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(keyword));
    frame.body = writer.data();
    if (!sendFrame(frame)) {
        user_search_seq_ = 0;
        user_search_dialog_->setBusy(false);
        user_search_dialog_->setStatus("网络未连接，无法搜索", true);
        return;
    }
    if (search_timeout_timer_ != nullptr) {
        search_timeout_timer_->start();
    }
}

void MainWindow::openUserSearch() {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法搜索用户", true);
        return;
    }
    if (user_search_dialog_ == nullptr) {
        user_search_dialog_ = new UserSearchDialog(this);
        connect(user_search_dialog_, &UserSearchDialog::searchRequested, this,
                &MainWindow::requestUserSearch);
        connect(user_search_dialog_, &UserSearchDialog::userActivated, this,
                [this](quint64 user_id, const QString&, const QString&) {
                    if (user_id == 0 || user_id == user_id_) {
                        return;
                    }
                    if (user_search_dialog_ != nullptr) {
                        user_search_dialog_->close();
                    }
                    sendFriendRequest(user_id);
                });
        connect(user_search_dialog_, &UserSearchDialog::profileRequested, this,
                [this](quint64 user_id) {
                    const QString fallback =
                        user_search_dialog_ != nullptr
                            ? user_search_dialog_->selectedDisplayName()
                            : QString();
                    openProfile(user_id, fallback);
                });
    }
    user_search_dialog_->clearResults();
    user_search_dialog_->show();
    user_search_dialog_->raise();
    user_search_dialog_->activateWindow();
}

void MainWindow::refreshContacts() {
    if (contact_list_ == nullptr) {
        return;
    }
    contact_list_->clear();
    for (const chatbox::client::Contact& contact : contact_store_.contacts()) {
        QString text = contact.nickname;
        if (!contact.username.isEmpty()) {
            text += " (" + contact.username + ")";
        }
        auto* item = new QListWidgetItem(text, contact_list_);
        item->setIcon(avatarIcon(contact.nickname, contact.user_id));
        item->setSizeHint(QSize(0, 48));
        item->setData(Qt::UserRole,
                      QVariant::fromValue<qulonglong>(contact.user_id));
        item->setData(Qt::UserRole + 1, contact.username);
        item->setData(Qt::UserRole + 2, contact.nickname);
    }
}

void MainWindow::onFriendContextMenu(const QPoint& position) {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法操作好友", true);
        return;
    }
    QListWidgetItem* item = conversation_list_->itemAt(position);
    if (item == nullptr) {
        return;
    }
    const quint64 friend_id = item->data(Qt::UserRole).toULongLong();
    if (friend_id == 0 || friend_id == user_id_) {
        return;
    }
    const QString nickname = item->data(Qt::UserRole + 1).toString();
    const QString remark = item->data(Qt::UserRole + 6).toString();
    const QString display_name = remark.isEmpty() ? nickname : remark;

    QMenu menu(this);
    QAction* profile_action = menu.addAction("查看资料");
    QAction* remark_action = menu.addAction("设置备注");
    QAction* remove_action = menu.addAction("删除好友");
    QAction* selected = menu.exec(conversation_list_->mapToGlobal(position));
    if (selected == profile_action) {
        openProfile(friend_id, display_name);
    } else if (selected == remark_action) {
        setFriendRemark(friend_id);
    } else if (selected == remove_action) {
        if (chatbox::client::confirmDanger(
                this, "删除好友",
                QString("确定要删除好友“%1”吗？").arg(display_name), "删除")) {
            sendFriendRemove(friend_id);
        }
    }
}

void MainWindow::onMemberContextMenu(const QPoint& position) {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法操作成员", true);
        return;
    }
    if (member_list_ == nullptr) {
        return;
    }
    QListWidgetItem* item = member_list_->itemAt(position);
    if (item == nullptr) {
        return;
    }
    const quint64 member_id = item->data(Qt::UserRole).toULongLong();
    if (member_id == 0) {
        return;
    }
    QMenu menu(this);
    QAction* profile_action = menu.addAction("查看资料");
    if (menu.exec(member_list_->mapToGlobal(position)) == profile_action) {
        openProfile(member_id, item->text());
    }
}

void MainWindow::onContactActivated(QListWidgetItem* item) {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法打开会话", true);
        return;
    }
    if (item == nullptr) {
        return;
    }
    const quint64 user_id = item->data(Qt::UserRole).toULongLong();
    const QString nickname = item->data(Qt::UserRole + 2).toString();
    if (user_id == 0 || user_id == user_id_) {
        return;
    }
    selectPrivate(user_id, nickname);
}

void MainWindow::onContactContextMenu(const QPoint& position) {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法操作联系人", true);
        return;
    }
    QListWidgetItem* item = contact_list_->itemAt(position);
    if (item == nullptr) {
        return;
    }
    const quint64 user_id = item->data(Qt::UserRole).toULongLong();
    QMenu menu(this);
    QAction* remove_action = menu.addAction("删除联系人");
    if (menu.exec(contact_list_->mapToGlobal(position)) == remove_action) {
        contact_store_.remove(user_id);
        refreshContacts();
    }
}

void MainWindow::onRoomClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const int room_id = item->data(Qt::UserRole).toInt();
    const QString name = item->data(Qt::UserRole + 1).toString();
    selectRoom(room_id, name);
}

void MainWindow::onConversationClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 peer_id = item->data(Qt::UserRole).toULongLong();
    if (peer_id == 0) {
        return;
    }
    const QString nickname = item->data(Qt::UserRole + 1).toString();
    const QString remark = item->data(Qt::UserRole + 6).toString();
    selectPrivate(peer_id, remark.isEmpty() ? nickname : remark);
}

void MainWindow::onFriendAvatarClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 peer_id = item->data(Qt::UserRole).toULongLong();
    if (peer_id == 0) {
        return;
    }
    const QString nickname = item->data(Qt::UserRole + 1).toString();
    const QString remark = item->data(Qt::UserRole + 6).toString();
    openProfile(peer_id, remark.isEmpty() ? nickname : remark);
}

void MainWindow::onMemberDoubleClicked(QListWidgetItem* item) {
    if (item == nullptr) {
        return;
    }
    const quint64 peer_id = item->data(Qt::UserRole).toULongLong();
    if (peer_id == 0 || peer_id == user_id_) {
        return;
    }
    const QString nickname = item->text();
    // 已知备注优先：成员列表里的昵称不能覆盖用户自己设的备注。
    const auto view = friend_views_.constFind(peer_id);
    if (view != friend_views_.constEnd() && !view->remark.trimmed().isEmpty()) {
        selectPrivate(peer_id, view->remark.trimmed());
        return;
    }
    selectPrivate(peer_id, nickname);
}

void MainWindow::addConversation(quint64 peer_id, const QString& nickname) {
    peer_names_.insert(peer_id, nickname);
    for (int i = 0; i < conversation_list_->count(); ++i) {
        QListWidgetItem* existing = conversation_list_->item(i);
        if (existing->data(Qt::UserRole).toULongLong() == peer_id) {
            existing->setText(nickname);
            existing->setData(Qt::UserRole + 1, nickname);
            return;
        }
    }
    auto* item = new QListWidgetItem(nickname, conversation_list_);
    item->setIcon(avatarIcon(nickname, peer_id));
    item->setSizeHint(QSize(0, 48));
    item->setSizeHint(QSize(0, 28));
    item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(peer_id));
    item->setData(Qt::UserRole + 1, nickname);
    item->setData(Qt::AccessibleTextRole, nickname + "，离线");
}

void MainWindow::selectAdjacentConversation(int delta) {
    if (conversation_list_ == nullptr || conversation_list_->count() == 0 ||
        delta == 0) {
        return;
    }
    const int count = conversation_list_->count();
    int index = conversation_list_->currentRow();
    if (index < 0) {
        index = delta > 0 ? -1 : count;
    }
    for (int step = 0; step < count; ++step) {
        index += delta;
        if (index < 0 || index >= count) {
            return;
        }
        QListWidgetItem* item = conversation_list_->item(index);
        if (item == nullptr || item->data(Qt::UserRole).toULongLong() == 0) {
            continue;
        }
        conversation_list_->setCurrentItem(item);
        onConversationClicked(item);
        return;
    }
}

// 打开本地历史库：目录取 settings/history/path，为空时用默认目录。
// 同一个账号重复打开是幂等的（HistoryStore 内部复用同一条连接）。
// 打开失败只记日志并保持 MessageStore 纯内存，聊天功能不受影响。
bool MainWindow::openHistoryStore(quint64 user_id) {
    if (user_id == 0) {
        return false;
    }
    QSettings settings;
    const QString configured =
        settings.value("settings/history/path").toString().trimmed();
    const QString directory = configured.isEmpty()
                                  ? chatbox::client::HistoryStore::defaultDirectory()
                                  : configured;
    QString error;
    if (!history_store_.open(directory, user_id, &error)) {
        message_store_.setHistoryStore(nullptr);
        qWarning() << QStringLiteral("本地历史库打开失败，聊天功能不受影响：%1")
                          .arg(error);
        return false;
    }
    message_store_.setHistoryStore(&history_store_);
    return true;
}

// 关闭本地历史库：先解绑 MessageStore，再关连接，
// 顺序反了就会让 MessageStore 持有已析构的库指针。
void MainWindow::closeHistoryStore() {
    message_store_.setHistoryStore(nullptr);
    history_store_.close();
}

// 切换会话时先用本地历史首屏渲染：在线历史回来后仍照旧覆盖这一屏。
void MainWindow::loadInitialHistory(chatbox::client::ChatContext context,
                                    quint64 target_id) {
    if (!history_store_.isOpen()) {
        return;
    }
    const QVector<chatbox::client::ChatMessage> local =
        history_store_.recent(context, target_id, 50);
    if (local.isEmpty()) {
        return;
    }
    message_store_.replaceHistory(local);
    // 分页游标由本地库决定：最旧一条的 id，再往前还有没有也查本地库。
    next_before_id_ = local.first().message_id;
    has_more_ = !history_store_.before(context, target_id, next_before_id_, 1)
                     .isEmpty();
    if (load_more_action_ != nullptr) {
        load_more_action_->setEnabled(has_more_);
    }
}

// 进入离线模式：只读地显示本机保存的聊天记录，全程不发任何请求帧。
// 只在已保存会话连不上/恢复失败时自动触发，后台继续退避重连。
bool MainWindow::enterOfflineMode(bool with_token) {
    if (user_id_ == 0) {
        return false;
    }
    if (!history_store_.isOpen() && !openHistoryStore(user_id_)) {
        return false;
    }
    if (!history_store_.hasMessages()) {
        // 本机没有这个账号的记录：不进入离线模式。
        closeHistoryStore();
        return false;
    }
    offline_mode_ = true;
    offline_session_ = with_token && !token_.isEmpty();
    exitJumpMode(false);
    if (!with_token) {
        // 纯本地浏览：不算登录态，也不参与自动重连。
        logged_in_ = false;
    }
    stack_->setCurrentWidget(chat_page_);
    updateOwnProfileStrip();
    setOnline(false, "离线模式 · 仅显示本地记录");

    if (context_type_ == ContextType::None) {
        populateOfflineSidebar();
        bool selected = false;
        if (room_list_ != nullptr && room_list_->count() > 0) {
            QListWidgetItem* first = room_list_->item(0);
            selectRoom(first->data(Qt::UserRole).toInt(),
                       first->data(Qt::UserRole + 1).toString());
            selected = true;
        }
        if (!selected && conversation_list_ != nullptr) {
            // 好友列表第一行可能是分组标题，按 user_id 跳过。
            for (int i = 0; i < conversation_list_->count(); ++i) {
                QListWidgetItem* item = conversation_list_->item(i);
                const quint64 peer_id = item->data(Qt::UserRole).toULongLong();
                if (peer_id == 0) {
                    continue;
                }
                selectPrivate(peer_id, item->data(Qt::UserRole + 1).toString());
                selected = true;
                break;
            }
        }
        if (!selected) {
            resetContext();
            setStatus("本机没有该账号的聊天记录");
            return true;
        }
    } else {
        // 已经选过会话：只把好友标成离线，保留名字，不动当前会话。
        for (auto it = friend_views_.begin(); it != friend_views_.end(); ++it) {
            it->online = false;
        }
        refreshFriendListView();
    }
    setStatus("离线模式 · 仅显示本地记录");
    return true;
}

// 退出离线模式：登录或恢复会话成功后调用。
void MainWindow::leaveOfflineMode() {
    offline_mode_ = false;
    offline_session_ = false;
    if (reconnect_button_ != nullptr) {
        reconnect_button_->hide();
    }
}

// 离线侧栏：本地存下来的会话全部列出，不做好友过滤
// （离线时没有好友列表，过滤会把唯一的副本藏起来）。
void MainWindow::populateOfflineSidebar() {
    if (room_list_ == nullptr) {
        return;
    }
    room_list_->clear();
    friend_views_.clear();
    friend_ids_.clear();
    if (!history_store_.isOpen()) {
        return;
    }
    const QVector<chatbox::client::ConversationRow> rows =
        history_store_.conversations();
    for (const chatbox::client::ConversationRow& row : rows) {
        if (row.context == chatbox::client::ChatContext::kRoom) {
            auto* item = new QListWidgetItem(row.title, room_list_);
            item->setIcon(
                QIcon(QPixmap::fromImage(chatbox::client::roomAvatar(36))));
            item->setSizeHint(QSize(0, 48));
            item->setData(Qt::UserRole, static_cast<uint>(row.target_id));
            item->setData(Qt::UserRole + 1, row.title);
            item->setData(Qt::AccessibleTextRole, row.title + "，离线记录");
            continue;
        }
        chatbox::client::FriendView view;
        view.user_id = row.target_id;
        view.nickname = row.title;
        view.online = false;
        view.unread = 0;
        view.last_content = row.last_content;
        view.last_time =
            row.last_time_utc.isValid()
                ? row.last_time_utc.toLocalTime().toString("yyyy-MM-dd HH:mm:ss")
                : QString();
        friend_views_.insert(row.target_id, view);
        peer_names_.insert(row.target_id, row.title);
    }
    refreshFriendListView();
}

// 清理或切换本地记录后重建离线视图：
// 重置当前会话，按本地库重载侧栏，再选中第一个可显示的会话。
// 会话被清空时保持重置状态。
// 本函数不写操作结果类的状态提示：状态由调用方在刷新之后给出，
// 否则「已迁移到新目录」会被这里的「已清空」盖掉。
void MainWindow::refreshOfflineAfterClear() {
    resetContext();
    populateOfflineSidebar();
    bool selected = false;
    if (room_list_ != nullptr && room_list_->count() > 0) {
        QListWidgetItem* first = room_list_->item(0);
        selectRoom(first->data(Qt::UserRole).toInt(),
                   first->data(Qt::UserRole + 1).toString());
        selected = true;
    }
    if (!selected && conversation_list_ != nullptr) {
        // 好友列表第一行可能是分组标题，按 user_id 跳过。
        for (int i = 0; i < conversation_list_->count(); ++i) {
            QListWidgetItem* item = conversation_list_->item(i);
            const quint64 peer_id = item->data(Qt::UserRole).toULongLong();
            if (peer_id == 0) {
                continue;
            }
            selectPrivate(peer_id, item->data(Qt::UserRole + 1).toString());
            selected = true;
            break;
        }
    }
    if (!selected) {
        // 没有可显示的会话：保持重置状态，提示交给调用方。
        resetContext();
        return;
    }
    setStatus("离线模式 · 仅显示本地记录");
}

// 离线翻页：从本地历史库取更早的一页。
void MainWindow::loadOlderFromLocal() {
    if (context_type_ == ContextType::None || !history_store_.isOpen()) {
        return;
    }
    chatbox::client::ChatContext context = chatbox::client::ChatContext::kRoom;
    quint64 target_id = 0;
    if (!currentChatContext(context, target_id)) {
        return;
    }
    quint64 oldest = 0;
    for (const chatbox::client::ChatMessage& message : message_store_.messages()) {
        if (message.message_id != 0 &&
            (oldest == 0 || message.message_id < oldest)) {
            oldest = message.message_id;
        }
    }
    if (oldest == 0) {
        has_more_ = false;
        if (load_more_action_ != nullptr) {
            load_more_action_->setEnabled(false);
        }
        setStatus("没有更早的本地记录");
        return;
    }
    const QVector<chatbox::client::ChatMessage> page =
        history_store_.before(context, target_id, oldest, 50);
    if (page.isEmpty()) {
        has_more_ = false;
        if (load_more_action_ != nullptr) {
            load_more_action_->setEnabled(false);
        }
        setStatus("没有更早的本地记录");
        return;
    }
    message_store_.prependHistory(page);
    const quint64 page_oldest = page.first().message_id;
    next_before_id_ = page_oldest;
    has_more_ = !history_store_.before(context, target_id, page_oldest, 1)
                     .isEmpty();
    if (load_more_action_ != nullptr) {
        load_more_action_->setEnabled(has_more_);
    }
    renderCurrentConversation(ScrollMode::kTop);
    setStatus(QString("已加载 %1 条更早的本地记录").arg(page.size()));
}

