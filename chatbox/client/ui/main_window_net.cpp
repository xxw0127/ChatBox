#include "ui/main_window_support.h"

#include "common/file_protocol.h"
#include "common/group_protocol.h"
#include "core/file_transfer_manager.h"
#include "core/file_types.h"
#include "core/group_types.h"
#include "ui/group_create_dialog.h"
#include "ui/group_join_dialog.h"
#include "ui/group_notify_dialog.h"
#include "ui/group_settings_dialog.h"

bool MainWindow::sendGroupCommand(quint16 command, const std::string& body) {
    chatbox::Frame frame;
    frame.header.cmd = command;
    frame.header.seq = next_seq_++;
    frame.body = body;
    pending_group_commands_.insert(frame.header.seq, command);
    return sendFrame(frame);
}

void MainWindow::requestGroupList() {
    if (offline_mode_) {
        return;
    }
    sendGroupCommand(chatbox::kCmdGroupList, {});
}

void MainWindow::requestGroupMembers(quint64 room_id) {
    if (offline_mode_ || room_id == 0) {
        return;
    }
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    sendGroupCommand(chatbox::kCmdGroupMemberList, writer.data());
}

void MainWindow::requestGroupRequests(quint64 room_id) {
    if (offline_mode_) {
        return;
    }
    chatbox::ByteWriter writer;
    writer.putU32(static_cast<uint32_t>(room_id));
    sendGroupCommand(chatbox::kCmdGroupRequestList, writer.data());
}

void MainWindow::handleGroupList(const QByteArray& body) {
    std::vector<chatbox::GroupInfo> wire_groups;
    if (!chatbox::decodeGroupList(
            std::string(body.constData(), static_cast<std::size_t>(body.size())),
            wire_groups)) {
        setStatus("群列表格式错误", true);
        return;
    }
    QVector<chatbox::client::GroupInfo> groups;
    groups.reserve(static_cast<int>(wire_groups.size()));
    for (const chatbox::GroupInfo& wire : wire_groups) {
        groups.push_back(chatbox::client::groupInfoFromWire(wire));
    }
    group_store_.replaceAll(groups);
    if (history_store_.isOpen()) {
        // Groups that were left or removed disappear from the server list but
        // their local messages are retained. Keep a read-only tombstone so the
        // user can still reach that history.
        for (const chatbox::client::ConversationRow& row :
             history_store_.conversations()) {
            if (row.context != chatbox::client::ChatContext::kRoom ||
                group_store_.find(row.target_id) != nullptr) {
                continue;
            }
            chatbox::client::GroupInfo tombstone;
            tombstone.room_id = row.target_id;
            tombstone.name = row.title;
            tombstone.status = 2;
            group_store_.upsert(tombstone);
        }
    }
    for (auto it = group_requests_by_room_.begin();
         it != group_requests_by_room_.end();) {
        const chatbox::client::GroupInfo* group = group_store_.find(it.key());
        if (group == nullptr || group->status != 0 || group->role < 1) {
            it = group_requests_by_room_.erase(it);
        } else {
            ++it;
        }
    }
    QSet<quint64> active_ids;
    for (const chatbox::client::GroupInfo& group : group_store_.active()) {
        if (group.role >= 1 &&
            !group_requests_by_room_.contains(group.room_id)) {
            requestGroupRequests(group.room_id);
        }
        active_ids.insert(group.room_id);
        if (joined_groups_.contains(group.room_id)) {
            continue;
        }
        chatbox::ByteWriter writer;
        writer.putU32(static_cast<uint32_t>(group.room_id));
        chatbox::Frame join;
        join.header.cmd = chatbox::kCmdJoinRoom;
        join.header.seq = next_seq_++;
        join.body = writer.data();
        if (sendFrame(join)) {
            joined_groups_.insert(group.room_id);
        }
    }
    joined_groups_.intersect(active_ids);
    refreshGroupListView();
    if (context_type_ == ContextType::Room && current_room_id_ != 0) {
        const chatbox::client::GroupInfo* current =
            group_store_.find(static_cast<quint64>(current_room_id_));
        if (current == nullptr) {
            // Removed, left, or the group is gone: the server already revoked
            // access, so drop the stale context instead of letting the user
            // keep typing into a room that rejects every frame.
            resetContext();
        } else if (current->status != 0) {
            selectDissolvedGroup(current->room_id, current->name);
        } else if (!offline_mode_ && input_edit_ != nullptr &&
                   !input_edit_->isEnabled()) {
            // The user was reading a left/removed tombstone when a new
            // invitation or approval made this group active again.
            selectGroup(current->room_id, current->name);
        }
    }
    if (context_type_ == ContextType::None) {
        const QVector<chatbox::client::GroupInfo> active = group_store_.active();
        for (const chatbox::client::GroupInfo& group : active) {
            if (group.is_default) {
                selectGroup(group.room_id, group.name);
                break;
            }
        }
    }
}

void MainWindow::handleGroupMemberList(const QByteArray& body) {
    uint32_t room_id = 0;
    std::vector<chatbox::GroupMemberInfo> wire_members;
    if (!chatbox::decodeGroupMembers(
            std::string(body.constData(), static_cast<std::size_t>(body.size())),
            room_id, wire_members)) {
        setStatus("群成员格式错误", true);
        return;
    }
    QVector<chatbox::client::GroupMemberInfo> members;
    members.reserve(static_cast<int>(wire_members.size()));
    for (const chatbox::GroupMemberInfo& wire : wire_members) {
        members.push_back(chatbox::client::groupMemberFromWire(wire));
    }
    group_store_.setMembers(room_id, members);
    if (group_settings_dialog_ != nullptr &&
        room_id == static_cast<uint32_t>(current_room_id_)) {
        group_settings_dialog_->setMembers(members);
    }
    if (context_type_ != ContextType::Room ||
        room_id != static_cast<uint32_t>(current_room_id_)) {
        return;
    }
    member_list_->clear();
    for (const chatbox::client::GroupMemberInfo& member : members) {
        QString text = member.nickname;
        if (member.role == 2) {
            text += " · 群主";
        } else if (member.role == 1) {
            text += " · 管理员";
        }
        auto* item = new QListWidgetItem(text, member_list_);
        item->setIcon(memberAvatarIcon(member.user_id, member.nickname, 28));
        item->setSizeHint(QSize(0, 48));
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(member.user_id));
        item->setData(Qt::UserRole + 1, member.nickname);
        item->setData(Qt::UserRole + 2, member.role);
        item->setData(Qt::UserRole + 3, member.online);
        item->setData(Qt::AccessibleTextRole,
                      member.nickname + (member.online ? "，在线" : "，离线"));
        if (member.user_id == user_id_) {
            if (!avatar_cache_.contains(member.user_id)) {
                requestAvatar(member.user_id, 0);
            }
        } else {
            requestPeerAvatar(member.user_id);
        }
    }
    const chatbox::client::GroupInfo* group =
        group_store_.find(static_cast<quint64>(room_id));
    if (member_count_label_ != nullptr) {
        member_count_label_->setText(
            group != nullptr
                ? QString("%1 · %2 人").arg(group->name).arg(members.size())
                : QString("成员 (%1)").arg(members.size()));
    }
}

void MainWindow::handleGroupRequestList(const QByteArray& body) {
    uint32_t room_id = 0;
    std::vector<chatbox::GroupRequestInfo> wire_requests;
    if (!chatbox::decodeGroupRequests(
            std::string(body.constData(), static_cast<std::size_t>(body.size())),
            room_id, wire_requests)) {
        setStatus("群申请格式错误", true);
        return;
    }
    QVector<chatbox::client::GroupRequestInfo> requests;
    requests.reserve(static_cast<int>(wire_requests.size()));
    for (const chatbox::GroupRequestInfo& wire : wire_requests) {
        requests.push_back(chatbox::client::groupRequestFromWire(wire));
    }
    if (room_id == 0) {
        group_invites_ = requests;
    } else {
        group_requests_by_room_.insert(static_cast<quint64>(room_id), requests);
    }
    updateGroupNoticeBadge();
    if (group_notify_dialog_ != nullptr) {
        const int role = group_store_.find(static_cast<quint64>(current_room_id_))
                             ? group_store_.find(static_cast<quint64>(current_room_id_))->role
                             : 0;
        group_notify_dialog_->setData(pendingGroupRequests(), group_invites_,
                                      role >= 1);
    }
}

void MainWindow::handleGroupPush(const QByteArray& body) {
    chatbox::ByteReader reader(body.constData(),
                               static_cast<std::size_t>(body.size()));
    uint8_t event = 0;
    uint32_t room_id = 0;
    uint64_t user_id = 0;
    uint8_t role = 0;
    if (!reader.getU8(event) || !reader.getU32(room_id) ||
        !reader.getU64(user_id) || !reader.getU8(role)) {
        return;
    }
    Q_UNUSED(role);
    requestGroupList();
    const bool room_removed =
        event == 3 || (event == 5 && user_id == user_id_);
    if (!room_removed && context_type_ == ContextType::Room &&
        room_id == static_cast<uint32_t>(current_room_id_)) {
        requestGroupMembers(current_room_id_);
    }
    if (event == 7 || event == 8) {
        requestGroupRequests(0);
        const chatbox::client::GroupInfo* group =
            group_store_.find(static_cast<quint64>(room_id));
        if (group != nullptr && group->role >= 1) {
            requestGroupRequests(static_cast<quint64>(room_id));
        }
    }
}

void MainWindow::onLoginRequested(const QString& host, quint16 port, const QString& username,
                                  const QString& password, bool tls) {
    logging_out_ = false;
    logout_finishing_ = false;
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
        reconnect_scheduled_ = false;
    }
    if (host != QString::fromLatin1(chatbox::client::kServerHost) ||
        port != chatbox::client::kServerPort ||
        tls != chatbox::client::kServerTls) {
        qWarning("ignoring non-production endpoint from login page");
    }
    host_ = QString::fromLatin1(chatbox::client::kServerHost);
    port_ = chatbox::client::kServerPort;
    use_tls_ = chatbox::client::kServerTls;
    username_ = username;
    password_ = password;
    pending_auth_ = PendingAuth::Login;
    login_page_->setBusy(true);
    if (connected_) {
        sendLogin();
    } else {
        startConnection();
    }
}

void MainWindow::onRegisterRequested(const QString& host, quint16 port,
                                     const QString& username, const QString& password,
                                     const QString& nickname, bool tls) {
    logging_out_ = false;
    logout_finishing_ = false;
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
        reconnect_scheduled_ = false;
    }
    if (host != QString::fromLatin1(chatbox::client::kServerHost) ||
        port != chatbox::client::kServerPort ||
        tls != chatbox::client::kServerTls) {
        qWarning("ignoring non-production endpoint from register page");
    }
    host_ = QString::fromLatin1(chatbox::client::kServerHost);
    port_ = chatbox::client::kServerPort;
    use_tls_ = chatbox::client::kServerTls;
    username_ = username;
    password_ = password;
    nickname_ = nickname;
    pending_auth_ = PendingAuth::Register;
    login_page_->setBusy(true);
    if (connected_) {
        sendRegister();
    } else {
        startConnection();
    }
}

void MainWindow::startConnection() {
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
        reconnect_scheduled_ = false;
    }
    if (network_ != nullptr) {
        disconnect(network_, nullptr, this, nullptr);
        network_->requestStop();
        network_->wait();
        delete network_;
        network_ = nullptr;
    }

    const quint64 generation = ++connection_generation_;
    const QString ca_path = QCoreApplication::applicationDirPath() + "/certs/ca.crt";
    network_ = new NetworkThread(host_, port_, use_tls_, ca_path, this);
    NetworkThread* net = network_;
    connect(net, &NetworkThread::connected, this, [this, net, generation] {
        if (network_ == net && generation == connection_generation_) {
            onConnected();
        }
    });
    connect(net, &NetworkThread::disconnected, this,
            [this, net, generation](const QString& reason) {
                if (network_ == net && generation == connection_generation_) {
                    onDisconnected(reason);
                }
            });
    connect(net, &NetworkThread::frameReceived, this,
            [this, net, generation](const QByteArray& data) {
                if (network_ == net && generation == connection_generation_) {
                    onFrameReceived(data);
                }
            });
    network_->start();
    setStatus("正在连接 " + host_ + ":" + QString::number(port_) + "...");
}

void MainWindow::onConnected() {
    connected_ = true;
    qInfo("connected to %s:%u tls=%d", qPrintable(host_),
          static_cast<unsigned>(port_), use_tls_ ? 1 : 0);
    setOnline(false, "已连接，正在恢复会话...");
    // 待处理的登录/注册优先：恢复会话失败后用户可能带着旧 token 停在登录页，
    // 这时手动登录必须发 LOGIN 而不是 RESUME。
    if (pending_auth_ == PendingAuth::Login) {
        sendLogin();
    } else if (pending_auth_ == PendingAuth::Register) {
        sendRegister();
    } else if (logged_in_ && !token_.isEmpty()) {
        setStatus("已连接，正在恢复会话...");
        sendResume();
    } else {
        setStatus("已连接");
    }
}

void MainWindow::onDisconnected(const QString& reason) {
    connected_ = false;
    qWarning("disconnected: %s", qPrintable(reason));
    resuming_ = false;
    // Presence is connection-scoped: the new connection must JOIN every
    // active group again, and any in-flight search/history answers are stale.
    joined_groups_.clear();
    pending_group_commands_.clear();
    panel_search_seq_ = 0;
    history_request_seq_ = 0;
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->stop();
    }
    const bool update_was_pending = pending_profile_update_seq_ != 0;
    pending_profile_update_seq_ = 0;
    pending_profile_requests_.clear();
    profile_request_seqs_.clear();
    pending_profile_seqs_.clear();
    pending_avatar_requests_.clear();
    avatar_request_seqs_.clear();
    pending_avatar_seqs_.clear();
    pending_avatar_versions_.clear();
    friend_request_seqs_.clear();
    if (profile_timeout_timer_ != nullptr) {
        profile_timeout_timer_->stop();
    }
    if (avatar_timeout_timer_ != nullptr) {
        avatar_timeout_timer_->stop();
    }
    if (profile_dialog_ != nullptr) {
        profile_dialog_->setBusy(false);
        profile_dialog_->setStatus(
            update_was_pending && profile_dialog_user_id_ == user_id_
                ? QString("连接已断开，资料保存中断")
                : QString("连接已断开，资料加载中断"),
            true);
    }
    if (shutting_down_) {
        return;
    }
    if (logging_out_) {
        logging_out_ = false;
        return;
    }
    if (logged_in_ && !token_.isEmpty()) {
        outbox_.markAllSendingFailed();
        message_store_.markAllSendingFailed();
        pending_sequences_.clear();
        renderCurrentConversation(ScrollMode::kPreserve);
        if (offline_mode_) {
            // 已经在离线模式：重连失败不要弹回登录页，继续退避重连。
            setOnline(false, "离线模式 · 仅显示本地记录");
            setStatus("连接断开: " + reason + "，正在重连...", true);
            scheduleReconnect();
            return;
        }
        if (enterOfflineMode(true)) {
            // 有本地记录：直接进离线模式（只读），重连继续在后台跑。
            setStatus("连接断开: " + reason + "，已进入离线模式", true);
            scheduleReconnect();
            return;
        }
        // 本机没有这个账号的记录：留在登录页显示连接失败，但重连照旧。
        setOnline(false, "连接已断开，正在重连...");
        setStatus("连接断开: " + reason + "，正在重连...", true);
        login_page_->setBusy(false);
        login_page_->setStatus("连接断开: " + reason, true);
        stack_->setCurrentWidget(login_page_);
        scheduleReconnect();
        return;
    }
    login_page_->setBusy(false);
    login_page_->setStatus("连接断开: " + reason, true);
    stack_->setCurrentWidget(login_page_);
    setStatus("连接断开: " + reason, true);
}

void MainWindow::scheduleReconnect() {
    if (reconnect_scheduled_ || shutting_down_) {
        return;
    }
    reconnect_scheduled_ = true;
    const int exponent = std::min(reconnect_attempt_, 3);
    const int delay = std::min(10000, 1000 * (1 << exponent));
    ++reconnect_attempt_;
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->start(delay);
    }
}

bool MainWindow::sendFrame(const chatbox::Frame& frame) {
    if (network_ == nullptr || !connected_) {
        setStatus("网络未连接", true);
        return false;
    }
    return network_->enqueueFrame(chatbox::encodeToByteArray(frame));
}

void MainWindow::sendLogin() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdLogin;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(username_));
    writer.putString(chatbox::toUtf8(password_));
    frame.body = writer.data();
    sendFrame(frame);
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->start();
    }
    setStatus("正在登录...");
}

void MainWindow::sendRegister() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdRegister;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(username_));
    writer.putString(chatbox::toUtf8(password_));
    writer.putString(chatbox::toUtf8(nickname_));
    frame.body = writer.data();
    sendFrame(frame);
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->start();
    }
    setStatus("正在注册...");
}

void MainWindow::sendResume() {
    resuming_ = true;
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdResume;
    frame.header.seq = next_seq_++;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(token_));
    frame.body = writer.data();
    sendFrame(frame);
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->start();
    }
}

void MainWindow::setOnline(bool online, const QString& text) {
    online_ = online;
    if (banner_row_ != nullptr) {
        if (online) {
            banner_row_->hide();
        } else {
            // 离线模式的横幅文案固定，不被重连提示覆盖。
            const QString banner_text =
                offline_mode_ ? QString("离线模式 · 仅显示本地记录")
                              : (text.isEmpty() ? QString("连接已断开") : text);
            if (connection_banner_ != nullptr) {
                connection_banner_->setText(banner_text);
                connection_banner_->setStyleSheet(
                    "background-color: " +
                    QString::fromLatin1(theme::kBannerErrorBg) +
                    "; color: " + QString::fromLatin1(theme::kSemanticDown) +
                    "; padding: 6px; border: 1px solid " +
                    QString::fromLatin1(theme::kBannerErrorLine) + ";");
            }
            banner_row_->show();
        }
    }
    if (reconnect_button_ != nullptr) {
        // 只有离线模式且还留着 token 时才允许手动重连。
        reconnect_button_->setVisible(offline_mode_ && offline_session_);
    }
    // 离线或未连接时输入区不可用：离线模式是只读视图。
    const bool can_send =
        online_ && !offline_mode_ && context_type_ != ContextType::None;
    if (input_edit_ != nullptr) {
        input_edit_->setEnabled(can_send);
    }
    if (send_button_ != nullptr) {
        send_button_->setEnabled(can_send);
    }
    if (mention_button_ != nullptr && (offline_mode_ || !online_)) {
        mention_button_->hide();
    }
    if (friend_requests_button_ != nullptr) {
        friend_requests_button_->setEnabled(online_ && !offline_mode_);
    }
    if (my_profile_button_ != nullptr) {
        my_profile_button_->setEnabled(online_ && !offline_mode_);
    }
    if (search_users_button_ != nullptr) {
        search_users_button_->setEnabled(online_ && !offline_mode_);
    }
    if (member_list_ != nullptr) {
        member_list_->setEnabled(online_ && !offline_mode_);
    }
    updateMessageByteCounter();
}

bool MainWindow::currentContextMatches(int kind, quint64 target) const {
    if (kind == 1) {
        return context_type_ == ContextType::Room &&
               target == static_cast<quint64>(current_room_id_);
    }
    if (kind == 2) {
        return context_type_ == ContextType::Private && target == current_peer_id_;
    }
    return false;
}

void MainWindow::onFrameReceived(const QByteArray& data) {
    chatbox::Frame frame;
    if (!chatbox::decodeFromByteArray(data, frame)) {
        setStatus("收到非法协议帧", true);
        return;
    }
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());

    if (frame.header.flags & chatbox::kFlagError) {
        uint16_t code = 0;
        QString message;
        if (reader.getU16(code) && readString(reader, message)) {
            pending_group_commands_.remove(frame.header.seq);
            handleError(code, message, frame.header.seq);
        }
        return;
    }

    switch (frame.header.cmd) {
        case chatbox::kCmdPing:
            break;
        case chatbox::kCmdLogin: {
            uint64_t user_id = 0;
            QString nickname;
            QString token;
            if (!reader.getU64(user_id) || !readString(reader, nickname) ||
                !readString(reader, token)) {
                pending_auth_ = PendingAuth::None;
                if (auth_timeout_timer_ != nullptr) {
                    auth_timeout_timer_->stop();
                }
                login_page_->setBusy(false);
                login_page_->setStatus("登录响应格式错误", true);
                setStatus("登录响应格式错误", true);
                return;
            }
            user_id_ = user_id;
            nickname_ = nickname;
            token_ = token;
            password_.clear();
            login_page_->clearPassword();
            logged_in_ = true;
            pending_auth_ = PendingAuth::None;
            resuming_ = false;
            if (auth_timeout_timer_ != nullptr) {
                auth_timeout_timer_->stop();
            }
            reconnect_attempt_ = 0;
            own_avatar_version_ = 0;
            pending_profile_update_seq_ = 0;
            if (reconnect_timer_ != nullptr) {
                reconnect_timer_->stop();
                reconnect_scheduled_ = false;
            }
            qInfo("login ok user_id=%llu",
                  static_cast<unsigned long long>(user_id_));
            // 登录成功后打开本地历史库：失败只记日志，不阻塞登录。
            openHistoryStore(user_id_);
            // 回到在线状态：离线模式的横幅与只读限制全部撤掉。
            leaveOfflineMode();
            saveSession();
            login_page_->setBusy(false);
            stack_->setCurrentWidget(chat_page_);
            updateOwnProfileStrip();
            pending_profile_requests_.clear();
            profile_request_seqs_.clear();
            pending_profile_seqs_.clear();
            pending_avatar_requests_.clear();
            avatar_request_seqs_.clear();
            pending_avatar_seqs_.clear();
            pending_avatar_versions_.clear();
            if (profile_timeout_timer_ != nullptr) {
                profile_timeout_timer_->stop();
            }
            if (avatar_timeout_timer_ != nullptr) {
                avatar_timeout_timer_->stop();
            }
            setOnline(true);
            startFileTransfer();
            resetContext();
            requestGroupList();
            requestGroupRequests(0);
            requestConversations();
            requestFriendList();
            requestFriendRequests();
            requestProfile(user_id_);
            setStatus("登录成功");
            break;
        }
        case chatbox::kCmdLogout:
            finishLogout();
            break;
        case chatbox::kCmdRegister: {
            uint64_t user_id = 0;
            if (!reader.getU64(user_id)) {
                pending_auth_ = PendingAuth::None;
                if (auth_timeout_timer_ != nullptr) {
                    auth_timeout_timer_->stop();
                }
                login_page_->setBusy(false);
                login_page_->setStatus("注册响应格式错误", true);
                setStatus("注册响应格式错误", true);
                return;
            }
            Q_UNUSED(user_id);
            pending_auth_ = PendingAuth::Login;
            if (auth_timeout_timer_ != nullptr) {
                auth_timeout_timer_->stop();
            }
            setStatus("注册成功，正在登录...");
            sendLogin();
            break;
        }
        case chatbox::kCmdResume: {
            uint64_t user_id = 0;
            QString nickname;
            if (!reader.getU64(user_id) || !readString(reader, nickname)) {
                resuming_ = false;
                if (auth_timeout_timer_ != nullptr) {
                    auth_timeout_timer_->stop();
                }
                login_page_->setBusy(false);
                setOnline(false, "恢复会话响应异常，正在重连...");
                setStatus("恢复会话响应格式错误", true);
                if (network_ != nullptr) {
                    network_->requestStop();
                }
                scheduleReconnect();
                return;
            }
            const ContextType previous_context = context_type_;
            const int previous_room = current_room_id_;
            const quint64 previous_peer = current_peer_id_;
            const QString previous_name = current_peer_name_;
            user_id_ = user_id;
            nickname_ = nickname;
            logged_in_ = true;
            resuming_ = false;
            if (auth_timeout_timer_ != nullptr) {
                auth_timeout_timer_->stop();
            }
            reconnect_attempt_ = 0;
            pending_profile_update_seq_ = 0;
            if (reconnect_timer_ != nullptr) {
                reconnect_timer_->stop();
                reconnect_scheduled_ = false;
            }
            qInfo("resume ok user_id=%llu",
                  static_cast<unsigned long long>(user_id_));
            // 恢复会话时同样打开本地历史库（幂等），
            // 保证下面恢复上一个会话时能先渲染本地首屏。
            openHistoryStore(user_id_);
            // 恢复成功即退出离线模式。
            leaveOfflineMode();
            saveSession();
            stack_->setCurrentWidget(chat_page_);
            updateOwnProfileStrip();
            pending_profile_requests_.clear();
            profile_request_seqs_.clear();
            pending_profile_seqs_.clear();
            pending_avatar_requests_.clear();
            avatar_request_seqs_.clear();
            pending_avatar_seqs_.clear();
            pending_avatar_versions_.clear();
            if (profile_timeout_timer_ != nullptr) {
                profile_timeout_timer_->stop();
            }
            if (avatar_timeout_timer_ != nullptr) {
                avatar_timeout_timer_->stop();
            }
            setOnline(true);
            startFileTransfer();
            if (previous_context == ContextType::Room && previous_room > 0) {
                selectRoom(previous_room, previous_name);
            } else if (previous_context == ContextType::Private && previous_peer != 0) {
                selectPrivate(previous_peer, previous_name);
            } else {
                resetContext();
            }
            requestGroupList();
            requestGroupRequests(0);
            requestConversations();
            requestFriendList();
            requestFriendRequests();
            requestProfile(user_id_);
            if (profile_dialog_ != nullptr &&
                profile_dialog_user_id_ != user_id_) {
                requestProfile(profile_dialog_user_id_);
            }
            setStatus("会话已恢复");
            break;
        }
        case chatbox::kCmdGroupList:
            pending_group_commands_.remove(frame.header.seq);
            handleGroupList(QByteArray(frame.body.data(),
                                       static_cast<int>(frame.body.size())));
            break;
        case chatbox::kCmdGroupCreate: {
            pending_group_commands_.remove(frame.header.seq);
            chatbox::GroupInfo wire;
            if (chatbox::decodeGroupInfo(frame.body, wire)) {
                const chatbox::client::GroupInfo group =
                    chatbox::client::groupInfoFromWire(wire);
                group_store_.upsert(group);
                refreshGroupListView();
                selectGroup(group.room_id, group.name);
                setStatus(QString("群已创建，群号 %1").arg(group.group_no));
            } else {
                setStatus("创建群响应格式错误", true);
            }
            break;
        }
        case chatbox::kCmdGroupSearch: {
            pending_group_commands_.remove(frame.header.seq);
            chatbox::GroupInfo wire;
            uint8_t membership = 0;
            if (!chatbox::decodeGroupSearch(frame.body, wire, membership)) {
                setStatus("群搜索响应格式错误", true);
                break;
            }
            last_group_search_found_ =
                wire.room_id != 0 && wire.status == chatbox::GroupStatus::kActive;
            last_group_search_ = chatbox::client::groupInfoFromWire(wire);
            last_group_search_membership_ = membership;
            if (wire.room_id != 0 && !last_group_search_found_) {
                setStatus("该群已解散", true);
            }
            if (group_join_dialog_ != nullptr) {
                group_join_dialog_->setSearchResult(
                    last_group_search_found_, last_group_search_.name,
                    last_group_search_.member_count, membership);
            }
            break;
        }
        case chatbox::kCmdGroupJoinRequest: {
            pending_group_commands_.remove(frame.header.seq);
            chatbox::ByteReader reader(frame.body.data(), frame.body.size());
            uint64_t request_id = 0;
            if (reader.getU64(request_id)) {
                last_group_request_id_ = request_id;
                if (group_join_dialog_ != nullptr) {
                    group_join_dialog_->setSearchResult(
                        true, last_group_search_.name,
                        last_group_search_.member_count, 2);
                }
            }
            break;
        }
        case chatbox::kCmdGroupInvite: {
            pending_group_commands_.remove(frame.header.seq);
            chatbox::ByteReader reader(frame.body.data(), frame.body.size());
            uint64_t invite_id = 0;
            if (reader.getU64(invite_id)) {
                last_group_invite_id_ = invite_id;
            }
            break;
        }
        case chatbox::kCmdGroupMemberList:
            pending_group_commands_.remove(frame.header.seq);
            handleGroupMemberList(QByteArray(frame.body.data(),
                                             static_cast<int>(frame.body.size())));
            break;
        case chatbox::kCmdGroupRequestList:
            pending_group_commands_.remove(frame.header.seq);
            handleGroupRequestList(QByteArray(frame.body.data(),
                                               static_cast<int>(frame.body.size())));
            break;
        case chatbox::kCmdGroupPush:
            handleGroupPush(QByteArray(frame.body.data(),
                                       static_cast<int>(frame.body.size())));
            break;
        case chatbox::kCmdGroupUpdate:
        case chatbox::kCmdGroupDisband:
        case chatbox::kCmdGroupJoinRespond:
        case chatbox::kCmdGroupInviteRespond:
        case chatbox::kCmdGroupMemberUpdate:
        case chatbox::kCmdGroupLeave:
        case chatbox::kCmdGroupMarkRead: {
            pending_group_commands_.remove(frame.header.seq);
            requestGroupList();
            const bool room_removed_by_action =
                frame.header.cmd == chatbox::kCmdGroupLeave ||
                frame.header.cmd == chatbox::kCmdGroupDisband;
            if (!room_removed_by_action &&
                context_type_ == ContextType::Room && current_room_id_ != 0) {
                requestGroupMembers(current_room_id_);
            }
            if (frame.header.cmd == chatbox::kCmdGroupMarkRead &&
                context_type_ == ContextType::Room) {
                group_store_.setUnread(static_cast<quint64>(current_room_id_), 0);
            }
            if (frame.header.cmd == chatbox::kCmdGroupJoinRespond ||
                frame.header.cmd == chatbox::kCmdGroupInviteRespond) {
                requestGroupRequests(0);
                if (context_type_ == ContextType::Room && current_room_id_ != 0) {
                    const chatbox::client::GroupInfo* group = group_store_.find(
                        static_cast<quint64>(current_room_id_));
                    if (group != nullptr && group->role >= 1) {
                        requestGroupRequests(current_room_id_);
                    }
                }
            }
            break;
        }
        case chatbox::kCmdRoomList: {
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("房间列表格式错误", true);
                return;
            }
            room_list_->clear();
            const int selected_room = current_room_id_;
            bool hall_added = false;
            for (uint16_t i = 0; i < count; ++i) {
                uint32_t id = 0;
                QString name;
                uint32_t max_members = 0;
                uint32_t online = 0;
                if (!reader.getU32(id) || !readString(reader, name) ||
                    !reader.getU32(max_members) || !reader.getU32(online)) {
                    setStatus("房间列表格式错误", true);
                    return;
                }
                // 所有房间都记一条会话摘要（含界面只展示一个大厅的情况），
                // 离线时侧栏才有房间名可显示。
                if (history_store_.isOpen()) {
                    history_store_.saveConversation(
                        conversationRow(chatbox::client::ChatContext::kRoom,
                                        static_cast<quint64>(id), name));
                }
                if (hall_added) {
                    continue;
                }
                hall_added = true;
                auto* item = new QListWidgetItem(
                    QString("%1  (%2/%3)").arg(name).arg(online).arg(max_members),
                    room_list_);
                item->setIcon(
                    QIcon(QPixmap::fromImage(chatbox::client::roomAvatar(36))));
                item->setSizeHint(QSize(0, 48));
                item->setData(Qt::UserRole, static_cast<uint>(id));
                item->setData(Qt::UserRole + 1, name);
                item->setData(Qt::UserRole + 2, max_members);
                item->setData(Qt::AccessibleTextRole,
                              QString("%1，在线 %2 人").arg(name).arg(online));
            }
            if (context_type_ == ContextType::None && room_list_->count() > 0) {
                QListWidgetItem* first = room_list_->item(0);
                selectRoom(first->data(Qt::UserRole).toInt(),
                           first->data(Qt::UserRole + 1).toString());
            } else if (context_type_ == ContextType::Room) {
                for (int i = 0; i < room_list_->count(); ++i) {
                    QListWidgetItem* item = room_list_->item(i);
                    if (item->data(Qt::UserRole).toInt() == selected_room) {
                        room_list_->setCurrentItem(item);
                        break;
                    }
                }
            }
            break;
        }
        case chatbox::kCmdJoinRoom:
            // Joining a room is a silent success; member data comes from
            // GROUP_MEMBER_LIST, so background joins must not trigger a
            // request for the currently selected room.
            break;
        case chatbox::kCmdRoomMembers: {
            uint32_t room_id = 0;
            uint16_t count = 0;
            if (!reader.getU32(room_id) || !reader.getU16(count)) {
                setStatus("成员列表格式错误", true);
                return;
            }
            // The left room row is driven by ROOM_LIST, but presence pushes can
            // arrive without a new room-list response. Keep its count in sync
            // with the authoritative member snapshot.
            for (int i = 0; i < room_list_->count(); ++i) {
                QListWidgetItem* room_item = room_list_->item(i);
                if (room_item->data(Qt::UserRole).toUInt() != room_id) {
                    continue;
                }
                const QString room_name = room_item->data(Qt::UserRole + 1).toString();
                const uint max_members =
                    room_item->data(Qt::UserRole + 2).toUInt();
                room_item->setText(QString("%1  (%2/%3)")
                                       .arg(room_name)
                                       .arg(count)
                                       .arg(max_members));
                room_item->setData(Qt::AccessibleTextRole,
                                   QString("%1，在线 %2 人").arg(room_name).arg(count));
                break;
            }
            if (context_type_ != ContextType::Room ||
                room_id != static_cast<uint32_t>(current_room_id_)) {
                break;
            }
            // New clients use GROUP_MEMBER_LIST (durable members). The old
            // ROOM_MEMBERS payload only contains online presence, so do not
            // replace the durable list when a group is known.
            if (group_store_.find(static_cast<quint64>(room_id)) != nullptr) {
                requestGroupMembers(static_cast<quint64>(room_id));
                break;
            }
            if (member_count_label_ != nullptr) {
                member_count_label_->setText(
                    QString("在线成员 (%1)").arg(count));
            }
            member_list_->clear();
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t member_id = 0;
                QString nickname;
                if (!reader.getU64(member_id) || !readString(reader, nickname)) {
                    setStatus("成员列表格式错误", true);
                    return;
                }
                peer_names_.insert(member_id, nickname);
                auto* item = new QListWidgetItem(nickname, member_list_);
                item->setIcon(memberAvatarIcon(member_id, nickname, 28));
                item->setSizeHint(QSize(0, 48));
                item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(member_id));
                item->setData(Qt::UserRole + 1, nickname);
                item->setData(Qt::UserRole + 2, 0);
                item->setData(Qt::UserRole + 3, true);
                item->setData(Qt::AccessibleTextRole, nickname + "，在线");
                if (member_id == user_id_) {
                    // requestPeerAvatar() skips self; ask the same way so the
                    // own row fills right after a fresh login too.
                    if (!avatar_cache_.contains(member_id)) {
                        requestAvatar(member_id, 0);
                    }
                } else {
                    // Members are usually non-friends: ask once with version 0
                    // and cache whatever version (including "no avatar") comes
                    // back.
                    requestPeerAvatar(member_id);
                }
            }
            break;
        }
        case chatbox::kCmdChatSendResp: {
            uint64_t message_id = 0;
            QString created_at;
            if (!reader.getU64(message_id) || !readString(reader, created_at)) {
                setStatus("发送响应格式错误", true);
                return;
            }
            const QString client_msg_id = pending_sequences_.take(frame.header.seq);
            if (!client_msg_id.isEmpty()) {
                outbox_.markSent(client_msg_id);
                if (message_store_.markSent(
                        client_msg_id, message_id,
                        chatbox::client::parseServerUtc(created_at))) {
                    renderCurrentConversation(ScrollMode::kPreserve);
                }
            }
            setStatus("消息已发送");
            break;
        }
        case chatbox::kCmdChatPush: {
            uint32_t room_id = 0;
            uint64_t message_id = 0;
            uint64_t sender_id = 0;
            QString nickname;
            QString content;
            QString created_at;
            if (!reader.getU32(room_id) || !reader.getU64(message_id) ||
                !reader.getU64(sender_id) || !readString(reader, nickname) ||
                !readString(reader, content) || !readString(reader, created_at)) {
                return;
            }
            uint64_t reply_to_id = 0;
            QString reply_sender_name;
            QString reply_content;
            if (reader.remaining() >= 8) {
                if (!reader.getU64(reply_to_id) ||
                    !readString(reader, reply_sender_name) ||
                    !readString(reader, reply_content)) {
                    return;
                }
            }
            chatbox::client::ChatMessage message;
            message.message_id = message_id;
            message.context = chatbox::client::ChatContext::kRoom;
            message.target_id = room_id;
            message.sender_id = sender_id;
            message.sender_name = nickname;
            message.content = content;
            message.created_at_utc = chatbox::client::parseServerUtc(created_at);
            message.reply_to_id = reply_to_id;
            message.reply_sender_name = reply_sender_name;
            message.reply_content = reply_content;
            message.outgoing = sender_id == user_id_;
            message.status = chatbox::client::MessageStatus::kSent;
            if (reader.remaining() > 0) {
                chatbox::FileMeta wire_meta;
                if (chatbox::readFileMeta(reader, wire_meta) &&
                    wire_meta.file_id != 0) {
                    message.type = chatbox::client::MessageType::kFile;
                    message.file = chatbox::client::fileMetaFromWire(wire_meta);
                }
            }
            // 任何房间的推送都先落本地库：当前会话之外的房间
            // 之后切过去时也能立刻看到这一段。
            if (history_store_.isOpen()) {
                history_store_.saveMessage(message);
            }
            if (sender_id == user_id_) {
                break;
            }
            if (!nickname_.isEmpty() && content.contains("@" + nickname_)) {
                showMentionNotification(nickname, content);
            }
            peer_names_.insert(sender_id, nickname);
            if (context_type_ == ContextType::Room &&
                room_id == static_cast<uint32_t>(current_room_id_)) {
                addIncomingMessage(message);
            } else if (chatbox::client::GroupInfo* group =
                           group_store_.find(static_cast<quint64>(room_id))) {
                ++group->unread;
                refreshGroupListView();
            }
            break;
        }
        case chatbox::kCmdPrivateSendResp: {
            uint64_t message_id = 0;
            QString created_at;
            if (!reader.getU64(message_id) || !readString(reader, created_at)) {
                setStatus("私聊响应格式错误", true);
                return;
            }
            const QString client_msg_id = pending_sequences_.take(frame.header.seq);
            if (!client_msg_id.isEmpty()) {
                outbox_.markSent(client_msg_id);
                if (message_store_.markSent(
                        client_msg_id, message_id,
                        chatbox::client::parseServerUtc(created_at))) {
                    renderCurrentConversation(ScrollMode::kPreserve);
                }
            }
            requestConversations();
            setStatus("私聊已发送");
            break;
        }
        case chatbox::kCmdPrivatePush: {
            uint64_t from_id = 0;
            uint64_t message_id = 0;
            QString nickname;
            QString content;
            QString created_at;
            if (!reader.getU64(from_id) || !reader.getU64(message_id) ||
                !readString(reader, nickname) || !readString(reader, content) ||
                !readString(reader, created_at)) {
                return;
            }
            uint64_t reply_to_id = 0;
            QString reply_sender_name;
            QString reply_content;
            if (reader.remaining() >= 8) {
                if (!reader.getU64(reply_to_id) ||
                    !readString(reader, reply_sender_name) ||
                    !readString(reader, reply_content)) {
                    return;
                }
            }
            chatbox::client::ChatMessage message;
            message.message_id = message_id;
            message.context = chatbox::client::ChatContext::kPrivate;
            message.target_id = from_id;
            message.sender_id = from_id;
            message.sender_name = nickname;
            message.content = content;
            message.created_at_utc = chatbox::client::parseServerUtc(created_at);
            message.reply_to_id = reply_to_id;
            message.reply_sender_name = reply_sender_name;
            message.reply_content = reply_content;
            message.outgoing = from_id == user_id_;
            message.status = chatbox::client::MessageStatus::kSent;
            if (reader.remaining() > 0) {
                chatbox::FileMeta wire_meta;
                if (chatbox::readFileMeta(reader, wire_meta) &&
                    wire_meta.file_id != 0) {
                    message.type = chatbox::client::MessageType::kFile;
                    message.file = chatbox::client::fileMetaFromWire(wire_meta);
                }
            }
            // 后台私聊同样落本地库并记一条会话摘要：
            // 之后切到该好友时能立刻看到这一段。
            // 标题优先用已知的备注，没有备注时用昵称。
            QString title = nickname;
            const auto known_view = friend_views_.constFind(from_id);
            if (known_view != friend_views_.constEnd() &&
                !known_view->remark.trimmed().isEmpty()) {
                title = known_view->remark.trimmed();
            }
            if (history_store_.isOpen()) {
                history_store_.saveMessage(message);
                history_store_.saveConversation(
                    conversationRow(chatbox::client::ChatContext::kPrivate,
                                    from_id, title));
            }
            if (from_id == user_id_) {
                break;
            }
            peer_names_.insert(from_id, nickname);
            QListWidgetItem* item = findConversationItem(from_id);
            const bool online = item == nullptr
                                    ? true
                                    : item->data(Qt::UserRole + 3).toBool();
            if (context_type_ == ContextType::Private && from_id == current_peer_id_) {
                addIncomingMessage(message);
                sendMarkRead(from_id, message_id);
                updateConversationItem(from_id, 0, nickname, content, created_at, online);
                refreshUnreadTitle();
            } else {
                if (!friend_ids_.contains(from_id)) {
                    break;
                }
                int unread = item == nullptr
                                 ? 1
                                 : item->data(Qt::UserRole + 2).toInt() + 1;
                updateConversationItem(from_id, unread, nickname, content, created_at, online);
                refreshUnreadTitle();
                showPrivateNotification(nickname, content);
            }
            break;
        }
        case chatbox::kCmdPrivateConversations: {
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("会话列表格式错误", true);
                return;
            }
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t peer_id = 0;
                QString nickname;
                uint64_t last_message_id = 0;
                QString last_content;
                QString last_time;
                uint8_t online = 0;
                uint32_t unread = 0;
                if (!reader.getU64(peer_id) || !readString(reader, nickname) ||
                    !reader.getU64(last_message_id) || !readString(reader, last_content) ||
                    !readString(reader, last_time) || !reader.getU8(online) ||
                    !reader.getU32(unread)) {
                    setStatus("会话列表格式错误", true);
                    return;
                }
                // 会话摘要先落库（好友过滤之前），
                // 标题优先用已知的备注，没有备注时用昵称。
                if (history_store_.isOpen()) {
                    QString title = nickname;
                    const auto view = friend_views_.constFind(peer_id);
                    if (view != friend_views_.constEnd() &&
                        !view->remark.trimmed().isEmpty()) {
                        title = view->remark;
                    }
                    history_store_.saveConversation(
                        conversationRow(chatbox::client::ChatContext::kPrivate,
                                        peer_id, title));
                }
                if (!friend_ids_.contains(peer_id)) {
                    continue;
                }
                Q_UNUSED(last_message_id);
                updateFriendView(peer_id, static_cast<int>(unread), nickname,
                                 last_content, last_time, online != 0);
            }
            refreshFriendListView();
            if (context_type_ == ContextType::Private) {
                QListWidgetItem* current = findConversationItem(current_peer_id_);
                if (current != nullptr) {
                    conversation_list_->setCurrentItem(current);
                }
            }
            break;
        }
        case chatbox::kCmdHistoryResp: {
            if (frame.header.seq != history_request_seq_) {
                break;
            }
            uint8_t kind = 0;
            uint64_t target = 0;
            uint16_t count = 0;
            uint8_t has_more = 0;
            uint64_t next_before = 0;
            if (!reader.getU8(kind) || !reader.getU64(target) || !reader.getU16(count) ||
                !reader.getU8(has_more) || !reader.getU64(next_before)) {
                setStatus("历史消息格式错误", true);
                return;
            }
            if (kind != history_request_kind_ ||
                target != history_request_target_ ||
                !currentContextMatches(kind, target)) {
                break;
            }
            QVector<chatbox::client::ChatMessage> page;
            const chatbox::client::ChatContext chat_context =
                kind == 1 ? chatbox::client::ChatContext::kRoom
                          : chatbox::client::ChatContext::kPrivate;
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t message_id = 0;
                uint64_t sender_id = 0;
                QString nickname;
                QString content;
                QString created_at;
                uint64_t reply_to_id = 0;
                QString reply_sender_name;
                QString reply_content;
                uint8_t recalled = 0;
                if (!reader.getU64(message_id) || !reader.getU64(sender_id) ||
                    !readString(reader, nickname) || !readString(reader, content) ||
                    !readString(reader, created_at) || !reader.getU64(reply_to_id) ||
                    !readString(reader, reply_sender_name) ||
                    !readString(reader, reply_content) || !reader.getU8(recalled)) {
                    setStatus("历史消息格式错误", true);
                    return;
                }
                chatbox::client::ChatMessage message;
                message.message_id = message_id;
                message.context = chat_context;
                message.target_id = target;
                message.sender_id = sender_id;
                message.sender_name = nickname;
                message.content = content;
                message.created_at_utc = chatbox::client::parseServerUtc(created_at);
                message.reply_to_id = reply_to_id;
                message.reply_sender_name = reply_sender_name;
                message.reply_content = reply_content;
                message.recalled = recalled != 0;
                message.outgoing = sender_id == user_id_;
                message.status = chatbox::client::MessageStatus::kSent;
                page.push_back(message);
                peer_names_.insert(sender_id, nickname);
            }
            if (reader.remaining() > 0) {
                std::vector<uint16_t> meta_indices;
                std::vector<chatbox::FileMeta> metas;
                if (chatbox::decodeFileMetaTable(reader, meta_indices, metas)) {
                    for (std::size_t i = 0;
                         i < meta_indices.size() && i < metas.size(); ++i) {
                        const std::size_t index = meta_indices[i];
                        if (index < page.size() && metas[i].file_id != 0) {
                            page[index].type =
                                chatbox::client::MessageType::kFile;
                            page[index].file =
                                chatbox::client::fileMetaFromWire(metas[i]);
                        }
                    }
                }
            }

            const bool is_jump = history_request_is_jump_;
            const bool was_older_page =
                history_request_before_ != 0 && !is_jump;
            history_request_is_jump_ = false;
            if (is_jump) {
                bool target_present = false;
                for (const chatbox::client::ChatMessage& message : page) {
                    if (message.message_id == pending_jump_message_id_) {
                        target_present = true;
                        break;
                    }
                }
                if (!target_present) {
                    pending_jump_message_id_ = 0;
                    loading_older_history_ = false;
                    history_request_seq_ = 0;
                    history_request_kind_ = 0;
                    history_request_target_ = 0;
                    history_request_before_ = 0;
                    setStatus("消息不在当前可见范围内", true);
                    break;
                }
            }
            if (was_older_page) {
                message_store_.prependHistory(page);
                renderCurrentConversation(ScrollMode::kPreserveAnchor);
            } else {
                message_store_.replaceHistory(page);
                renderCurrentConversation(ScrollMode::kBottom);
            }
            loading_older_history_ = false;
            history_request_seq_ = 0;
            history_request_kind_ = 0;
            history_request_target_ = 0;
            history_request_before_ = 0;
            has_more_ = has_more != 0;
            next_before_id_ = next_before;
            if (kind == 1 && !was_older_page && !is_jump && !page.isEmpty()) {
                markGroupRead(target, page.back().message_id);
            }
            if (pending_jump_message_id_ != 0) {
                const quint64 target_message = pending_jump_message_id_;
                pending_jump_message_id_ = 0;
                if (message_store_.findMessage(target_message) != nullptr) {
                    jumpToMessage(target_message);
                } else {
                    setStatus("消息不在当前可见范围内", true);
                }
            }
            if (kind == 2 && !was_older_page) {
                requestConversations();
            }
            if (count == 0) {
                setStatus("暂无历史消息");
            }
            break;
        }
        case chatbox::kCmdUserSearch: {
            if (frame.header.seq != user_search_seq_) {
                break;
            }
            if (search_timeout_timer_ != nullptr) {
                search_timeout_timer_->stop();
            }
            user_search_seq_ = 0;
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("用户搜索响应格式错误", true);
                return;
            }
            QVector<UserSearchResult> results;
            results.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t user_id = 0;
                QString username;
                QString nickname;
                uint8_t online = 0;
                if (!reader.getU64(user_id) || !readString(reader, username) ||
                    !readString(reader, nickname) || !reader.getU8(online)) {
                    setStatus("用户搜索响应格式错误", true);
                    return;
                }
                UserSearchResult result;
                result.user_id = user_id;
                result.username = username;
                result.nickname = nickname;
                result.online = online != 0;
                results.push_back(result);
            }
            if (user_search_dialog_ != nullptr) {
                user_search_dialog_->setResults(results);
                user_search_dialog_->setBusy(false);
            }
            break;
        }
        case chatbox::kCmdMessageRecall: {
            uint8_t kind = 0;
            uint64_t target_id = 0;
            uint64_t message_id = 0;
            if (!reader.getU8(kind) || !reader.getU64(target_id) ||
                !reader.getU64(message_id)) {
                setStatus("撤回响应格式错误", true);
                return;
            }
            // kind: 1 = 房间，2 = 私聊。其它取值一律忽略，
            // 免得把无法定位会话的撤回套到本地记录上。
            if (kind != 1 && kind != 2) {
                break;
            }
            const chatbox::client::ChatContext context =
                (kind == 1) ? chatbox::client::ChatContext::kRoom
                            : chatbox::client::ChatContext::kPrivate;
            const quint64 target = static_cast<quint64>(target_id);
            const bool matches =
                (kind == 1 && context_type_ == ContextType::Room &&
                 target_id == static_cast<uint64_t>(current_room_id_)) ||
                (kind == 2 && context_type_ == ContextType::Private &&
                 target_id == current_peer_id_);
            if (matches) {
                // 当前会话：先在内存里撤回并重绘；内存里没有这条消息
                // （还没加载进来）时退回到本地历史库。
                if (message_store_.markRecalled(message_id)) {
                    renderCurrentConversation(ScrollMode::kPreserve);
                } else if (history_store_.isOpen()) {
                    history_store_.markRecalled(context, target, message_id);
                }
                break;
            }
            // 后台会话的撤回：只更新本地历史库里的副本，
            // 不重绘当前视图，也不改未读数与提醒状态。
            if (history_store_.isOpen()) {
                history_store_.markRecalled(context, target, message_id);
            }
            break;
        }
        case chatbox::kCmdMessageSearch: {
            const bool panel_search =
                panel_search_seq_ != 0 && frame.header.seq == panel_search_seq_;
            if (!panel_search && frame.header.seq != message_search_seq_) {
                break;
            }
            if (message_search_timeout_timer_ != nullptr) {
                message_search_timeout_timer_->stop();
            }
            if (panel_search) {
                panel_search_seq_ = 0;
            } else {
                message_search_seq_ = 0;
            }
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("消息搜索响应格式错误", true);
                return;
            }
            QVector<MessageSearchResult> results;
            results.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t message_id = 0;
                uint64_t sender_id = 0;
                QString nickname;
                QString content;
                QString created_at;
                uint8_t recalled = 0;
                if (!reader.getU64(message_id) || !reader.getU64(sender_id) ||
                    !readString(reader, nickname) || !readString(reader, content) ||
                    !readString(reader, created_at) || !reader.getU8(recalled)) {
                    setStatus("消息搜索响应格式错误", true);
                    return;
                }
                MessageSearchResult result;
                result.message_id = message_id;
                result.sender_id = sender_id;
                result.nickname = nickname;
                result.content = content;
                result.created_at =
                    chatbox::client::parseServerUtc(created_at)
                        .toLocalTime()
                        .toString("yyyy-MM-dd HH:mm:ss");
                result.recalled = recalled != 0;
                results.push_back(result);
            }
            if (panel_search) {
                right_search_results_->clear();
                for (const MessageSearchResult& result : results) {
                    auto* item = new QListWidgetItem(
                        QString("%1  %2\n%3")
                            .arg(result.nickname, result.created_at,
                                 result.recalled ? QString() : result.content),
                        right_search_results_);
                    item->setData(Qt::UserRole,
                                  QVariant::fromValue<qulonglong>(result.message_id));
                    item->setToolTip(QStringLiteral("双击跳转到该消息"));
                }
                right_search_status_->setText(
                    results.isEmpty() ? QStringLiteral("没有匹配消息")
                                      : QString("%1 条结果").arg(results.size()));
                right_mode_stack_->setCurrentWidget(right_search_page_);
            } else if (message_search_dialog_ != nullptr) {
                message_search_dialog_->setResults(results);
                message_search_dialog_->setBusy(false);
            }
            break;
        }
        case chatbox::kCmdFriendList: {
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("好友列表格式错误", true);
                return;
            }
            conversation_list_->clear();
            friend_ids_.clear();
            friend_views_.clear();
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t friend_id = 0;
                QString username;
                QString nickname;
                uint8_t online = 0;
                QString remark;
                uint32_t avatar_version = 0;
                if (!reader.getU64(friend_id) || !readString(reader, username) ||
                    !readString(reader, nickname) || !reader.getU8(online) ||
                    !readString(reader, remark) || !reader.getU32(avatar_version)) {
                    setStatus("好友列表格式错误", true);
                    return;
                }
                friend_ids_.insert(friend_id);
                chatbox::client::FriendView view;
                view.user_id = friend_id;
                view.username = username;
                view.nickname = nickname;
                view.remark = remark;
                view.avatar_version = avatar_version;
                view.online = online != 0;
                friend_views_.insert(friend_id, view);
            }
            refreshFriendListView();
            for (const chatbox::client::FriendView& view : friend_views_) {
                if (view.avatar_version != 0 &&
                    avatar_cache_.needsFetch(view.user_id, view.avatar_version)) {
                    requestAvatar(view.user_id, view.avatar_version);
                }
            }
            requestConversations();
            break;
        }
        case chatbox::kCmdFriendRequests: {
            uint16_t count = 0;
            if (!reader.getU16(count)) {
                setStatus("好友申请格式错误", true);
                return;
            }
            QVector<FriendRequestInfo> requests;
            requests.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                uint64_t requester_id = 0;
                QString username;
                QString nickname;
                QString created_at;
                if (!reader.getU64(requester_id) || !readString(reader, username) ||
                    !readString(reader, nickname) || !readString(reader, created_at)) {
                    setStatus("好友申请格式错误", true);
                    return;
                }
                FriendRequestInfo request;
                request.user_id = requester_id;
                request.username = username;
                request.nickname = nickname;
                request.created_at = created_at;
                requests.push_back(request);
            }
            friend_request_count_ = requests.size();
            updateFriendRequestCount();
            if (friend_requests_dialog_ != nullptr) {
                friend_requests_dialog_->setRequests(requests);
            }
            break;
        }
        case chatbox::kCmdFriendRequest: {
            const quint64 requested_user = friend_request_seqs_.take(frame.header.seq);
            if (profile_dialog_ != nullptr && requested_user != 0 &&
                profile_dialog_user_id_ == requested_user &&
                profile_dialog_profile_.relationship == 2) {
                profile_dialog_profile_.relationship = 3;
                profile_dialog_->setProfile(profile_dialog_profile_, false);
                profile_dialog_->setStatus("好友申请已发送");
            }
            setStatus("好友申请已发送");
            break;
        }
        case chatbox::kCmdFriendRespond:
            setStatus("好友申请已处理");
            requestFriendList();
            requestFriendRequests();
            break;
        case chatbox::kCmdFriendRemove:
            setStatus("已删除好友");
            requestFriendList();
            break;
        case chatbox::kCmdFriendSetRemark:
            setStatus("好友备注已保存");
            requestFriendList();
            if (profile_dialog_ != nullptr &&
                friend_ids_.contains(profile_dialog_user_id_)) {
                requestProfile(profile_dialog_user_id_);
            }
            break;
        case chatbox::kCmdProfileUpdate: {
            const quint32 response_seq = frame.header.seq;
            if (pending_profile_update_seq_ == 0 ||
                response_seq != pending_profile_update_seq_) {
                qWarning("stale profile update response ignored seq=%u pending=%u",
                         response_seq, pending_profile_update_seq_);
                break;
            }
            pending_profile_update_seq_ = 0;

            QString updated_nickname;
            QString updated_signature;
            uint32_t avatar_version = 0;
            if (!readString(reader, updated_nickname) ||
                !readString(reader, updated_signature) ||
                !reader.getU32(avatar_version) ||
                updated_nickname.trimmed().isEmpty()) {
                if (profile_dialog_ != nullptr &&
                    profile_dialog_user_id_ == user_id_) {
                    const bool still_loading =
                        pending_profile_requests_.contains(user_id_) ||
                        pending_profile_seqs_.contains(user_id_);
                    if (!still_loading) {
                        profile_dialog_->setBusy(false);
                    }
                    profile_dialog_->setStatus("资料响应格式错误", true);
                }
                setStatus("资料响应格式错误", true);
                return;
            }
            applyOwnProfileUpdate(updated_nickname, updated_signature,
                                  static_cast<quint32>(avatar_version));
            break;
        }
        case chatbox::kCmdProfileGet: {
            const quint32 response_seq = frame.header.seq;
            const quint64 requested_user = profile_request_seqs_.value(response_seq);
            uint64_t target_id = 0;
            QString username;
            QString nickname;
            QString signature;
            uint32_t avatar_version = 0;
            std::string avatar_bytes;
            uint8_t online = 0;
            uint8_t relationship = 0;
            QString remark;
            if (!reader.getU64(target_id) || !readString(reader, username) ||
                !readString(reader, nickname) || !readString(reader, signature) ||
                !reader.getU32(avatar_version) || !reader.getString(avatar_bytes) ||
                !reader.getU8(online) || !reader.getU8(relationship) ||
                !readString(reader, remark)) {
                if (requested_user != 0) {
                    const bool current_request =
                        isCurrentProfileRequest(requested_user, response_seq);
                    clearPendingProfileRequest(requested_user, response_seq);
                    if (current_request) {
                        if (profile_dialog_ != nullptr &&
                            profile_dialog_user_id_ == requested_user) {
                            profile_dialog_->setBusy(false);
                            profile_dialog_->setStatus("资料响应格式错误", true);
                        }
                        setStatus("资料响应格式错误", true);
                    } else {
                        qWarning("stale malformed profile response ignored seq=%u "
                                 "user_id=%llu",
                                 response_seq,
                                 static_cast<unsigned long long>(requested_user));
                    }
                }
                return;
            }

            chatbox::client::UserProfile profile;
            profile.user_id = target_id;
            profile.username = username;
            profile.nickname = nickname;
            profile.signature = signature;
            profile.avatar_version = avatar_version;
            if (!avatar_bytes.empty()) {
                profile.avatar = QImage::fromData(
                    QByteArray(avatar_bytes.data(),
                               static_cast<int>(avatar_bytes.size())));
            }
            profile.online = online != 0;
            profile.relationship = relationship;
            profile.remark = remark;

            clearPendingProfileRequest(target_id, response_seq);
            if (requested_user != 0 && requested_user != target_id) {
                clearPendingProfileRequest(requested_user, response_seq);
            }
            if (pending_profile_seqs_.contains(target_id)) {
                // A newer request for this user is still in flight. Applying
                // this older response could regress the dialog/friend view.
                qInfo("stale profile response ignored user_id=%llu seq=%u "
                      "pending_seq=%u",
                      static_cast<unsigned long long>(target_id), response_seq,
                      pending_profile_seqs_.value(target_id));
                break;
            }
            onProfileDataReceived(profile);
            break;
        }
        case chatbox::kCmdAvatarGet: {
            const quint32 response_seq = frame.header.seq;
            const quint64 requested_user = avatar_request_seqs_.value(response_seq);
            uint64_t target_id = 0;
            uint32_t avatar_version = 0;
            std::string avatar_bytes;
            if (!reader.getU64(target_id) || !reader.getU32(avatar_version) ||
                !reader.getString(avatar_bytes)) {
                if (requested_user != 0) {
                    const bool current_request =
                        isCurrentAvatarRequest(requested_user, response_seq);
                    clearPendingAvatarRequest(requested_user, response_seq);
                    if (current_request) {
                        setStatus("头像响应格式错误", true);
                    } else {
                        qWarning("stale malformed avatar response ignored seq=%u "
                                 "user_id=%llu",
                                 response_seq,
                                 static_cast<unsigned long long>(requested_user));
                    }
                }
                return;
            }
            QImage image;
            if (!avatar_bytes.empty()) {
                image = QImage::fromData(
                    QByteArray(avatar_bytes.data(),
                               static_cast<int>(avatar_bytes.size())));
            }

            clearPendingAvatarRequest(target_id, response_seq);
            if (requested_user != 0 && requested_user != target_id) {
                clearPendingAvatarRequest(requested_user, response_seq);
            }
            if (pending_avatar_seqs_.contains(target_id)) {
                const quint32 pending_version =
                    pending_avatar_versions_.value(target_id);
                if (pending_version != 0 && avatar_version < pending_version) {
                    qInfo("stale avatar response ignored user_id=%llu version=%u "
                          "pending_version=%u seq=%u",
                          static_cast<unsigned long long>(target_id),
                          avatar_version, pending_version, response_seq);
                    break;
                }
            }
            onProfileAvatarReceived(target_id, avatar_version, image);
            break;
        }
        case chatbox::kCmdProfileUpdatePush: {
            uint64_t pushed_user_id = 0;
            QString pushed_nickname;
            uint32_t pushed_avatar_version = 0;
            if (!reader.getU64(pushed_user_id) ||
                !readString(reader, pushed_nickname) ||
                !reader.getU32(pushed_avatar_version)) {
                return;
            }
            if (pushed_user_id == 0) {
                break;
            }
            if (pushed_user_id == user_id_) {
                // Self updates are applied from the PROFILE_UPDATE response;
                // a self push must never rewrite the private peer header.
                qWarning("self profile update push ignored");
                break;
            }
            applyProfileUpdatePush(
                pushed_user_id, pushed_nickname,
                static_cast<quint32>(pushed_avatar_version));
            break;
        }
        case chatbox::kCmdFriendRequestPush: {
            uint64_t from_id = 0;
            QString username;
            QString nickname;
            if (!reader.getU64(from_id) || !readString(reader, username) ||
                !readString(reader, nickname)) {
                return;
            }
            Q_UNUSED(username);
            requestFriendRequests();
            if (notifications_enabled_ && system_popup_enabled_ && tray_ != nullptr) {
                tray_->showMessage("好友申请", nickname + " 请求添加你为好友",
                                   QSystemTrayIcon::Information, 3000);
            }
            break;
        }
        case chatbox::kCmdFriendAcceptPush: {
            uint64_t friend_id = 0;
            QString username;
            QString nickname;
            uint8_t online = 0;
            if (!reader.getU64(friend_id) || !readString(reader, username) ||
                !readString(reader, nickname) || !reader.getU8(online)) {
                return;
            }
            Q_UNUSED(username);
            friend_ids_.insert(friend_id);
            updateConversationItem(friend_id, 0, nickname, "", "", online != 0);
            if (notifications_enabled_ && system_popup_enabled_ && tray_ != nullptr) {
                tray_->showMessage("好友申请", nickname + " 已添加你为好友",
                                   QSystemTrayIcon::Information, 3000);
            }
            requestFriendList();
            break;
        }
        case chatbox::kCmdError: {
            uint16_t code = 0;
            QString message;
            if (!reader.getU16(code) || !readString(reader, message)) {
                setStatus("错误响应格式错误", true);
                return;
            }
            pending_group_commands_.remove(frame.header.seq);
            handleError(code, message, frame.header.seq);
            break;
        }
        case chatbox::kCmdFileDeleted: {
            uint64_t file_id = 0;
            uint64_t message_id = 0;
            uint8_t kind = 0;
            uint64_t target_id = 0;
            if (!reader.getU64(file_id) || !reader.getU64(message_id) ||
                !reader.getU8(kind) || !reader.getU64(target_id)) {
                return;
            }
            (void)file_id;
            history_store_.markFileDeleted(
                kind == 0 ? chatbox::client::ChatContext::kRoom
                          : chatbox::client::ChatContext::kPrivate,
                target_id, message_id);
            if (message_store_.markFileDeleted(message_id)) {
                renderCurrentConversation(ScrollMode::kPreserve);
            }
            if (file_manager_dialog_ != nullptr && file_transfer_ != nullptr) {
                file_transfer_->listFiles(
                    context_type_ == ContextType::Room ? 1 : 2,
                    context_type_ == ContextType::Room
                        ? static_cast<quint64>(current_room_id_)
                        : current_peer_id_);
            }
            break;
        }
        default:
            break;
    }
}

void MainWindow::handleError(quint16 code, const QString& message, quint32 seq) {
    const QString text = errorText(code, message);
    qWarning("server error code=%u message=%s", static_cast<unsigned>(code),
             qPrintable(text));
    const bool was_resuming = resuming_;
    resuming_ = false;
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->stop();
    }
    login_page_->setBusy(false);
    if (!logged_in_) {
        login_page_->setStatus(text, true);
    }

    const QString pending_id = pending_sequences_.take(seq);
    bool pending_failed = false;
    if (!pending_id.isEmpty()) {
        const bool was_pending = outbox_.markFailed(pending_id);
        if (was_pending) {
            pending_failed = true;
            const bool current_changed = message_store_.markFailed(pending_id);
            if (current_changed) {
                renderCurrentConversation(ScrollMode::kPreserve);
            }
        }
    }

    if (code == 1002 || code == 1009 || code == 1010) {
        clearSession();
        setStatus(QString("会话失效 (%1): %2").arg(code).arg(text), true);
        login_page_->setStatus(QString("会话失效: %1").arg(text), true);
        return;
    }

    if (pending_profile_update_seq_ != 0 && seq == pending_profile_update_seq_) {
        pending_profile_update_seq_ = 0;
        const QString update_error = QString("保存资料失败: %1").arg(text);
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
            const bool still_loading =
                pending_profile_requests_.contains(user_id_) ||
                pending_profile_seqs_.contains(user_id_);
            if (!still_loading) {
                profile_dialog_->setBusy(false);
            }
            profile_dialog_->setStatus(update_error, true);
        }
        setStatus(update_error, true);
        return;
    }

    if (profile_request_seqs_.contains(seq)) {
        const quint64 profile_user = profile_request_seqs_.value(seq);
        const bool current_request =
            isCurrentProfileRequest(profile_user, seq);
        clearPendingProfileRequest(profile_user, seq);
        if (current_request) {
            if (profile_dialog_ != nullptr &&
                profile_dialog_user_id_ == profile_user) {
                profile_dialog_->setBusy(false);
                profile_dialog_->setStatus(
                    QString("加载资料失败: %1").arg(text), true);
            }
            setStatus(QString("加载资料失败: %1").arg(text), true);
        } else {
            qWarning("stale profile error ignored seq=%u user_id=%llu: %s",
                     seq, static_cast<unsigned long long>(profile_user),
                     qPrintable(text));
        }
        return;
    }

    if (avatar_request_seqs_.contains(seq)) {
        const quint64 avatar_user = avatar_request_seqs_.value(seq);
        const bool current_request =
            isCurrentAvatarRequest(avatar_user, seq);
        clearPendingAvatarRequest(avatar_user, seq);
        if (current_request) {
            qWarning("avatar request failed user_id=%llu: %s",
                     static_cast<unsigned long long>(avatar_user),
                     qPrintable(text));
        } else {
            qWarning("stale avatar error ignored seq=%u user_id=%llu: %s",
                     seq, static_cast<unsigned long long>(avatar_user),
                     qPrintable(text));
        }
        return;
    }

    if (friend_request_seqs_.contains(seq)) {
        const quint64 friend_user = friend_request_seqs_.take(seq);
        if (profile_dialog_ != nullptr &&
            profile_dialog_user_id_ == friend_user) {
            profile_dialog_->setStatus(QString("好友申请失败: %1").arg(text), true);
        }
        setStatus(QString("好友申请失败: %1").arg(text), true);
        return;
    }

    if (was_resuming) {
        if (offline_mode_) {
            // 离线模式下的恢复失败：留在离线视图，继续退避重连。
            setOnline(false, "离线模式 · 仅显示本地记录");
            setStatus(QString("恢复会话失败: %1").arg(text), true);
            if (network_ != nullptr) {
                network_->requestStop();
            }
            scheduleReconnect();
            return;
        }
        if (enterOfflineMode(true)) {
            setStatus(QString("恢复会话失败: %1，已进入离线模式").arg(text), true);
            if (network_ != nullptr) {
                network_->requestStop();
            }
            scheduleReconnect();
            return;
        }
        setOnline(false, "恢复会话失败，正在重连...");
        setStatus(QString("恢复会话失败: %1").arg(text), true);
        login_page_->setBusy(false);
        login_page_->setStatus(QString("恢复会话失败: %1").arg(text), true);
        stack_->setCurrentWidget(login_page_);
        if (network_ != nullptr) {
            network_->requestStop();
        }
        scheduleReconnect();
        return;
    }

    if (user_search_seq_ != 0 && seq == user_search_seq_) {
        user_search_seq_ = 0;
        if (search_timeout_timer_ != nullptr) {
            search_timeout_timer_->stop();
        }
        if (user_search_dialog_ != nullptr) {
            user_search_dialog_->setBusy(false);
            user_search_dialog_->setStatus(QString("搜索失败: %1").arg(text), true);
        }
        setStatus(QString("搜索失败: %1").arg(text), true);
        return;
    }

    if (history_request_seq_ != 0 && seq == history_request_seq_) {
        history_request_seq_ = 0;
        history_request_is_jump_ = false;
        jump_history_pending_ = false;
        pending_jump_message_id_ = 0;
        loading_older_history_ = false;
        setStatus(QString("加载历史失败: %1").arg(text), true);
        return;
    }

    if (panel_search_seq_ != 0 && seq == panel_search_seq_) {
        panel_search_seq_ = 0;
        if (message_search_timeout_timer_ != nullptr) {
            message_search_timeout_timer_->stop();
        }
        if (right_search_status_ != nullptr) {
            right_search_status_->setText(QString("搜索失败: %1").arg(text));
        }
        setStatus(QString("搜索失败: %1").arg(text), true);
        return;
    }

    if (message_search_seq_ != 0 && seq == message_search_seq_) {
        message_search_seq_ = 0;
        if (message_search_timeout_timer_ != nullptr) {
            message_search_timeout_timer_->stop();
        }
        if (message_search_dialog_ != nullptr) {
            message_search_dialog_->setBusy(false);
            message_search_dialog_->setStatus(QString("搜索失败: %1").arg(text), true);
        }
        setStatus(QString("搜索失败: %1").arg(text), true);
        return;
    }

    if (pending_failed) {
        setStatus(QString("发送失败: %1").arg(text), true);
        return;
    }
    setStatus(QString("错误 %1: %2").arg(code).arg(text), true);
}

void MainWindow::clearSession() {
    if (file_transfer_ != nullptr) {
        file_transfer_->stop();
    }
    logged_in_ = false;
    connected_ = false;
    offline_mode_ = false;
    offline_session_ = false;
    ++connection_generation_;
    if (network_ != nullptr) {
        network_->requestStop();
    }
    // 本地历史库随会话一起关掉，但磁盘上的记录保留：
    // 下次登录同一账号仍能读到本地首屏。
    closeHistoryStore();
    resuming_ = false;
    if (auth_timeout_timer_ != nullptr) {
        auth_timeout_timer_->stop();
    }
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
        reconnect_scheduled_ = false;
    }
    token_.clear();
    password_.clear();
    user_id_ = 0;
    nickname_.clear();
    pending_auth_ = PendingAuth::None;
    outbox_.clear();
    pending_sequences_.clear();
    friend_ids_.clear();
    friend_views_.clear();
    pending_profile_requests_.clear();
    profile_request_seqs_.clear();
    pending_profile_seqs_.clear();
    pending_avatar_requests_.clear();
    avatar_request_seqs_.clear();
    pending_avatar_seqs_.clear();
    pending_avatar_versions_.clear();
    friend_request_seqs_.clear();
    pending_profile_update_seq_ = 0;
    own_avatar_version_ = 0;
    if (profile_timeout_timer_ != nullptr) {
        profile_timeout_timer_->stop();
    }
    if (avatar_timeout_timer_ != nullptr) {
        avatar_timeout_timer_->stop();
    }
    closeProfileDialog();
    avatar_cache_.clear();
    friend_request_count_ = 0;
    updateFriendRequestCount();
    user_search_seq_ = 0;
    if (search_timeout_timer_ != nullptr) {
        search_timeout_timer_->stop();
    }
    if (user_search_dialog_ != nullptr) {
        user_search_dialog_->close();
        user_search_dialog_->clearResults();
    }
    message_search_seq_ = 0;
    if (message_search_timeout_timer_ != nullptr) {
        message_search_timeout_timer_->stop();
    }
    if (message_search_dialog_ != nullptr) {
        message_search_dialog_->close();
        message_search_dialog_->clearResults();
    }
    clearReply();
    QSettings settings;
    // 只删会话信息：本地记录（history/*）保留，离线模式继续可用。
    settings.remove("session");
    settings.sync();
    if (conversation_list_ != nullptr) {
        conversation_list_->clear();
    }
    if (room_list_ != nullptr) {
        room_list_->clear();
    }
    group_store_.clear();
    pending_group_commands_.clear();
    joined_groups_.clear();
    group_requests_by_room_.clear();
    group_invites_.clear();
    panel_search_seq_ = 0;
    history_request_seq_ = 0;
    history_request_is_jump_ = false;
    jump_history_pending_ = false;
    right_panel_profile_ = chatbox::client::UserProfile();
    if (group_create_dialog_ != nullptr) {
        group_create_dialog_->hide();
    }
    if (group_join_dialog_ != nullptr) {
        group_join_dialog_->hide();
    }
    if (group_list_ != nullptr) {
        group_list_->clear();
    }
    if (group_settings_dialog_ != nullptr) {
        group_settings_dialog_->disconnect(this);
        group_settings_dialog_->close();
        group_settings_dialog_->deleteLater();
        group_settings_dialog_ = nullptr;
    }
    if (group_notify_dialog_ != nullptr) {
        group_notify_dialog_->disconnect(this);
        group_notify_dialog_->close();
        group_notify_dialog_->deleteLater();
        group_notify_dialog_ = nullptr;
    }
    updateGroupNoticeBadge();
    updateOwnProfileStrip();
    refreshUnreadTitle();
    resetContext();
    setOnline(false, "未连接");
    stack_->setCurrentWidget(login_page_);
    login_page_->setBusy(false);
    login_page_->clearPassword();
    login_page_->resetToLogin();
    login_page_->setStatus("会话已失效，请重新登录", true);
}

void MainWindow::saveSession() {
    if (token_.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.setValue("session/host", host_);
    settings.setValue("session/port", port_);
    settings.setValue("session/tls", use_tls_);
    if (settings.value("login/auto_login", true).toBool()) {
        settings.setValue("session/token", token_);
    } else {
        settings.remove("session/token");
    }
    settings.setValue("session/user_id", QVariant::fromValue<qulonglong>(user_id_));
    settings.setValue("session/nickname", nickname_);
    // 记住最近使用的本机历史库账号，供离线缓存定位。
    settings.setValue("history/last_user_id", QVariant::fromValue<qulonglong>(user_id_));
    settings.setValue("history/last_nickname", nickname_);
    settings.setValue("server/host", host_);
    settings.setValue("server/port", port_);
    settings.setValue("server/tls", use_tls_);
    settings.sync();
}

void MainWindow::loadSession() {
    QSettings settings;
    if (!settings.value("login/auto_login", true).toBool()) {
        settings.remove("session/token");
        return;
    }
    const QString token = settings.value("session/token").toString();
    if (token.isEmpty()) {
        return;
    }
    host_ = QString::fromLatin1(chatbox::client::kServerHost);
    port_ = chatbox::client::kServerPort;
    use_tls_ = chatbox::client::kServerTls;
    token_ = token;
    user_id_ = settings.value("session/user_id").toULongLong();
    nickname_ = settings.value("session/nickname").toString();
    // 升级路径：早期版本只存了 session/*，这里补齐离线缓存定位用的两个键。
    if (user_id_ != 0) {
        settings.setValue("history/last_user_id",
                          QVariant::fromValue<qulonglong>(user_id_));
        settings.setValue("history/last_nickname", nickname_);
        settings.sync();
    }
    logged_in_ = true;
    login_page_->setBusy(true);
    login_page_->setStatus("正在恢复上次登录...");
    // 先打开本地历史库：连不上服务端时才能直接进入离线模式。
    openHistoryStore(user_id_);
    startConnection();
}

void MainWindow::logout() {
    if (offline_mode_) {
        // 离线模式没有服务端会话：直接回到登录页。
        finishLogout();
        return;
    }
    if (!logged_in_) {
        clearSession();
        return;
    }
    if (logging_out_) {
        return;
    }
    logging_out_ = true;
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdLogout;
    frame.header.seq = next_seq_++;
    sendFrame(frame);
    QTimer::singleShot(800, this, &MainWindow::finishLogout);
}

