#include "ui/main_window_support.h"

#include <QAction>
#include <QFile>
#include <QMenu>

#include "core/account_store.h"
#include "core/credential_store.h"
#include "ui/account_switcher_dialog.h"

void MainWindow::updateRightPanelVisibility() {
    if (right_panel_ == nullptr || right_mode_stack_ == nullptr) {
        return;
    }
    const bool has_context = context_type_ == ContextType::Room ||
                             context_type_ == ContextType::Private;
    right_panel_->setVisible(has_context && member_panel_visible_);
    if (more_button_ != nullptr) {
        more_button_->setIcon(chatbox::client::sidebarToggleIcon(
            right_panel_->isVisible(),
            QColor(chatbox::client::theme::kMuted)));
    }
    if (!has_context || !member_panel_visible_) {
        return;
    }
    if (context_type_ == ContextType::Room) {
        right_mode_stack_->setCurrentWidget(right_group_page_);
        const chatbox::client::GroupInfo* group =
            group_store_.find(static_cast<quint64>(current_room_id_));
        if (group != nullptr) {
            member_count_label_->setText(group->name);
            const bool active = group->status == 0;
            if (right_invite_button_ != nullptr) {
                right_invite_button_->setVisible(active);
                right_invite_button_->setEnabled(active && online_ &&
                                                 !offline_mode_);
            }
            if (right_leave_button_ != nullptr) {
                const bool can_leave =
                    active && !group->is_default && group->role != 2;
                right_leave_button_->setVisible(can_leave);
                right_leave_button_->setEnabled(can_leave && online_ &&
                                                !offline_mode_);
            }
        } else {
            member_count_label_->setText(current_peer_name_);
            if (right_invite_button_ != nullptr) {
                right_invite_button_->hide();
            }
            if (right_leave_button_ != nullptr) {
                right_leave_button_->hide();
            }
        }
    } else {
        right_mode_stack_->setCurrentWidget(right_private_page_);
        member_count_label_->setText(current_peer_name_);
        updatePrivateProfileCard();
    }
}

void MainWindow::updatePrivateProfileCard() {
    if (right_profile_name_ == nullptr) {
        return;
    }
    const QString display_name =
        chatbox::client::profileDisplayName(right_panel_profile_).trimmed();
    right_profile_name_->setText(display_name.isEmpty() ? current_peer_name_
                                                        : display_name);
    right_profile_id_->setText(chatbox::client::publicUserId(current_peer_id_));
    right_profile_signature_->setText(right_panel_profile_.signature);
    right_profile_avatar_->setPixmap(
        memberAvatarIcon(current_peer_id_, current_peer_name_, 56).pixmap(56, 56));
}

void MainWindow::toggleRightPanel() {
    if (context_type_ == ContextType::None) {
        setStatus("请先选择一个会话", true);
        return;
    }
    member_panel_visible_ = !member_panel_visible_;
    QSettings settings;
    settings.setValue(QStringLiteral("settings/member_panel"), member_panel_visible_);
    settings.sync();
    updateRightPanelVisibility();
}

void MainWindow::rememberCurrentAccount() {
    if (username_.trimmed().isEmpty() || user_id_ == 0) {
        return;
    }
    chatbox::client::AccountRecord record;
    record.username = username_.trimmed();
    record.nickname = nickname_.trimmed();
    record.user_id = user_id_;
    chatbox::client::upsertAccount(record);

    // Cache the real avatar so the switcher can show a face before the user is
    // authenticated again. Fallback initials cover the case where no avatar
    // has arrived yet.
    const QString path = chatbox::client::accountAvatarPath(record.username);
    if (path.isEmpty()) {
        return;
    }
    QImage image;
    if (avatar_cache_.lookup(user_id_, image) && !image.isNull()) {
        image.save(path, "PNG");
    }
}

void MainWindow::openAccountMenu() {
    if (my_profile_button_ == nullptr) {
        return;
    }
    QMenu menu(this);
    menu.addAction(QStringLiteral("查看我的资料"), this,
                   &MainWindow::openOwnProfile);

    QMenu* switch_menu = menu.addMenu(QStringLiteral("切换账号"));
    const QVector<chatbox::client::AccountRecord> records =
        chatbox::client::loadAccounts();
    if (records.isEmpty()) {
        QAction* empty = switch_menu->addAction(
            QStringLiteral("登录一次后这里会显示账号"));
        empty->setEnabled(false);
    } else {
        for (const chatbox::client::AccountRecord& record : records) {
            const bool is_current = record.username == username_;
            QString label = record.nickname.isEmpty() ? record.username
                                                      : record.nickname;
            if (is_current) {
                label += QStringLiteral("（当前）");
            }
            QAction* action = switch_menu->addAction(label);
            action->setCheckable(true);
            action->setChecked(is_current);
            action->setEnabled(!is_current);
            action->setToolTip(record.username);
            const QString target = record.username;
            connect(action, &QAction::triggered, this,
                    [this, target] { switchToAccount(target); });
        }
        switch_menu->addSeparator();
        switch_menu->addAction(QStringLiteral("管理账号…"), this, [this] {
            AccountSwitcherDialog dialog(username_, this);
            if (dialog.exec() == QDialog::Accepted &&
                !dialog.selectedUsername().isEmpty()) {
                switchToAccount(dialog.selectedUsername());
            }
        });
    }

    menu.addSeparator();
    menu.addAction(QStringLiteral("设置…"), this, &MainWindow::openSettings);
    menu.addAction(QStringLiteral("退出登录"), this, [this] {
        if (chatbox::client::confirmDanger(this, "退出登录",
                                           "确定要退出登录吗？", "退出登录")) {
            logout();
        }
    });
    menu.exec(my_profile_button_->mapToGlobal(
        QPoint(0, my_profile_button_->height())));
}

void MainWindow::switchToAccount(const QString& username) {
    const QString target = username.trimmed();
    if (target.isEmpty() || target == username_) {
        return;
    }
    const QString host = QString::fromLatin1(chatbox::client::kServerHost);
    const quint16 port = chatbox::client::kServerPort;
    const bool tls = chatbox::client::kServerTls;

    QString password;
    const bool has_password =
        chatbox::client::loadAccountCredentials(target, &password);

    logging_out_ = true;
    logged_in_ = false;
    logout_finishing_ = false;
    if (network_ != nullptr) {
        network_->requestStop();
    }
    clearSession();
    login_page_->resetToLogin();
    login_page_->setUsername(target);
    if (stack_ != nullptr) {
        stack_->setCurrentWidget(login_page_);
    }

    if (!has_password) {
        // Nothing to log in with: park on the login page with the username
        // filled in so the user only has to type the password.
        login_page_->setStatus(
            QStringLiteral("请为 %1 输入密码后登录").arg(target), true);
        setStatus(QStringLiteral("切换到 %1：需要重新输入密码").arg(target), true);
        return;
    }

    login_page_->setStatus(QStringLiteral("正在切换到 %1...").arg(target));
    onLoginRequested(host, port, target, password, tls);
}

void MainWindow::updateOwnProfileStrip() {
    if (my_profile_button_ == nullptr || my_profile_avatar_ == nullptr ||
        my_profile_name_ == nullptr || my_profile_uid_ == nullptr) {
        return;
    }
    QString name = nickname_.trimmed();
    if (name.isEmpty()) {
        name = username_.trimmed();
    }
    if (name.isEmpty()) {
        name = "未登录";
    }
    my_profile_name_->setText(name);
    my_profile_uid_->setText(
        user_id_ == 0 ? QString() : chatbox::client::publicUserId(user_id_));

    QPixmap avatar;
    QImage image;
    if (user_id_ != 0 && avatar_cache_.lookup(user_id_, image) && !image.isNull()) {
        avatar = QPixmap::fromImage(chatbox::client::circularAvatar(image, 32));
    } else {
        avatar = avatarIcon(name, user_id_).pixmap(32, 32);
    }
    my_profile_avatar_->setPixmap(avatar);
    my_profile_button_->setToolTip(
        user_id_ == 0
            ? QString("未登录")
            : QString("%1\n%2").arg(name, chatbox::client::publicUserId(user_id_)));
    // Keep the switcher entry (nickname + cached avatar) in step with whatever
    // the strip is currently showing.
    rememberCurrentAccount();
}

bool MainWindow::isCurrentProfileRequest(quint64 user_id, quint32 seq) const {
    return seq != 0 && pending_profile_seqs_.value(user_id) == seq;
}

bool MainWindow::isCurrentAvatarRequest(quint64 user_id, quint32 seq) const {
    return seq != 0 && pending_avatar_seqs_.value(user_id) == seq;
}

void MainWindow::clearPendingProfileRequest(quint64 user_id, quint32 seq) {
    if (isCurrentProfileRequest(user_id, seq)) {
        pending_profile_seqs_.remove(user_id);
        pending_profile_requests_.remove(user_id);
    }
    profile_request_seqs_.remove(seq);
    if (pending_profile_requests_.isEmpty() && profile_timeout_timer_ != nullptr) {
        profile_timeout_timer_->stop();
    }
}

void MainWindow::clearPendingAvatarRequest(quint64 user_id, quint32 seq) {
    if (isCurrentAvatarRequest(user_id, seq)) {
        pending_avatar_seqs_.remove(user_id);
        pending_avatar_versions_.remove(user_id);
        pending_avatar_requests_.remove(user_id);
    }
    avatar_request_seqs_.remove(seq);
    if (pending_avatar_requests_.isEmpty() && avatar_timeout_timer_ != nullptr) {
        avatar_timeout_timer_->stop();
    }
}

void MainWindow::requestProfile(quint64 user_id) {
    if (user_id == 0 || offline_mode_) {
        return;
    }
    if (!connected_ || !logged_in_) {
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus("网络未连接，无法加载资料", true);
        }
        setStatus("网络未连接，无法加载资料", true);
        return;
    }
    if (pending_profile_requests_.contains(user_id) ||
        pending_profile_seqs_.contains(user_id)) {
        return;
    }

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdProfileGet;
    const quint32 seq = next_seq_++;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU64(user_id);
    frame.body = writer.data();
    if (!sendFrame(frame)) {
        // No request reached the server: keep the dialog usable instead of
        // leaving it in its "loading" busy state.
        clearPendingProfileRequest(user_id, seq);
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus("网络未连接，无法加载资料", true);
        }
        return;
    }
    pending_profile_requests_.insert(user_id);
    pending_profile_seqs_.insert(user_id, seq);
    profile_request_seqs_.insert(seq, user_id);
    qInfo("PROFILE_GET user_id=%llu seq=%u",
          static_cast<unsigned long long>(user_id), seq);
    if (profile_timeout_timer_ != nullptr) {
        profile_timeout_timer_->start();
    }
}

void MainWindow::requestAvatar(quint64 user_id, quint32 version) {
    if (user_id == 0 || !connected_ || !logged_in_ || offline_mode_) {
        return;
    }
    if (pending_avatar_requests_.contains(user_id) ||
        pending_avatar_seqs_.contains(user_id)) {
        return;
    }

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdAvatarGet;
    const quint32 seq = next_seq_++;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU64(user_id);
    frame.body = writer.data();
    if (!sendFrame(frame)) {
        return;
    }
    pending_avatar_requests_.insert(user_id);
    pending_avatar_seqs_.insert(user_id, seq);
    pending_avatar_versions_.insert(user_id, version);
    avatar_request_seqs_.insert(seq, user_id);
    qInfo("AVATAR_GET user_id=%llu version=%u seq=%u",
          static_cast<unsigned long long>(user_id), version, seq);
    if (avatar_timeout_timer_ != nullptr) {
        avatar_timeout_timer_->start();
    }
}

void MainWindow::requestPeerAvatar(quint64 peer_id) {
    if (peer_id == 0 || peer_id == user_id_ || offline_mode_) {
        return;
    }
    const auto it = friend_views_.constFind(peer_id);
    if (it != friend_views_.constEnd()) {
        if (it->avatar_version != 0 &&
            avatar_cache_.needsFetch(peer_id, it->avatar_version)) {
            requestAvatar(peer_id, it->avatar_version);
        }
        return;
    }
    // A non-friend has no version in any list yet: ask once with version 0 and
    // cache the version returned by the server, including "no avatar".
    if (!avatar_cache_.contains(peer_id)) {
        requestAvatar(peer_id, 0);
    }
}

void MainWindow::updatePrivateHeaderAvatar(quint64 peer_id, const QString& name) {
    if (context_avatar_ == nullptr || peer_id == 0) {
        return;
    }
    QImage image;
    if (avatar_cache_.lookup(peer_id, image) && !image.isNull()) {
        context_avatar_->setPixmap(
            QPixmap::fromImage(chatbox::client::circularAvatar(image, 32)));
        return;
    }
    QString fallback_name = name;
    if (fallback_name.isEmpty()) {
        fallback_name = peer_names_.value(peer_id);
    }
    if (fallback_name.isEmpty()) {
        fallback_name = chatbox::client::publicUserId(peer_id);
    }
    context_avatar_->setPixmap(avatarIcon(fallback_name, peer_id).pixmap(32, 32));
}

QIcon MainWindow::memberAvatarIcon(quint64 member_id, const QString& nickname,
                                   int size) {
    QImage image;
    if (member_id != 0 && avatar_cache_.lookup(member_id, image) &&
        !image.isNull()) {
        return QIcon(
            QPixmap::fromImage(chatbox::client::circularAvatar(image, size)));
    }
    const QString fallback_name =
        nickname.isEmpty() ? chatbox::client::publicUserId(member_id)
                           : nickname;
    return avatarIcon(fallback_name, member_id);
}

void MainWindow::refreshMemberListAvatar(quint64 user_id) {
    if (member_list_ == nullptr || user_id == 0) {
        return;
    }
    for (int i = 0; i < member_list_->count(); ++i) {
        QListWidgetItem* item = member_list_->item(i);
        if (item != nullptr &&
            item->data(Qt::UserRole).toULongLong() == user_id) {
            item->setIcon(memberAvatarIcon(
                user_id,
                item->data(Qt::UserRole + 1).toString().isEmpty()
                    ? item->text()
                    : item->data(Qt::UserRole + 1).toString(),
                28));
            break;
        }
    }
}

void MainWindow::closeProfileDialog() {
    if (profile_dialog_ == nullptr) {
        return;
    }
    ProfileDialog* dialog = profile_dialog_;
    profile_dialog_ = nullptr;
    profile_dialog_user_id_ = 0;
    profile_dialog_fallback_name_.clear();
    profile_dialog_profile_ = chatbox::client::UserProfile();
    dialog->close();
}

void MainWindow::onProfileAvatarReceived(quint64 user_id, quint32 version,
                                         const QImage& image) {
    if (user_id == 0) {
        return;
    }
    quint32 known_version = 0;
    const auto known_view = friend_views_.constFind(user_id);
    if (known_view != friend_views_.constEnd()) {
        known_version = std::max(known_version, known_view->avatar_version);
    }
    const auto known_pending = pending_avatar_versions_.constFind(user_id);
    if (known_pending != pending_avatar_versions_.constEnd()) {
        known_version = std::max(known_version, *known_pending);
    }
    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
        known_version =
            std::max(known_version, profile_dialog_profile_.avatar_version);
    }
    if (user_id == user_id_) {
        // Keep the newest self version even when the self dialog is closed, so
        // an older in-flight avatar/clear response cannot replace it.
        known_version = std::max(known_version, own_avatar_version_);
    }
    if (version < known_version) {
        qInfo("stale avatar discarded user_id=%llu version=%u known=%u",
              static_cast<unsigned long long>(user_id), version, known_version);
        if (avatar_cache_.needsFetch(user_id, known_version)) {
            // The older in-flight request suppressed the request for this
            // version. Its response has now arrived, so fetch the known
            // newer version. requestAvatar() is a no-op if it is pending.
            requestAvatar(user_id, known_version);
        }
        return;
    }

    // AvatarCache::store ignores older versions, so a late stale response
    // cannot replace a newer cached avatar.
    avatar_cache_.store(user_id, version, image);
    qInfo("avatar loaded user_id=%llu version=%u bytes=%d",
          static_cast<unsigned long long>(user_id), version,
          image.isNull() ? 0 : static_cast<int>(image.sizeInBytes()));
    const auto view_it = friend_views_.constFind(user_id);
    const quint32 newer_version =
        view_it != friend_views_.constEnd() && view_it->avatar_version > version
            ? view_it->avatar_version
            : 0;
    if (friend_ids_.contains(user_id)) {
        refreshFriendListView();
    }
    refreshMemberListAvatar(user_id);
    if (user_id == user_id_) {
        own_avatar_version_ = std::max(own_avatar_version_, version);
        updateOwnProfileStrip();
    }
    if (context_type_ == ContextType::Private && current_peer_id_ == user_id &&
        user_id != user_id_) {
        updatePrivateHeaderAvatar(user_id, current_peer_name_);
    }
    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id &&
        version >= profile_dialog_profile_.avatar_version) {
        profile_dialog_profile_.avatar = image;
        profile_dialog_profile_.avatar_version = version;
        profile_dialog_->setAvatar(image);
    }
    for (const chatbox::client::ChatMessage& message : message_store_.messages()) {
        if (message.sender_id == user_id) {
            // The transcript embeds avatars as QTextDocument resources; rebuild
            // the current page so a late image replaces the fallback initials.
            renderCurrentConversation(ScrollMode::kPreserve);
            break;
        }
    }
    if (newer_version != 0 &&
        avatar_cache_.needsFetch(user_id, newer_version)) {
        // A stale response arrived after a push announced a newer version.
        // requestAvatar() is a no-op while that newer request is still pending.
        requestAvatar(user_id, newer_version);
    }
    if (context_type_ == ContextType::Private && user_id == current_peer_id_) {
        updatePrivateProfileCard();
    }
}

void MainWindow::onProfileDataReceived(
    const chatbox::client::UserProfile& profile) {
    if (profile.user_id == 0) {
        return;
    }
    if (context_type_ == ContextType::Private &&
        profile.user_id == current_peer_id_) {
        right_panel_profile_ = profile;
        updatePrivateProfileCard();
    }

    quint32 known_version = 0;
    const auto known_view = friend_views_.constFind(profile.user_id);
    if (known_view != friend_views_.constEnd()) {
        known_version = std::max(known_version, known_view->avatar_version);
    }
    const auto known_pending = pending_avatar_versions_.constFind(profile.user_id);
    if (known_pending != pending_avatar_versions_.constEnd()) {
        known_version = std::max(known_version, *known_pending);
    }
    if (profile_dialog_ != nullptr &&
        profile_dialog_user_id_ == profile.user_id) {
        known_version =
            std::max(known_version, profile_dialog_profile_.avatar_version);
    }
    if (profile.user_id == user_id_) {
        // The self dialog may be closed; own_avatar_version_ still remembers
        // the newest version learned from a self profile/avatar response.
        known_version = std::max(known_version, own_avatar_version_);
    }
    if (profile.avatar_version >= known_version) {
        // AvatarCache::store is also monotonic, which covers self/non-friend
        // responses where no friend view or pending version is available.
        avatar_cache_.store(profile.user_id, profile.avatar_version,
                            profile.avatar);
    } else {
        qInfo("stale profile avatar discarded user_id=%llu version=%u known=%u",
              static_cast<unsigned long long>(profile.user_id),
              profile.avatar_version, known_version);
    }
    qInfo("profile loaded user_id=%llu version=%u relationship=%u",
          static_cast<unsigned long long>(profile.user_id),
          profile.avatar_version, static_cast<unsigned>(profile.relationship));

    const auto friend_it = friend_views_.find(profile.user_id);
    if (friend_it != friend_views_.end()) {
        if (!profile.nickname.isEmpty()) {
            friend_it->nickname = profile.nickname;
        }
        friend_it->remark = profile.remark;
        if (profile.avatar_version >= friend_it->avatar_version) {
            friend_it->avatar_version = profile.avatar_version;
        }
        friend_it->online = profile.online;
        peer_names_.insert(profile.user_id, friend_it->nickname);
        refreshFriendListView();
    }

    if (profile.user_id == user_id_) {
        if (!profile.nickname.isEmpty()) {
            nickname_ = profile.nickname;
        }
        if (!profile.username.isEmpty()) {
            username_ = profile.username;
        }
        own_avatar_version_ =
            std::max(own_avatar_version_, static_cast<quint32>(profile.avatar_version));
        saveSession();
        updateOwnProfileStrip();
    } else if (context_type_ == ContextType::Private &&
               current_peer_id_ == profile.user_id) {
        updatePrivateHeaderAvatar(profile.user_id, current_peer_name_);
    }
    refreshMemberListAvatar(profile.user_id);

    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == profile.user_id) {
        chatbox::client::UserProfile displayed = profile;
        if (displayed.avatar_version < profile_dialog_profile_.avatar_version) {
            displayed.avatar_version = profile_dialog_profile_.avatar_version;
            displayed.avatar = profile_dialog_profile_.avatar;
        }
        profile_dialog_profile_ = displayed;
        profile_dialog_fallback_name_ =
            chatbox::client::profileDisplayName(displayed).trimmed();
        profile_dialog_->setProfile(displayed, displayed.user_id == user_id_);
        profile_dialog_->setBusy(false);
        profile_dialog_->setStatus(QString());
    }
}

void MainWindow::openOwnProfile() {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法查看资料", true);
        return;
    }
    if (user_id_ == 0) {
        setStatus("请先登录", true);
        return;
    }
    openProfile(user_id_, nickname_);
}

void MainWindow::openProfile(quint64 user_id, const QString& fallback_name) {
    if (offline_mode_ || !online_) {
        setStatus("当前离线，无法查看资料", true);
        return;
    }
    if (user_id == 0) {
        return;
    }
    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
        profile_dialog_->show();
        profile_dialog_->raise();
        profile_dialog_->activateWindow();
        if (connected_ && logged_in_ &&
            !pending_profile_requests_.contains(user_id) &&
            !pending_profile_seqs_.contains(user_id)) {
            profile_dialog_->setBusy(true);
            profile_dialog_->setStatus("正在加载资料...");
            requestProfile(user_id);
        }
        return;
    }
    closeProfileDialog();

    const bool self = user_id == user_id_;
    chatbox::client::UserProfile fallback;
    fallback.user_id = user_id;
    fallback.relationship = self ? 0 : 2;
    if (self) {
        fallback.username = username_;
        fallback.nickname = nickname_;
        fallback.online = true;
    } else {
        const auto it = friend_views_.constFind(user_id);
        if (it != friend_views_.constEnd()) {
            fallback.username = it->username;
            fallback.nickname = it->nickname;
            fallback.remark = it->remark;
            fallback.avatar_version = it->avatar_version;
            fallback.online = it->online;
            fallback.relationship = 1;
        } else {
            fallback.nickname = fallback_name;
            if (fallback.nickname.isEmpty()) {
                fallback.nickname = peer_names_.value(user_id);
            }
        }
    }
    QImage cached_avatar;
    if (avatar_cache_.lookup(user_id, cached_avatar) && !cached_avatar.isNull()) {
        fallback.avatar = cached_avatar;
    }

    auto* dialog = new ProfileDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    profile_dialog_ = dialog;
    profile_dialog_user_id_ = user_id;
    profile_dialog_fallback_name_ = fallback_name;
    profile_dialog_profile_ = fallback;

    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (profile_dialog_ != dialog) {
            return;
        }
        // Always drop the dangling pointer, including during app close.
        profile_dialog_ = nullptr;
        if (shutting_down_) {
            // Destructor teardown disconnects this handler before deleting
            // dialogs; while members are still alive we only need the pointer
            // cleared and avoid touching the remaining dialog state.
            return;
        }
        profile_dialog_user_id_ = 0;
        profile_dialog_fallback_name_.clear();
        profile_dialog_profile_ = chatbox::client::UserProfile();
    });
    connect(dialog, &ProfileDialog::saveRequested, this,
            &MainWindow::onProfileSaveRequested);
    connect(dialog, &ProfileDialog::messageRequested, this, [this](quint64 id) {
        if (profile_dialog_user_id_ != id) {
            return;
        }
        QString name =
            chatbox::client::profileDisplayName(profile_dialog_profile_).trimmed();
        if (name.isEmpty()) {
            name = profile_dialog_fallback_name_.trimmed();
        }
        if (name.isEmpty()) {
            name = peer_names_.value(id);
        }
        if (name.isEmpty()) {
            name = chatbox::client::publicUserId(id);
        }
        selectPrivate(id, name);
        closeProfileDialog();
    });
    connect(dialog, &ProfileDialog::addFriendRequested, this, [this, dialog](quint64 id) {
        if (!connected_ || !logged_in_) {
            dialog->setStatus("网络未连接，无法发送好友申请", true);
            return;
        }
        dialog->setStatus("正在发送好友申请...");
        sendFriendRequest(id);
    });
    connect(dialog, &ProfileDialog::removeFriendRequested, this, [this, dialog](quint64 id) {
        if (!connected_ || !logged_in_) {
            dialog->setStatus("网络未连接，无法删除好友", true);
            return;
        }
        QString name =
            chatbox::client::profileDisplayName(profile_dialog_profile_).trimmed();
        if (name.isEmpty()) {
            name = profile_dialog_fallback_name_.trimmed();
        }
        if (name.isEmpty()) {
            name = peer_names_.value(id);
        }
        if (name.isEmpty()) {
            name = chatbox::client::publicUserId(id);
        }
        if (!chatbox::client::confirmDanger(
                dialog, "删除好友",
                QString("确定要删除好友“%1”吗？").arg(name), "删除")) {
            return;
        }
        sendFriendRemove(id);
        setStatus("正在删除好友...");
        closeProfileDialog();
    });
    connect(dialog, &ProfileDialog::remarkRequested, this, [this](quint64 id) {
        setFriendRemark(id);
    });
    connect(dialog, &ProfileDialog::refreshRequested, this, [this, dialog](quint64 id) {
        if (!connected_ || !logged_in_) {
            dialog->setBusy(false);
            dialog->setStatus("网络未连接，无法刷新", true);
            return;
        }
        dialog->setBusy(true);
        dialog->setStatus("正在加载资料...");
        requestProfile(id);
    });
    connect(dialog, &ProfileDialog::friendRequestsRequested, this,
            &MainWindow::openFriendRequests);

    dialog->setProfile(fallback, self);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();

    if (!connected_ || !logged_in_) {
        dialog->setBusy(false);
        dialog->setStatus("网络未连接，无法加载资料", true);
        setStatus("网络未连接，无法加载资料", true);
        return;
    }
    dialog->setBusy(true);
    dialog->setStatus("正在加载资料...");
    requestProfile(user_id);
}

void MainWindow::onProfileSaveRequested(const QString& nickname,
                                        const QString& signature,
                                        int avatar_action,
                                        const QByteArray& avatar_jpeg) {
    if (user_id_ == 0 || profile_dialog_ == nullptr ||
        profile_dialog_user_id_ != user_id_) {
        // The save button only exists in self mode. Never touch a dialog that
        // is showing another user.
        setStatus("资料窗口已关闭，无法保存", true);
        return;
    }

    const auto fail = [this](const QString& text) {
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus(text, true);
        }
        setStatus(text, true);
    };

    const QString trimmed_nickname = nickname.trimmed();
    const QString trimmed_signature = signature.trimmed();
    const int nickname_count = trimmed_nickname.toUcs4().size();
    const int signature_count = trimmed_signature.toUcs4().size();
    if (nickname_count < 1) {
        fail("昵称不能为空");
        return;
    }
    if (nickname_count > 32) {
        fail(QString("昵称最多 32 个字符（当前 %1 个）").arg(nickname_count));
        return;
    }
    if (signature_count > 128) {
        fail(QString("个性签名最多 128 个字符（当前 %1 个）").arg(signature_count));
        return;
    }
    if (avatar_action < 0 || avatar_action > 2) {
        fail("头像操作无效，请重新选择");
        return;
    }
    const int kAvatarMaxBytes = 48 * 1024;
    if (avatar_action == 1 &&
        (avatar_jpeg.isEmpty() || avatar_jpeg.size() > kAvatarMaxBytes)) {
        fail(avatar_jpeg.isEmpty() ? QString("头像数据无效，请重新选择")
                                   : QString("头像不能超过 48 KiB"));
        return;
    }
    if (!connected_) {
        fail("网络未连接，无法保存资料");
        return;
    }
    if (!logged_in_ || !online_) {
        fail("会话未就绪，无法保存资料");
        return;
    }
    if (pending_profile_update_seq_ != 0) {
        // A second click cannot start another request while the first one is
        // still waiting for its response.
        profile_dialog_->setBusy(true);
        profile_dialog_->setStatus("正在保存资料...");
        return;
    }

    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdProfileUpdate;
    const quint32 seq = next_seq_++;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(trimmed_nickname));
    writer.putString(chatbox::toUtf8(trimmed_signature));
    writer.putU8(static_cast<uint8_t>(avatar_action));
    if (avatar_action == 1) {
        writer.putString(std::string(avatar_jpeg.constData(),
                                     static_cast<std::size_t>(avatar_jpeg.size())));
    }
    frame.body = writer.data();

    pending_profile_update_seq_ = seq;
    profile_dialog_->setBusy(true);
    profile_dialog_->setStatus("正在保存资料...");
    if (!sendFrame(frame)) {
        pending_profile_update_seq_ = 0;
        profile_dialog_->setBusy(false);
        profile_dialog_->setStatus("网络未连接，无法保存资料", true);
        setStatus("网络未连接，无法保存资料", true);
        return;
    }

    qInfo("PROFILE_UPDATE nickname=%s signature_bytes=%d avatar_action=%d "
          "avatar_bytes=%d seq=%u",
          qPrintable(trimmed_nickname), trimmed_signature.toUtf8().size(),
          avatar_action, avatar_action == 1 ? avatar_jpeg.size() : 0, seq);
    QTimer::singleShot(8000, this, [this, seq] {
        if (pending_profile_update_seq_ != seq) {
            return;
        }
        pending_profile_update_seq_ = 0;
        if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
            const bool still_loading =
                pending_profile_requests_.contains(user_id_) ||
                pending_profile_seqs_.contains(user_id_);
            if (!still_loading) {
                profile_dialog_->setBusy(false);
            }
            profile_dialog_->setStatus("保存资料超时，请重试", true);
        }
        setStatus("保存资料超时，请重试", true);
        qWarning("PROFILE_UPDATE timeout seq=%u", seq);
    });
}

void MainWindow::applyOwnProfileUpdate(const QString& nickname,
                                       const QString& signature,
                                       quint32 avatar_version) {
    if (user_id_ == 0) {
        return;
    }
    // Use the version returned by the server instead of inferring a change
    // from the request action: a nickname-only save can reveal a newer version
    // when the self profile request never loaded it.
    quint32 known_version = own_avatar_version_;
    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
        known_version =
            std::max(known_version, profile_dialog_profile_.avatar_version);
    }
    const auto pending_version = pending_avatar_versions_.constFind(user_id_);
    if (pending_version != pending_avatar_versions_.constEnd()) {
        known_version = std::max(known_version, *pending_version);
    }
    const bool version_changed = avatar_version > known_version;

    if (!nickname.isEmpty()) {
        nickname_ = nickname;
        QSettings settings;
        settings.setValue("session/nickname", nickname_);
        settings.sync();
    }
    own_avatar_version_ = std::max(own_avatar_version_, avatar_version);
    if (version_changed) {
        // The response carries no avatar bytes. Drop the old image so a clear
        // is visible immediately and a new upload is filled by AVATAR_GET.
        avatar_cache_.remove(user_id_);
    }
    peer_names_.insert(user_id_, nickname_);
    updateOwnProfileStrip();
    updateRoomMemberNickname(user_id_, nickname_);

    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id_) {
        chatbox::client::UserProfile displayed = profile_dialog_profile_;
        displayed.nickname = nickname_;
        displayed.signature = signature;
        displayed.relationship = 0;
        displayed.online = true;
        if (avatar_version >= displayed.avatar_version) {
            if (avatar_version > displayed.avatar_version) {
                displayed.avatar = QImage();
            }
            displayed.avatar_version = avatar_version;
        }
        profile_dialog_profile_ = displayed;
        profile_dialog_fallback_name_ =
            chatbox::client::profileDisplayName(displayed).trimmed();
        const bool still_loading =
            pending_profile_requests_.contains(user_id_) ||
            pending_profile_seqs_.contains(user_id_);
        profile_dialog_->setProfile(displayed, true);
        if (still_loading) {
            profile_dialog_->setBusy(true);
            profile_dialog_->setStatus("正在加载资料...");
        } else {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus("资料已保存");
        }
    }

    if (avatar_version != 0 &&
        avatar_cache_.needsFetch(user_id_, avatar_version)) {
        requestAvatar(user_id_, avatar_version);
    }
    qInfo("profile updated user_id=%llu version=%u avatar_changed=%d",
          static_cast<unsigned long long>(user_id_), avatar_version,
          version_changed ? 1 : 0);
    setStatus("资料已保存");
}

void MainWindow::applyProfileUpdatePush(quint64 user_id, const QString& nickname,
                                        quint32 avatar_version) {
    if (user_id == 0 || user_id == user_id_) {
        return;
    }

    const auto view_it = friend_views_.find(user_id);
    if (view_it != friend_views_.end()) {
        const quint32 previous_version = view_it->avatar_version;
        if (!nickname.isEmpty()) {
            view_it->nickname = nickname;
        }
        if (avatar_version >= view_it->avatar_version) {
            view_it->avatar_version = avatar_version;
        }
        peer_names_.insert(user_id, view_it->nickname);
        if (view_it->avatar_version != previous_version) {
            avatar_cache_.remove(user_id);
            if (view_it->avatar_version != 0) {
                requestAvatar(user_id, view_it->avatar_version);
            }
        }
        refreshFriendListView();
    } else {
        // The friend list may still be loading. The push is still the newest
        // public name/version we know about.
        if (!nickname.isEmpty()) {
            peer_names_.insert(user_id, nickname);
        }
        avatar_cache_.remove(user_id);
        if (avatar_version != 0) {
            requestAvatar(user_id, avatar_version);
        }
    }

    if (!nickname.isEmpty()) {
        updateRoomMemberNickname(user_id, nickname);
    }

    if (context_type_ == ContextType::Private && current_peer_id_ == user_id) {
        QString display_name;
        const auto current_view = friend_views_.constFind(user_id);
        if (current_view != friend_views_.constEnd()) {
            const QString remark = current_view->remark.trimmed();
            display_name = remark.isEmpty() ? current_view->nickname : remark;
        }
        if (display_name.isEmpty()) {
            display_name = nickname;
        }
        if (display_name.isEmpty()) {
            display_name = current_peer_name_;
        }
        if (!display_name.isEmpty() && display_name != current_peer_name_) {
            current_peer_name_ = display_name;
            context_label_->setText("私聊: " + display_name);
        }
        updatePrivateHeaderAvatar(user_id, current_peer_name_);
    }

    if (profile_dialog_ != nullptr && profile_dialog_user_id_ == user_id) {
        chatbox::client::UserProfile displayed = profile_dialog_profile_;
        bool changed = false;
        if (!nickname.isEmpty() && displayed.nickname != nickname) {
            displayed.nickname = nickname;
            changed = true;
        }
        if (avatar_version >= displayed.avatar_version) {
            if (avatar_version > displayed.avatar_version) {
                displayed.avatar = QImage();
                changed = true;
            }
            displayed.avatar_version = avatar_version;
        }
        if (changed) {
            profile_dialog_profile_ = displayed;
            profile_dialog_fallback_name_ =
                chatbox::client::profileDisplayName(displayed).trimmed();
            const bool still_loading =
                pending_profile_requests_.contains(user_id) ||
                pending_profile_seqs_.contains(user_id);
            profile_dialog_->setProfile(displayed, false);
            if (still_loading) {
                profile_dialog_->setBusy(true);
                profile_dialog_->setStatus("正在加载资料...");
            } else {
                profile_dialog_->setBusy(false);
                profile_dialog_->setStatus(QString());
            }
        }
    }
}
