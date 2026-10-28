#include "ui/main_window_support.h"

void MainWindow::insertMention() {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法提及成员", true);
        return;
    }
    if (context_type_ == ContextType::None) {
        setStatus("请先选择一个会话", true);
        return;
    }
    QString nickname;
    if (context_type_ == ContextType::Private) {
        nickname = current_peer_name_;
    } else if (member_list_ != nullptr && member_list_->count() > 0) {
        QMenu menu(this);
        for (int i = 0; i < member_list_->count(); ++i) {
            QListWidgetItem* item = member_list_->item(i);
            if (item == nullptr || item->data(Qt::UserRole).toULongLong() == user_id_) {
                continue;
            }
            QAction* action = menu.addAction(item->text());
            action->setData(item->text());
        }
        if (menu.isEmpty()) {
            setStatus("当前没有可提及的成员", true);
            return;
        }
        QAction* selected = menu.exec(QCursor::pos());
        if (selected == nullptr) {
            return;
        }
        nickname = selected->data().toString();
    }
    if (nickname.isEmpty()) {
        setStatus("当前没有可提及的成员", true);
        return;
    }
    input_edit_->insertPlainText("@" + nickname + " ");
    input_edit_->setFocus();
}

void MainWindow::setReplyTo(const chatbox::client::ChatMessage& message) {
    reply_to_id_ = message.message_id;
    reply_sender_name_ = message.outgoing ? QString("我") : message.sender_name;
    reply_content_ = message.recalled ? QString("消息已撤回") : message.content;
    if (reply_to_id_ == 0) {
        clearReply();
        return;
    }
    reply_label_->setText("回复 " + reply_sender_name_ + ": " + reply_content_);
    reply_label_->show();
    reply_cancel_button_->show();
    input_edit_->setFocus();
}

void MainWindow::clearReply() {
    reply_to_id_ = 0;
    reply_sender_name_.clear();
    reply_content_.clear();
    if (reply_label_ != nullptr) {
        reply_label_->hide();
    }
    if (reply_cancel_button_ != nullptr) {
        reply_cancel_button_->hide();
    }
}

void MainWindow::sendRecall(quint64 message_id) {
    if (!online_) {
        setStatus("当前离线，无法撤回", true);
        return;
    }
    if (context_type_ == ContextType::None || message_id == 0) {
        return;
    }
    if (!chatbox::client::confirmDanger(this, "撤回消息",
                                        "确定要撤回这条消息吗？", "撤回")) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdMessageRecall;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    if (context_type_ == ContextType::Room) {
        writer.putU8(1);
        writer.putU64(static_cast<quint64>(current_room_id_));
    } else {
        writer.putU8(2);
        writer.putU64(current_peer_id_);
    }
    writer.putU64(message_id);
    frame.body = writer.data();
    sendFrame(frame);
}

void MainWindow::showMessageMenu(quint64 message_id, const QString& client_msg_id,
                                 const QPoint& global_pos) {
    const chatbox::client::ChatMessage* message =
        message_id != 0 ? message_store_.findMessage(message_id)
                        : message_store_.findByClientId(client_msg_id);
    if (message == nullptr) {
        return;
    }
    chatbox::client::MessageActions actions =
        chatbox::client::messageActions(*message, user_id_);
    if (offline_mode_) {
        // 离线是只读视图：撤回与重发都不提供（复制与回复仍可用）。
        actions.can_recall = false;
        actions.can_retry = false;
    }

    QMenu menu(this);
    QAction* retry_action = nullptr;
    QAction* reply_action = nullptr;
    QAction* recall_action = nullptr;
    if (actions.can_retry) {
        retry_action = menu.addAction(QString::fromUtf8("重发"));
    }
    if (actions.can_reply) {
        reply_action = menu.addAction(QString::fromUtf8("回复"));
    }
    if (actions.can_recall) {
        recall_action = menu.addAction(QString::fromUtf8("撤回"));
    }
    const bool has_selection = chat_view_->textCursor().hasSelection();
    const bool can_copy = has_selection || !message->content.isEmpty();
    if ((retry_action != nullptr || reply_action != nullptr ||
         recall_action != nullptr) &&
        can_copy) {
        menu.addSeparator();
    }
    QAction* copy_action = nullptr;
    if (can_copy) {
        copy_action = menu.addAction(has_selection ? QString::fromUtf8("复制选中")
                                                   : QString::fromUtf8("复制"));
    }
    if (menu.isEmpty()) {
        return;
    }

    QAction* chosen = menu.exec(global_pos);
    if (chosen == nullptr) {
        return;
    }
    if (chosen == retry_action) {
        retryMessage(message->client_msg_id);
    } else if (chosen == reply_action) {
        setReplyTo(*message);
    } else if (chosen == recall_action) {
        sendRecall(message_id);
    } else if (chosen == copy_action) {
        copyMessageText(*message);
    }
}

void MainWindow::copyMessageText(const chatbox::client::ChatMessage& message) {
    const QTextCursor selection = chat_view_->textCursor();
    if (selection.hasSelection()) {
        QApplication::clipboard()->setText(selection.selectedText());
        return;
    }
    QApplication::clipboard()->setText(message.content);
}

void MainWindow::openMessageSearch() {
    if (context_type_ == ContextType::None) {
        setStatus("请先选择一个会话", true);
        return;
    }
    if (message_search_dialog_ == nullptr) {
        message_search_dialog_ = new MessageSearchDialog(this);
        connect(message_search_dialog_, &MessageSearchDialog::searchRequested, this,
                &MainWindow::requestMessageSearch);
    }
    message_search_dialog_->clearResults();
    message_search_dialog_->show();
    message_search_dialog_->raise();
    message_search_dialog_->activateWindow();
}

void MainWindow::requestMessageSearch(const QString& keyword) {
    if (message_search_dialog_ == nullptr || context_type_ == ContextType::None) {
        return;
    }
    if (keyword.toUtf8().size() > 64) {
        message_search_dialog_->setBusy(false);
        message_search_dialog_->setStatus("关键词不能超过 64 字节", true);
        return;
    }
    if (offline_mode_) {
        // 离线搜索只查本地库：不发任何帧，也不占用服务端序号。
        chatbox::client::ChatContext context = chatbox::client::ChatContext::kRoom;
        quint64 target_id = 0;
        if (!currentChatContext(context, target_id) || !history_store_.isOpen()) {
            message_search_dialog_->setBusy(false);
            message_search_dialog_->setStatus("没有可搜索的本地记录", true);
            return;
        }
        const QVector<chatbox::client::ChatMessage> hits =
            history_store_.search(context, target_id, keyword, 50);
        QVector<MessageSearchResult> results;
        results.reserve(hits.size());
        for (const chatbox::client::ChatMessage& message : hits) {
            MessageSearchResult result;
            result.message_id = message.message_id;
            result.sender_id = message.sender_id;
            result.nickname = message.sender_name;
            result.content = message.recalled ? QString() : message.content;
            result.created_at =
                message.created_at_utc.toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
            result.recalled = message.recalled;
            results.push_back(result);
        }
        message_search_dialog_->setResults(results);
        message_search_dialog_->setBusy(false);
        if (results.isEmpty()) {
            message_search_dialog_->setStatus("本地记录中没有匹配消息");
        } else {
            message_search_dialog_->setStatus(
                QString("本地记录 · %1 条").arg(results.size()));
        }
        return;
    }
    const quint32 seq = next_seq_++;
    message_search_seq_ = seq;
    message_search_dialog_->setBusy(true);
    message_search_dialog_->setStatus("正在搜索...");

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdMessageSearch;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    if (context_type_ == ContextType::Room) {
        writer.putU8(1);
        writer.putU64(static_cast<quint64>(current_room_id_));
    } else {
        writer.putU8(2);
        writer.putU64(current_peer_id_);
    }
    writer.putString(chatbox::toUtf8(keyword));
    writer.putU16(50);
    frame.body = writer.data();
    if (!sendFrame(frame)) {
        message_search_seq_ = 0;
        message_search_dialog_->setBusy(false);
        message_search_dialog_->setStatus("网络未连接，无法搜索", true);
        return;
    }
    if (message_search_timeout_timer_ != nullptr) {
        message_search_timeout_timer_->start();
    }
}

void MainWindow::requestMembers(int room_id) {
    if (offline_mode_) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdRoomMembers;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    frame.body = writer.data();
    sendFrame(frame);
}

void MainWindow::requestHistory(int kind, quint64 target, quint64 before_id) {
    if (offline_mode_) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdHistory;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU8(static_cast<uint8_t>(kind));
    writer.putU64(target);
    writer.putU64(before_id);
    writer.putU16(50);
    frame.body = writer.data();
    loading_older_history_ = before_id != 0;
    load_more_action_->setEnabled(false);
    sendFrame(frame);
}

void MainWindow::onSendClicked() {
    if (context_type_ == ContextType::None) {
        setStatus("请先选择一个会话", true);
        return;
    }
    if (!online_) {
        setStatus("当前离线，无法发送", true);
        return;
    }
    const QString text = input_edit_->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }
    if (text.toUtf8().size() > 2000) {
        setStatus("消息不能超过 2000 字节", true);
        return;
    }

    const QString client_msg_id =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    chatbox::client::ChatMessage message;
    message.client_msg_id = client_msg_id;
    message.context = context_type_ == ContextType::Room
                          ? chatbox::client::ChatContext::kRoom
                          : chatbox::client::ChatContext::kPrivate;
    message.target_id = context_type_ == ContextType::Room
                            ? static_cast<quint64>(current_room_id_)
                            : current_peer_id_;
    message.sender_id = user_id_;
    message.sender_name = nickname_;
    message.content = text;
    message.created_at_utc = QDateTime::currentDateTimeUtc();
    message.reply_to_id = reply_to_id_;
    message.reply_sender_name = reply_sender_name_;
    message.reply_content = reply_content_;
    message.outgoing = true;
    message.status = chatbox::client::MessageStatus::kSending;

    outbox_.add(message, message.created_at_utc);
    message_store_.addPending(message);
    input_edit_->clear();
    updateMessageByteCounter();
    renderCurrentConversation(ScrollMode::kBottom);
    sendMessage(message);
    clearReply();
}

void MainWindow::sendMessage(const chatbox::client::ChatMessage& message) {
    chatbox::Frame frame;
    chatbox::ByteWriter writer;
    if (message.context == chatbox::client::ChatContext::kRoom) {
        frame.header.cmd = chatbox::kCmdChatSend;
        writer.putU32(static_cast<uint32_t>(message.target_id));
    } else {
        frame.header.cmd = chatbox::kCmdPrivateSend;
        writer.putU64(message.target_id);
    }
    writer.putString(chatbox::toUtf8(message.client_msg_id));
    writer.putString(chatbox::toUtf8(message.content));
    writer.putU64(message.reply_to_id);
    writer.putString(chatbox::toUtf8(message.reply_sender_name));
    writer.putString(chatbox::toUtf8(message.reply_content));
    frame.header.seq = next_seq_++;
    frame.body = writer.data();

    pending_sequences_.insert(frame.header.seq, message.client_msg_id);
    setStatus("正在发送...");
    if (!sendFrame(frame)) {
        pending_sequences_.remove(frame.header.seq);
        outbox_.markFailed(message.client_msg_id);
        message_store_.markFailed(message.client_msg_id);
        renderCurrentConversation(ScrollMode::kPreserve);
        setStatus("发送失败：网络未连接", true);
        qWarning("send failed: network unavailable client_msg_id=%s",
                 qPrintable(message.client_msg_id));
    }
}

void MainWindow::retryMessage(const QString& client_msg_id) {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法重发", true);
        return;
    }
    const QVector<chatbox::client::ChatMessage> pending = outbox_.pending();
    for (const chatbox::client::ChatMessage& message : pending) {
        if (message.client_msg_id != client_msg_id) {
            continue;
        }
        if (message.status != chatbox::client::MessageStatus::kFailed) {
            return;
        }
        const QDateTime now = QDateTime::currentDateTimeUtc();
        if (!outbox_.markSending(client_msg_id, now)) {
            return;
        }
        message_store_.markSending(client_msg_id);
        renderCurrentConversation(ScrollMode::kPreserve);
        sendMessage(message);
        return;
    }
}

void MainWindow::onSendTimeout() {
    const QVector<QString> expired =
        outbox_.expireSending(QDateTime::currentDateTimeUtc(), 10);
    if (expired.isEmpty()) {
        return;
    }
    bool current_changed = false;
    for (const QString& client_msg_id : expired) {
        if (message_store_.markFailed(client_msg_id)) {
            current_changed = true;
        }
    }
    if (current_changed) {
        renderCurrentConversation(ScrollMode::kPreserve);
    }
    setStatus("消息发送超时，请点击重发", true);
}

void MainWindow::onAuthTimeout() {
    if (resuming_) {
        resuming_ = false;
        if (offline_mode_) {
            // 离线模式下的恢复超时：留在离线视图，继续退避重连。
            setOnline(false, "离线模式 · 仅显示本地记录");
            setStatus("恢复会话超时，正在重连...", true);
            if (network_ != nullptr) {
                network_->requestStop();
            }
            scheduleReconnect();
            return;
        }
        if (enterOfflineMode(true)) {
            setStatus("恢复会话超时，已进入离线模式（仅显示本地记录）", true);
            if (network_ != nullptr) {
                network_->requestStop();
            }
            scheduleReconnect();
            return;
        }
        setOnline(false, "恢复会话超时，正在重连...");
        setStatus("恢复会话超时，正在重连...", true);
        login_page_->setBusy(false);
        login_page_->setStatus("恢复会话超时，请检查网络后重试", true);
        stack_->setCurrentWidget(login_page_);
        if (network_ != nullptr) {
            network_->requestStop();
        }
        scheduleReconnect();
        return;
    }
    if (pending_auth_ != PendingAuth::None) {
        pending_auth_ = PendingAuth::None;
        login_page_->setBusy(false);
        login_page_->setStatus("服务器响应超时，请重试", true);
        setStatus("服务器响应超时，请重试", true);
        if (network_ != nullptr) {
            network_->requestStop();
        }
    }
}

void MainWindow::restorePendingForCurrentContext() {
    chatbox::client::ChatContext context;
    quint64 target_id = 0;
    if (!currentChatContext(context, target_id)) {
        return;
    }
    for (const chatbox::client::ChatMessage& message : outbox_.pending()) {
        if (message.context == context && message.target_id == target_id) {
            message_store_.addPending(message);
        }
    }
}

void MainWindow::clearStaleListSelection(ContextType keep) {
    // clearSelection() alone leaves the current index, and the delegate paints
    // State_Selected from the view's selection model, so both have to go.
    if (keep != ContextType::Room && room_list_ != nullptr) {
        room_list_->clearSelection();
        room_list_->setCurrentRow(-1);
    }
    if (keep != ContextType::Private && conversation_list_ != nullptr) {
        conversation_list_->clearSelection();
        conversation_list_->setCurrentRow(-1);
    }
}

void MainWindow::selectRoom(int room_id, const QString& name) {
    context_type_ = ContextType::Room;
    clearStaleListSelection(ContextType::Room);
    updateRightPanelVisibility();
    current_room_id_ = room_id;
    current_peer_id_ = 0;
    current_peer_name_ = name;
    context_label_->setText(name);
    if (offline_mode_) {
        mention_button_->hide();
    } else {
        mention_button_->show();
    }
    if (context_avatar_ != nullptr) {
        context_avatar_->setPixmap(
            QPixmap::fromImage(chatbox::client::roomAvatar(32)));
        context_avatar_->setCursor(Qt::ArrowCursor);
    }
    chat_view_->clear();
    if (input_edit_ != nullptr) {
        input_edit_->clear();
    }
    clearReply();
    if (history_store_.isOpen()) {
        history_store_.saveConversation(
            conversationRow(chatbox::client::ChatContext::kRoom,
                            static_cast<quint64>(room_id), name));
    }
    message_store_.reset(chatbox::client::ChatContext::kRoom,
                         static_cast<quint64>(room_id));
    // 分页状态先复位，再由本地首屏决定：
    // 离线时「还有没有更早的消息」完全由本地库说了算。
    load_more_action_->setEnabled(false);
    has_more_ = false;
    loading_older_history_ = false;
    next_before_id_ = 0;
    loadInitialHistory(chatbox::client::ChatContext::kRoom,
                       static_cast<quint64>(room_id));
    restorePendingForCurrentContext();
    renderCurrentConversation(ScrollMode::kBottom);
    member_list_->clear();
    if (member_count_label_ != nullptr) {
        member_count_label_->setText("在线成员 (0)");
    }

    if (offline_mode_) {
        // 离线只读：不发 JOIN、成员、头像、历史等任何请求帧。
        input_edit_->setEnabled(false);
        send_button_->setEnabled(false);
        mention_button_->hide();
        setStatus("离线模式 · 仅显示本地记录");
        return;
    }

    chatbox::Frame join;
    join.header.cmd = chatbox::kCmdJoinRoom;
    join.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    join.body = writer.data();
    sendFrame(join);

    requestMembers(room_id);
    requestHistory(1, static_cast<quint64>(room_id), 0);
    input_edit_->setEnabled(online_);
    send_button_->setEnabled(online_);
    updateMessageByteCounter();
    // The chat header already names the room; a persistent status-bar line
    // would just repeat it until the next unrelated event.
}

void MainWindow::selectPrivate(quint64 peer_id, const QString& nickname) {
    context_type_ = ContextType::Private;
    clearStaleListSelection(ContextType::Private);
    updateRightPanelVisibility();
    current_room_id_ = 0;
    current_peer_id_ = peer_id;
    current_peer_name_ = nickname;
    peer_names_.insert(peer_id, nickname);
    context_label_->setText(nickname);
    mention_button_->hide();
    updatePrivateHeaderAvatar(peer_id, nickname);
    context_avatar_->setCursor(Qt::PointingHandCursor);
    if (!offline_mode_) {
        requestPeerAvatar(peer_id);
    }
    chat_view_->clear();
    if (input_edit_ != nullptr) {
        input_edit_->clear();
    }
    clearReply();
    if (history_store_.isOpen()) {
        history_store_.saveConversation(
            conversationRow(chatbox::client::ChatContext::kPrivate, peer_id,
                            nickname));
    }
    message_store_.reset(chatbox::client::ChatContext::kPrivate, peer_id);
    // 分页状态先复位，再由本地首屏决定（见 selectRoom）。
    load_more_action_->setEnabled(false);
    has_more_ = false;
    loading_older_history_ = false;
    next_before_id_ = 0;
    loadInitialHistory(chatbox::client::ChatContext::kPrivate, peer_id);
    restorePendingForCurrentContext();
    renderCurrentConversation(ScrollMode::kBottom);
    member_list_->clear();
    if (member_count_label_ != nullptr) {
        member_count_label_->setText("在线成员 (0)");
    }
    if (QListWidgetItem* item = findConversationItem(peer_id)) {
        item->setData(Qt::UserRole + 2, 0);
        QString text = item->text();
        if (text.startsWith('(')) {
            const int end = text.indexOf(") ");
            if (end > 0) {
                text = text.mid(end + 2);
            }
        }
        item->setText(text);
        refreshUnreadTitle();
    }
    if (offline_mode_) {
        // 离线只读：不发头像、历史等任何请求帧。
        input_edit_->setEnabled(false);
        send_button_->setEnabled(false);
        mention_button_->hide();
        setStatus("离线模式 · 仅显示本地记录");
        return;
    }
    requestHistory(2, peer_id, 0);
    input_edit_->setEnabled(online_);
    send_button_->setEnabled(online_);
    updateMessageByteCounter();
    setStatus("正在加载与 " + nickname + " 的私聊历史");
}









// 离线模式下的「重试连接」：没有 token 就没什么可重试的。
void MainWindow::retryConnection() {
    if (!offline_session_ || token_.isEmpty()) {
        return;
    }
    startConnection();
    setStatus("正在重连...");
}

void MainWindow::onLoadMoreClicked() {
    if (!has_more_ || context_type_ == ContextType::None) {
        return;
    }
    if (offline_mode_) {
        // 离线翻页走本地库，不发历史请求。
        loadOlderFromLocal();
        return;
    }
    if (context_type_ == ContextType::Room) {
        requestHistory(1, static_cast<quint64>(current_room_id_), next_before_id_);
    } else {
        requestHistory(2, current_peer_id_, next_before_id_);
    }
}

void MainWindow::updateMessageByteCounter() {
    if (input_edit_ == nullptr || message_byte_counter_ == nullptr ||
        send_button_ == nullptr) {
        return;
    }
    const int bytes = input_edit_->toPlainText().toUtf8().size();
    const bool over_limit = bytes > 2000;
    if (bytes <= 1800) {
        message_byte_counter_->hide();
    } else {
        message_byte_counter_->setText(
            QString("已用 %1/2000 字节").arg(bytes));
        message_byte_counter_->setStyleSheet(
            QString("font-size: 11px; color: %1;")
                .arg(QString::fromLatin1(over_limit ? theme::kSemanticDown
                                                    : theme::kMuted)));
        message_byte_counter_->show();
    }
    send_button_->setEnabled(
        !over_limit && online_ && !offline_mode_ &&
        context_type_ != ContextType::None);
}

void MainWindow::updateChatEmptyState() {
    if (chat_stack_ == nullptr || chat_empty_state_ == nullptr) {
        return;
    }
    if (context_type_ == ContextType::None) {
        chat_empty_state_->setTitle("选择一个会话开始聊天");
        chat_empty_state_->setDescription(
            "从左侧选择群聊或好友；按 Ctrl+Shift+F 搜索用户");
        chat_stack_->setCurrentWidget(chat_empty_state_);
        return;
    }
    if (message_store_.messages().isEmpty()) {
        chat_empty_state_->setTitle("还没有消息");
        chat_empty_state_->setDescription("发送第一条消息，打个招呼吧");
        chat_stack_->setCurrentWidget(chat_empty_state_);
        return;
    }
    chat_stack_->setCurrentWidget(chat_view_);
}

void MainWindow::resetContext() {
    context_type_ = ContextType::None;
    clearStaleListSelection(ContextType::None);
    updateRightPanelVisibility();
    current_room_id_ = 0;
    current_peer_id_ = 0;
    current_peer_name_.clear();
    message_store_.reset(chatbox::client::ChatContext::kRoom, 0);
    has_more_ = false;
    loading_older_history_ = false;
    next_before_id_ = 0;
    if (input_edit_ != nullptr) {
        input_edit_->clear();
    }
    updateMessageByteCounter();
    if (mention_button_ != nullptr) {
        mention_button_->hide();
    }
    clearReply();
    if (context_label_ != nullptr) {
        context_label_->setText("请选择一个会话");
    }
    if (context_avatar_ != nullptr) {
        context_avatar_->setPixmap(avatarIcon("C", 0).pixmap(32, 32));
        context_avatar_->setCursor(Qt::ArrowCursor);
    }
    if (chat_view_ != nullptr) {
        chat_view_->clear();
    }
    if (member_list_ != nullptr) {
        member_list_->clear();
    }
    if (member_count_label_ != nullptr) {
        member_count_label_->setText("在线成员 (0)");
    }
    if (load_more_action_ != nullptr) {
        load_more_action_->setEnabled(false);
    }
    updateChatEmptyState();
}

void MainWindow::renderCurrentConversation(ScrollMode mode) {
    QScrollBar* bar = chat_view_->verticalScrollBar();
    const int previous = bar->value();
    QVector<chatbox::client::MessageKey> rendered_messages;
    chat_view_->setConversationHtml(
        chatbox::client::renderConversationHtml(message_store_.messages(), user_id_,
                                                 &rendered_messages),
        rendered_messages);
    switch (mode) {
        case ScrollMode::kBottom:
            bar->setValue(bar->maximum());
            break;
        case ScrollMode::kTop:
            bar->setValue(0);
            break;
        case ScrollMode::kPreserve:
            bar->setValue(std::min(previous, bar->maximum()));
            break;
    }
    updateChatEmptyState();
}

void MainWindow::addIncomingMessage(const chatbox::client::ChatMessage& message) {
    if (!message_store_.isCurrent(message.context, message.target_id)) {
        return;
    }
    QScrollBar* bar = chat_view_->verticalScrollBar();
    const bool at_bottom = bar->value() >= bar->maximum() - 8;
    const QVector<chatbox::client::ChatMessage>& messages = message_store_.messages();
    const bool same_day =
        !messages.isEmpty() &&
        chatbox::client::formatLocalDate(messages.last().created_at_utc) ==
            chatbox::client::formatLocalDate(message.created_at_utc);
    const bool appended = message_store_.appendIncoming(message);
    if (appended && at_bottom && same_day) {
        chatbox::client::MessageKey key;
        key.message_id = message.message_id;
        key.client_msg_id = message.client_msg_id;
        key.bubble_color =
            chatbox::client::messageBubbleColor(message, user_id_);
        const bool show_sender_name =
            messages.isEmpty() ||
            chatbox::client::shouldShowSenderName(&messages.last(), message);
        chat_view_->appendFragment(
            chatbox::client::renderMessageFragment(message, user_id_, false, QString(),
                                                   show_sender_name),
            key);
        bar->setValue(bar->maximum());
        updateChatEmptyState();
        return;
    }
    renderCurrentConversation(at_bottom ? ScrollMode::kBottom : ScrollMode::kPreserve);
}

bool MainWindow::currentChatContext(chatbox::client::ChatContext& context,
                                    quint64& target_id) const {
    if (context_type_ == ContextType::Room) {
        context = chatbox::client::ChatContext::kRoom;
        target_id = static_cast<quint64>(current_room_id_);
        return true;
    }
    if (context_type_ == ContextType::Private) {
        context = chatbox::client::ChatContext::kPrivate;
        target_id = current_peer_id_;
        return true;
    }
    return false;
}
