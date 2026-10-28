#include "ui/main_window_support.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("ChatBox");
    // Keep the initial window inside the usable logical screen, leaving room
    // for the native frame. At 150% DPI a 1366x768 display only exposes about
    // 910x512 logical pixels; a 560px minimum height would put the composer
    // below the screen. 90% also gives 1920x1080 @ 150% some breathing room.
    const QRect available =
        QGuiApplication::primaryScreen() != nullptr
            ? QGuiApplication::primaryScreen()->availableGeometry()
            : QRect(0, 0, 1120, 720);
    resize(qMin(1120, (available.width() * 9) / 10),
           qMin(720, (available.height() * 9) / 10));
    setMinimumSize(qMin(760, available.width()),
                   qMin(460, available.height()));
    {
        QSettings settings;
        member_panel_visible_ =
            settings.value(QStringLiteral("settings/member_panel"), false).toBool();
    }
    buildUi();
    QTimer::singleShot(1500, this, &MainWindow::checkForUpdates);

    auto* message_search_shortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this);
    connect(message_search_shortcut, &QShortcut::activated, this,
            &MainWindow::openMessageSearch);
    auto* user_search_shortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")), this);
    connect(user_search_shortcut, &QShortcut::activated, this,
            &MainWindow::openUserSearch);
    auto* settings_shortcut =
        new QShortcut(QKeySequence(QStringLiteral("Ctrl+,")), this);
    connect(settings_shortcut, &QShortcut::activated, this, &MainWindow::openSettings);
    auto* previous_conversation =
        new QShortcut(QKeySequence(QStringLiteral("Alt+Up")), this);
    connect(previous_conversation, &QShortcut::activated, this,
            [this] { selectAdjacentConversation(-1); });
    auto* next_conversation =
        new QShortcut(QKeySequence(QStringLiteral("Alt+Down")), this);
    connect(next_conversation, &QShortcut::activated, this,
            [this] { selectAdjacentConversation(1); });
    auto* cancel_reply =
        new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(cancel_reply, &QShortcut::activated, this, [this] {
        if (reply_to_id_ != 0) {
            clearReply();
        }
    });

    QSettings initial_settings;
    notifications_enabled_ =
        initial_settings.value("settings/notifications", true).toBool();
    system_popup_enabled_ =
        initial_settings.value("settings/system_popup", true).toBool();
    if (initial_settings.value("settings/autostart", false).toBool()) {
        if (!chatbox::client::setAutostartEnabled(
                true, QCoreApplication::applicationFilePath())) {
            qWarning("autostart refresh failed");
        }
    }
    send_timeout_timer_ = new QTimer(this);
    send_timeout_timer_->setInterval(1000);
    connect(send_timeout_timer_, &QTimer::timeout, this, &MainWindow::onSendTimeout);
    send_timeout_timer_->start();
    auth_timeout_timer_ = new QTimer(this);
    auth_timeout_timer_->setSingleShot(true);
    auth_timeout_timer_->setInterval(8000);
    connect(auth_timeout_timer_, &QTimer::timeout, this, &MainWindow::onAuthTimeout);
    reconnect_timer_ = new QTimer(this);
    reconnect_timer_->setSingleShot(true);
    connect(reconnect_timer_, &QTimer::timeout, this, [this] {
        reconnect_scheduled_ = false;
        if (!shutting_down_ && logged_in_) {
            startConnection();
        }
    });
    search_timeout_timer_ = new QTimer(this);
    search_timeout_timer_->setSingleShot(true);
    search_timeout_timer_->setInterval(8000);
    connect(search_timeout_timer_, &QTimer::timeout, this, [this] {
        user_search_seq_ = 0;
        if (user_search_dialog_ != nullptr) {
            user_search_dialog_->setBusy(false);
            user_search_dialog_->setStatus("搜索超时，请重试", true);
        }
    });
    message_search_timeout_timer_ = new QTimer(this);
    message_search_timeout_timer_->setSingleShot(true);
    message_search_timeout_timer_->setInterval(8000);
    connect(message_search_timeout_timer_, &QTimer::timeout, this, [this] {
        message_search_seq_ = 0;
        if (panel_search_seq_ != 0) {
            panel_search_seq_ = 0;
            if (right_search_status_ != nullptr) {
                right_search_status_->setText("搜索超时，请重试");
            }
        }
        if (message_search_dialog_ != nullptr) {
            message_search_dialog_->setBusy(false);
            message_search_dialog_->setStatus("搜索超时，请重试", true);
        }
    });
    profile_timeout_timer_ = new QTimer(this);
    profile_timeout_timer_->setSingleShot(true);
    profile_timeout_timer_->setInterval(8000);
    connect(profile_timeout_timer_, &QTimer::timeout, this, [this] {
        if (pending_profile_requests_.isEmpty()) {
            return;
        }
        pending_profile_requests_.clear();
        profile_request_seqs_.clear();
        pending_profile_seqs_.clear();
        if (profile_dialog_ != nullptr) {
            profile_dialog_->setBusy(false);
            profile_dialog_->setStatus("加载资料超时，请重试", true);
        }
        setStatus("加载资料超时，请重试", true);
    });
    avatar_timeout_timer_ = new QTimer(this);
    avatar_timeout_timer_->setSingleShot(true);
    avatar_timeout_timer_->setInterval(8000);
    connect(avatar_timeout_timer_, &QTimer::timeout, this, [this] {
        if (pending_avatar_requests_.isEmpty()) {
            return;
        }
        qWarning("avatar requests timed out: %d pending",
                 static_cast<int>(pending_avatar_requests_.size()));
        pending_avatar_requests_.clear();
        avatar_request_seqs_.clear();
        pending_avatar_seqs_.clear();
        pending_avatar_versions_.clear();
    });
    connect(chat_view_, &QTextBrowser::anchorClicked, this,
            [this](const QUrl& url) {
                if (url.scheme() == "retry") {
                    retryMessage(url.path());
                }
            });
    connect(chat_view_, &ChatView::hoveredMessage, this,
            [this](quint64 message_id, const QRect& meta_rect) {
                showMessageActions(message_id, meta_rect);
            });
    connect(chat_view_, &ChatView::hoverCleared, this,
            &MainWindow::hideMessageActionsUnlessHovered);
    connect(chat_view_, &ChatView::messageMenuRequested, this,
            &MainWindow::showMessageMenu);
    connect(chat_view_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) {
                if (programmatic_scroll_) {
                    return;
                }
                if (chat_view_ != nullptr) {
                    chat_view_->clearHoverState();
                }
                hideMessageActionBar();
                maybeLoadOlder();
                if (context_type_ == ContextType::Room &&
                    !message_store_.messages().isEmpty() &&
                    chat_view_->verticalScrollBar()->value() >=
                        chat_view_->verticalScrollBar()->maximum() - 4) {
                    const chatbox::client::GroupInfo* group = group_store_.find(
                        static_cast<quint64>(current_room_id_));
                    if (group != nullptr && group->unread > 0) {
                        markGroupRead(static_cast<quint64>(current_room_id_),
                                      message_store_.messages().back().message_id);
                    }
                }
            });
    if (message_action_bar_ != nullptr) {
        connect(message_action_bar_, &MessageActionBar::replyRequested, this, [this] {
            const chatbox::client::ChatMessage* message =
                message_store_.findMessage(action_bar_message_id_);
            if (message != nullptr) {
                setReplyTo(*message);
            }
            hideMessageActionBar();
        });
        connect(message_action_bar_, &MessageActionBar::recallRequested, this, [this] {
            const quint64 message_id = action_bar_message_id_;
            hideMessageActionBar();
            sendRecall(message_id);
        });
        connect(message_action_bar_, &MessageActionBar::hoverLeft, this, [this] {
            hideMessageActionBar();
            if (chat_view_ != nullptr) {
                chat_view_->clearHoverState();
            }
        });
    }
    loadSession();
    if (!logged_in_) {
        setStatus("请输入服务器和账号");
    }
}

MainWindow::~MainWindow() {
    shutting_down_ = true;
    // 本地历史库先关：解绑 MessageStore 后再关连接，避免留下悬空指针。
    closeHistoryStore();
    // Destroy the dialog(s) while all MainWindow members are still alive.
    // Without this, a child dialog (including one already closed with a
    // pending deleteLater) can be deleted by QObject base teardown after the
    // derived members, including the dialog state, are gone.
    const auto profile_dialogs = findChildren<ProfileDialog*>();
    for (ProfileDialog* dialog : profile_dialogs) {
        dialog->disconnect(this);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        delete dialog;
    }
    profile_dialog_ = nullptr;
    profile_dialog_user_id_ = 0;
    profile_dialog_fallback_name_.clear();
    profile_dialog_profile_ = chatbox::client::UserProfile();
    if (network_ != nullptr) {
        network_->requestStop();
        network_->wait();
    }
}

void MainWindow::buildUi() {
    stack_ = new QStackedWidget(this);
    login_page_ = new LoginPage(stack_);
    chat_page_ = buildChatPage();
    stack_->addWidget(login_page_);
    stack_->addWidget(chat_page_);
    stack_->setCurrentWidget(login_page_);
    setCentralWidget(stack_);

    status_label_ = new QLabel(this);
    statusBar()->addWidget(status_label_, 1);

    connect(login_page_, &LoginPage::loginRequested, this, &MainWindow::onLoginRequested);
    connect(login_page_, &LoginPage::registerRequested, this,
            &MainWindow::onRegisterRequested);
    connect(login_page_, &LoginPage::cancelRequested, this, [this] {
        logging_out_ = true;
        logged_in_ = false;
        if (network_ != nullptr) {
            network_->requestStop();
        }
        clearSession();
        login_page_->setStatus("已取消登录");
        setStatus("已取消登录");
    });
}

QWidget* MainWindow::buildChatPage() {
    auto* page = new QWidget(this);
    page->setObjectName("ChatPage");
    auto* page_layout = new QVBoxLayout(page);
    page_layout->setContentsMargins(0, 0, 0, 0);
    page_layout->setSpacing(0);
    auto* splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setObjectName("ChatSplitter");
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(0);
    page_layout->addWidget(splitter);

    auto* left_panel = new QWidget(page);
    left_panel->setObjectName("Sidebar");
    left_panel->setMinimumWidth(240);
    left_panel->setMaximumWidth(360);
    auto* left = new QVBoxLayout(left_panel);
    left->setContentsMargins(12, 12, 12, 12);
    left->setSpacing(6);

    search_users_button_ = new QPushButton("搜索用户", left_panel);
    search_users_button_->setObjectName("SidebarAction");
    search_users_button_->setIcon(
        tintedIcon(":/icons/search.png", QColor(theme::kMuted)));
    search_users_button_->setIconSize(QSize(17, 17));
    search_users_button_->setFixedHeight(32);
    left->addWidget(search_users_button_);

    sidebar_tabs_ = new QTabWidget(left_panel);
    sidebar_tabs_->setObjectName("SidebarTabs");

    auto* conversations_page = new QWidget(sidebar_tabs_);
    auto* conversations_layout = new QVBoxLayout(conversations_page);
    conversations_layout->setContentsMargins(0, 0, 0, 0);
    conversations_layout->setSpacing(6);

    // 好友申请属于好友体系，跟着「好友」标题走，而不是悬在群聊列表和好友
    // 列表之间独占一行。标题行右侧放图标按钮，未读数用角标提示。
    auto* friends_header_layout = new QHBoxLayout();
    friends_header_layout->setContentsMargins(0, 0, 0, 0);
    friends_header_layout->setSpacing(4);

    auto* conversations_label = new QLabel("好友", conversations_page);
    conversations_label->setObjectName("SectionLabel");
    conversations_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    friends_header_layout->addWidget(conversations_label, 1);

    friend_requests_badge_ = new QLabel(conversations_page);
    friend_requests_badge_->setObjectName("FriendRequestBadge");
    friend_requests_badge_->setAlignment(Qt::AlignCenter);
    friend_requests_badge_->setFixedSize(16, 16);
    friend_requests_badge_->hide();
    friends_header_layout->addWidget(friend_requests_badge_, 0, Qt::AlignVCenter);

    friend_requests_button_ = new QToolButton(conversations_page);
    friend_requests_button_->setObjectName("HeaderAction");
    friend_requests_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    friend_requests_button_->setText("好友申请");
    friend_requests_button_->setFixedHeight(24);
    friend_requests_button_->setAutoRaise(true);
    friend_requests_button_->setCursor(Qt::PointingHandCursor);
    friend_requests_button_->setToolTip("好友申请");
    friend_requests_button_->setAccessibleName("好友申请");
    friends_header_layout->addWidget(friend_requests_button_, 0, Qt::AlignVCenter);
    conversations_layout->addLayout(friends_header_layout);

    conversation_stack_ = new QStackedWidget(conversations_page);
    conversation_stack_->setObjectName("ConversationStack");
    conversation_list_ = new FriendListWidget(conversation_stack_);
    conversation_list_->setAccessibleName("会话列表");
    conversation_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    conversation_list_->setFocusPolicy(Qt::StrongFocus);
    conversation_list_->setIconSize(QSize(30, 30));
    conversation_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    conversation_list_->setItemDelegate(new FriendItemDelegate(conversation_list_));
    conversation_list_->setSpacing(0);
    friends_empty_state_ = new EmptyState(conversation_stack_);
    friends_empty_state_->setIcon(QIcon(":/icons/users.png"));
    friends_empty_state_->setTitle("还没有好友");
    friends_empty_state_->setDescription("点上方「搜索用户」，用昵称或 ID 找到对方");
    conversation_stack_->addWidget(conversation_list_);
    conversation_stack_->addWidget(friends_empty_state_);
    conversation_stack_->setCurrentWidget(friends_empty_state_);
    conversations_layout->addWidget(conversation_stack_, 1);
    sidebar_tabs_->addTab(conversations_page, "会话");

    auto* groups_page = new QWidget(sidebar_tabs_);
    auto* groups_layout = new QVBoxLayout(groups_page);
    groups_layout->setContentsMargins(0, 0, 0, 0);
    groups_layout->setSpacing(6);

    auto* notice_row = new QHBoxLayout();
    notice_row->setContentsMargins(0, 0, 0, 0);
    group_notice_button_ = new QToolButton(groups_page);
    group_notice_button_->setObjectName("GroupNoticeButton");
    group_notice_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    group_notice_button_->setText("群通知");
    group_notice_button_->setAutoRaise(true);
    group_notice_button_->setCursor(Qt::PointingHandCursor);
    notice_row->addWidget(group_notice_button_);
    group_notice_badge_ = new QLabel(groups_page);
    group_notice_badge_->setObjectName("GroupNoticeBadge");
    group_notice_badge_->setAlignment(Qt::AlignCenter);
    group_notice_badge_->setFixedSize(16, 16);
    group_notice_badge_->hide();
    notice_row->addWidget(group_notice_badge_, 0, Qt::AlignVCenter);
    notice_row->addStretch(1);
    groups_layout->addLayout(notice_row);

    group_list_ = new QListWidget(groups_page);
    group_list_->setObjectName("GroupList");
    group_list_->setAccessibleName("群组列表");
    group_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    group_list_->setFocusPolicy(Qt::StrongFocus);
    group_list_->setIconSize(QSize(30, 30));
    group_list_->setMinimumHeight(120);
    group_list_->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    groups_layout->addWidget(group_list_, 1);

    auto* group_actions = new QHBoxLayout();
    group_actions->setContentsMargins(0, 0, 0, 0);
    group_actions->setSpacing(6);
    create_group_button_ = new QPushButton("创建群", groups_page);
    create_group_button_->setObjectName("CreateGroupButton");
    create_group_button_->setFixedHeight(30);
    join_group_button_ = new QPushButton("加入群", groups_page);
    join_group_button_->setObjectName("JoinGroupButton");
    join_group_button_->setFixedHeight(30);
    group_actions->addWidget(create_group_button_);
    group_actions->addWidget(join_group_button_);
    groups_layout->addLayout(group_actions);
    sidebar_tabs_->addTab(groups_page, "群组");
    left->addWidget(sidebar_tabs_, 1);

    // The legacy ROOM_LIST path still exists for 0.2.5 compatibility, but the
    // new UI uses the group list above. Keep the widget hidden so old handlers
    // and selection clearing do not need a second code path.
    room_list_ = new QListWidget(left_panel);
    room_list_->setObjectName("LegacyRoomList");
    room_list_->hide();

    my_profile_button_ = new QPushButton(left_panel);
    my_profile_button_->setObjectName("MyProfileButton");
    my_profile_button_->setCursor(Qt::PointingHandCursor);
    my_profile_button_->setFixedHeight(52);
    auto* profile_layout = new QHBoxLayout(my_profile_button_);
    profile_layout->setContentsMargins(8, 2, 8, 2);
    profile_layout->setSpacing(10);
    my_profile_avatar_ = new QLabel(my_profile_button_);
    my_profile_avatar_->setFixedSize(32, 32);
    my_profile_avatar_->setAccessibleName("我的头像");
    my_profile_avatar_->setAccessibleDescription("我的头像");
    my_profile_avatar_->setAttribute(Qt::WA_TransparentForMouseEvents);
    profile_layout->addWidget(my_profile_avatar_);
    auto* profile_text_layout = new QVBoxLayout();
    profile_text_layout->setContentsMargins(0, 0, 0, 0);
    profile_text_layout->setSpacing(0);
    my_profile_name_ = new QLabel(my_profile_button_);
    my_profile_name_->setObjectName("MyProfileName");
    my_profile_name_->setAttribute(Qt::WA_TransparentForMouseEvents);
    my_profile_uid_ = new QLabel(my_profile_button_);
    my_profile_uid_->setObjectName("MyProfileUid");
    my_profile_uid_->setAttribute(Qt::WA_TransparentForMouseEvents);
    profile_text_layout->addWidget(my_profile_name_);
    profile_text_layout->addWidget(my_profile_uid_);
    profile_layout->addLayout(profile_text_layout, 1);
    // Without this the strip reads as a status line, not as the account menu
    // entry that now holds 切换账号 / 设置 / 退出登录.
    auto* account_menu_hint = new QLabel(QStringLiteral("⋯"), my_profile_button_);
    account_menu_hint->setObjectName("AccountMenuHint");
    account_menu_hint->setAttribute(Qt::WA_TransparentForMouseEvents);
    profile_layout->addWidget(account_menu_hint, 0, Qt::AlignVCenter);
    left->addWidget(my_profile_button_);
    updateOwnProfileStrip();
    splitter->addWidget(left_panel);

    auto* center_panel = new QWidget(page);
    center_panel->setObjectName("ChatPanel");
    auto* center = new QVBoxLayout(center_panel);
    center->setContentsMargins(0, 0, 0, 0);
    center->setSpacing(0);

    auto* header = new QWidget(center_panel);
    header->setObjectName("ChatHeader");
    auto* header_layout = new QVBoxLayout(header);
    header_layout->setContentsMargins(16, 10, 16, 8);
    header_layout->setSpacing(4);
    // 横幅行：左侧提示 + 右侧「重试连接」。
    // 离线模式下整行都要显示，重试按钮只在还留着 token 时才出现。
    banner_row_ = new QWidget(header);
    banner_row_->setObjectName("ConnectionBannerRow");
    auto* banner_layout = new QHBoxLayout(banner_row_);
    banner_layout->setContentsMargins(0, 0, 0, 0);
    banner_layout->setSpacing(8);
    connection_banner_ = new QLabel(banner_row_);
    connection_banner_->setWordWrap(true);
    banner_layout->addWidget(connection_banner_, 1);
    reconnect_button_ = new QPushButton("重试连接", banner_row_);
    reconnect_button_->setObjectName("ReconnectButton");
    reconnect_button_->setFixedHeight(28);
    reconnect_button_->hide();
    banner_layout->addWidget(reconnect_button_, 0, Qt::AlignVCenter);
    banner_row_->hide();
    header_layout->addWidget(banner_row_);
    connect(reconnect_button_, &QPushButton::clicked, this, &MainWindow::retryConnection);    auto* header_row = new QHBoxLayout();
    context_avatar_ = new QLabel(header);
    context_avatar_->setFixedSize(36, 36);
    context_avatar_->setAccessibleName("查看资料");
    context_avatar_->setAccessibleDescription("当前会话头像");
    context_avatar_->setPixmap(avatarIcon("C", 0).pixmap(32, 32));
    context_avatar_->setCursor(Qt::ArrowCursor);
    context_avatar_->setToolTip("查看资料");
    context_avatar_->installEventFilter(this);
    header_row->addWidget(context_avatar_);
    context_label_ = new QLabel("请选择一个会话", header);
    context_label_->setObjectName("ContextTitle");
    header_row->addWidget(context_label_, 1);
    more_button_ = new QToolButton(header);
    more_button_->setObjectName("MoreButton");
    more_button_->setAccessibleName("右侧栏");
    more_button_->setIcon(chatbox::client::moreDotsIcon(QColor(theme::kInk)));
    more_button_->setIconSize(QSize(20, 20));
    more_button_->setToolTip("打开/关闭右侧栏");
    more_button_->setFixedSize(32, 32);
    more_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    more_button_->setArrowType(Qt::NoArrow);
    more_button_->setPopupMode(QToolButton::DelayedPopup);
    connect(more_button_, &QToolButton::clicked, this, &MainWindow::toggleRightPanel);
    header_row->addWidget(more_button_);
    header_layout->addLayout(header_row);
    center->addWidget(header);

    chat_stack_ = new QStackedWidget(center_panel);
    chat_stack_->setObjectName("ChatStack");
    chat_view_ = new ChatView(chat_stack_);
    chat_view_->setReadOnly(true);
    chat_view_->setOpenExternalLinks(false);
    chat_view_->setOpenLinks(false);
    chat_empty_state_ = new EmptyState(chat_stack_);
    chat_empty_state_->setIcon(QIcon(":/icons/message.png"));
    chat_empty_state_->setTitle("选择一个会话开始聊天");
    chat_empty_state_->setDescription(
        "从左侧选择群聊或好友；按 Ctrl+Shift+F 搜索用户");
    chat_stack_->addWidget(chat_view_);
    chat_stack_->addWidget(chat_empty_state_);
    chat_stack_->setCurrentWidget(chat_empty_state_);
    center->addWidget(chat_stack_, 1);

    // Floating per-message actions; lives in the viewport so it can be
    // positioned in view coordinates and disappears as soon as the pointer
    // leaves the message or the view scrolls.
    message_action_bar_ = new MessageActionBar(chat_view_->viewport());
    message_action_bar_->hide();

    auto* composer = new QWidget(center_panel);
    composer->setObjectName("Composer");
    auto* composer_layout = new QVBoxLayout(composer);
    composer_layout->setContentsMargins(12, 8, 12, 10);
    composer_layout->setSpacing(6);

    reply_label_ = new QLabel(composer);
    reply_label_->setObjectName("MutedLabel");
    reply_label_->setWordWrap(true);
    reply_label_->hide();
    reply_cancel_button_ = new QPushButton("取消回复", composer);
    reply_cancel_button_->setAccessibleName("取消回复");
    reply_cancel_button_->hide();
    auto* reply_layout = new QHBoxLayout();
    reply_layout->setContentsMargins(0, 0, 0, 0);
    reply_layout->addWidget(reply_label_, 1);
    reply_layout->addWidget(reply_cancel_button_);
    composer_layout->addLayout(reply_layout);

    auto* input_layout = new QHBoxLayout();
    input_layout->setSpacing(6);
    input_edit_ = new MessageInput(composer);
    input_edit_->setObjectName("MessageInput");
    input_edit_->setAccessibleName("输入消息");
    input_edit_->setPlaceholderText("输入消息，回车发送，Shift + 回车换行");
    message_byte_counter_ = new QLabel(composer);
    message_byte_counter_->setObjectName("MessageByteCounter");
    message_byte_counter_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    message_byte_counter_->hide();
    mention_button_ = new QPushButton("@", composer);
    mention_button_->setObjectName("IconButton");
    mention_button_->setAccessibleName("提及成员");
    mention_button_->setFixedSize(38, 38);
    mention_button_->setToolTip("提及成员");
    send_button_ = new QPushButton(QIcon(":/icons/send.png"), "", composer);
    send_button_->setObjectName("SendButton");
    send_button_->setAccessibleName("发送消息");
    send_button_->setIconSize(QSize(18, 18));
    send_button_->setFixedSize(38, 38);
    send_button_->setToolTip("发送");
    input_layout->addWidget(input_edit_, 1);
    input_layout->addWidget(mention_button_);
    input_layout->addWidget(send_button_);
    composer_layout->addLayout(input_layout);
    composer_layout->addWidget(message_byte_counter_);
    center->addWidget(composer);

    right_panel_ = new QWidget(page);
    right_panel_->setObjectName("RightSidebar");
    right_panel_->setMinimumWidth(200);
    right_panel_->setMaximumWidth(300);
    auto* right = new QVBoxLayout(right_panel_);
    right->setContentsMargins(12, 12, 12, 12);
    right->setSpacing(6);

    auto* right_header_row = new QHBoxLayout();
    member_count_label_ = new QLabel("成员", right_panel_);
    member_count_label_->setObjectName("RightHeader");
    right_header_row->addWidget(member_count_label_, 1);
    right_settings_button_ = new QToolButton(right_panel_);
    right_settings_button_->setObjectName("RightSettingsButton");
    right_settings_button_->setText("⚙");
    right_settings_button_->setToolTip("群设置");
    right_settings_button_->setAutoRaise(true);
    right_settings_button_->hide();
    right_header_row->addWidget(right_settings_button_, 0, Qt::AlignVCenter);
    right->addLayout(right_header_row);

    right_mode_stack_ = new QStackedWidget(right_panel_);
    right_mode_stack_->setObjectName("RightModeStack");

    right_group_page_ = new QWidget(right_mode_stack_);
    auto* group_layout = new QVBoxLayout(right_group_page_);
    group_layout->setContentsMargins(0, 0, 0, 0);
    group_layout->setSpacing(6);
    right_group_search_ = new QLineEdit(right_group_page_);
    right_group_search_->setObjectName("RightGroupSearch");
    right_group_search_->setPlaceholderText("搜索消息");
    group_layout->addWidget(right_group_search_);
    member_list_ = new QListWidget(right_group_page_);
    member_list_->setObjectName("RightMemberList");
    member_list_->setAccessibleName("群成员");
    member_list_->setIconSize(QSize(28, 28));
    member_list_->setContextMenuPolicy(Qt::CustomContextMenu);
    group_layout->addWidget(member_list_, 1);
    auto* group_action_row = new QHBoxLayout();
    group_action_row->setContentsMargins(0, 0, 0, 0);
    group_action_row->setSpacing(6);
    right_invite_button_ = new QPushButton("邀请成员", right_group_page_);
    right_invite_button_->setObjectName("RightInviteButton");
    right_invite_button_->setFixedHeight(30);
    right_leave_button_ = new QPushButton("退出群", right_group_page_);
    right_leave_button_->setObjectName("RightLeaveButton");
    right_leave_button_->setFixedHeight(30);
    group_action_row->addWidget(right_invite_button_);
    group_action_row->addWidget(right_leave_button_);
    group_layout->addLayout(group_action_row);
    right_group_file_button_ = new QPushButton("文件功能将在下一阶段开放", right_group_page_);
    right_group_file_button_->setObjectName("RightFileButton");
    right_group_file_button_->setEnabled(false);
    group_layout->addWidget(right_group_file_button_);

    right_private_page_ = new QWidget(right_mode_stack_);
    auto* private_layout = new QVBoxLayout(right_private_page_);
    private_layout->setContentsMargins(0, 0, 0, 0);
    private_layout->setSpacing(8);
    auto* profile_card = new QWidget(right_private_page_);
    profile_card->setObjectName("RightProfileCard");
    auto* profile_card_layout = new QVBoxLayout(profile_card);
    profile_card_layout->setContentsMargins(8, 8, 8, 8);
    profile_card_layout->setSpacing(4);
    right_profile_avatar_ = new QLabel(profile_card);
    right_profile_avatar_->setFixedSize(56, 56);
    right_profile_avatar_->setAlignment(Qt::AlignCenter);
    profile_card_layout->addWidget(right_profile_avatar_, 0, Qt::AlignHCenter);
    right_profile_name_ = new QLabel(profile_card);
    right_profile_name_->setObjectName("RightProfileName");
    right_profile_name_->setAlignment(Qt::AlignCenter);
    profile_card_layout->addWidget(right_profile_name_);
    right_profile_id_ = new QLabel(profile_card);
    right_profile_id_->setObjectName("RightProfileId");
    right_profile_id_->setAlignment(Qt::AlignCenter);
    right_profile_id_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    profile_card_layout->addWidget(right_profile_id_);
    right_profile_signature_ = new QLabel(profile_card);
    right_profile_signature_->setObjectName("RightProfileSignature");
    right_profile_signature_->setWordWrap(true);
    right_profile_signature_->setAlignment(Qt::AlignCenter);
    profile_card_layout->addWidget(right_profile_signature_);
    private_layout->addWidget(profile_card);
    right_profile_button_ = new QPushButton("查看资料", right_private_page_);
    right_profile_button_->setObjectName("RightProfileButton");
    private_layout->addWidget(right_profile_button_);
    right_private_file_button_ =
        new QPushButton("文件功能将在下一阶段开放", right_private_page_);
    right_private_file_button_->setObjectName("RightFileButton");
    right_private_file_button_->setEnabled(false);
    private_layout->addWidget(right_private_file_button_);
    private_layout->addStretch(1);
    right_private_search_ = new QLineEdit(right_private_page_);
    right_private_search_->setObjectName("RightPrivateSearch");
    right_private_search_->setPlaceholderText("搜索消息");
    private_layout->addWidget(right_private_search_);

    right_search_page_ = new QWidget(right_mode_stack_);
    right_search_page_->setObjectName("RightSearchPage");
    auto* search_layout = new QVBoxLayout(right_search_page_);
    search_layout->setContentsMargins(0, 0, 0, 0);
    search_layout->setSpacing(6);
    auto* search_header = new QHBoxLayout();
    right_search_input_ = new QLineEdit(right_search_page_);
    right_search_input_->setObjectName("RightSearchInput");
    right_search_input_->setPlaceholderText("搜索消息");
    auto* back = new QPushButton("返回", right_search_page_);
    back->setObjectName("RightSearchBack");
    search_header->addWidget(right_search_input_, 1);
    search_header->addWidget(back);
    search_layout->addLayout(search_header);
    right_search_results_ = new QListWidget(right_search_page_);
    right_search_results_->setObjectName("RightSearchResults");
    search_layout->addWidget(right_search_results_, 1);
    right_search_status_ = new QLabel(right_search_page_);
    right_search_status_->setObjectName("RightSearchStatus");
    search_layout->addWidget(right_search_status_);
    connect(back, &QPushButton::clicked, this, [this] {
        panel_search_seq_ = 0;
        updateRightPanelVisibility();
    });
    connect(right_search_input_, &QLineEdit::returnPressed, this,
            [this] { startPanelSearch(right_search_input_->text()); });
    connect(right_search_results_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) {
                if (item == nullptr) {
                    return;
                }
                jumpToMessage(item->data(Qt::UserRole).toULongLong());
                updateRightPanelVisibility();
            });

    right_mode_stack_->addWidget(right_group_page_);
    right_mode_stack_->addWidget(right_private_page_);
    right_mode_stack_->addWidget(right_search_page_);
    right->addWidget(right_mode_stack_, 1);
    right_panel_->hide();
    splitter->addWidget(center_panel);
    splitter->addWidget(right_panel_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);

    connect(right_group_search_, &QLineEdit::returnPressed, this,
            [this] { startPanelSearch(right_group_search_->text()); });
    connect(right_private_search_, &QLineEdit::returnPressed, this,
            [this] { startPanelSearch(right_private_search_->text()); });
    connect(right_settings_button_, &QToolButton::clicked, this,
            &MainWindow::openGroupSettings);
    connect(right_profile_button_, &QPushButton::clicked, this, [this] {
        if (current_peer_id_ != 0) {
            openProfile(current_peer_id_, current_peer_name_);
        }
    });
    connect(right_invite_button_, &QPushButton::clicked, this, [this] {
        openGroupInviteDialog(static_cast<quint64>(current_room_id_));
    });
    connect(right_leave_button_, &QPushButton::clicked, this, [this] {
        leaveGroup(static_cast<quint64>(current_room_id_));
    });

    connect(room_list_, &QListWidget::itemClicked, this, &MainWindow::onRoomClicked);
    connect(room_list_, &QListWidget::itemActivated, this, &MainWindow::onRoomClicked);
    connect(group_list_, &QListWidget::itemClicked, this, &MainWindow::onGroupClicked);
    connect(group_list_, &QListWidget::itemActivated, this,
            &MainWindow::onGroupClicked);
    connect(group_notice_button_, &QToolButton::clicked, this,
            &MainWindow::openGroupNotifications);
    connect(create_group_button_, &QPushButton::clicked, this,
            &MainWindow::openCreateGroupDialog);
    connect(join_group_button_, &QPushButton::clicked, this,
            &MainWindow::openJoinGroupDialog);
    connect(conversation_list_, &QListWidget::itemClicked, this,
            &MainWindow::onConversationClicked);
    connect(conversation_list_, &FriendListWidget::avatarClicked, this,
            &MainWindow::onFriendAvatarClicked);
    connect(conversation_list_, &QListWidget::itemActivated, this,
            &MainWindow::onConversationClicked);
    connect(conversation_list_, &QListWidget::customContextMenuRequested, this,
            &MainWindow::onFriendContextMenu);
    connect(my_profile_button_, &QPushButton::clicked, this,
            &MainWindow::openAccountMenu);
    connect(friend_requests_button_, &QToolButton::clicked, this,
            &MainWindow::openFriendRequests);
    connect(member_list_, &QListWidget::itemDoubleClicked, this,
            &MainWindow::onMemberDoubleClicked);
    connect(member_list_, &QListWidget::customContextMenuRequested, this,
            &MainWindow::onMemberContextMenu);
    connect(search_users_button_, &QPushButton::clicked, this, &MainWindow::openUserSearch);
    connect(send_button_, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(input_edit_, &MessageInput::submitRequested, this, &MainWindow::onSendClicked);
    connect(input_edit_, &MessageInput::textChanged, this,
            &MainWindow::updateMessageByteCounter);
    connect(mention_button_, &QPushButton::clicked, this, &MainWindow::insertMention);
    connect(reply_cancel_button_, &QPushButton::clicked, this, &MainWindow::clearReply);

    QWidget::setTabOrder(room_list_, conversation_list_);
    QWidget::setTabOrder(conversation_list_, chat_view_);
    QWidget::setTabOrder(chat_view_, input_edit_);
    QWidget::setTabOrder(input_edit_, mention_button_);
    QWidget::setTabOrder(mention_button_, send_button_);
    QWidget::setTabOrder(send_button_, member_list_);

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_ = new QSystemTrayIcon(this);
        tray_->setIcon(QIcon(QStringLiteral(":/app.ico")));
        tray_->setToolTip("ChatBox");
        auto* tray_menu = new QMenu(this);
        tray_menu->addAction("打开 ChatBox", this, [this] {
            showNormal();
            raise();
            activateWindow();
        });
        tray_menu->addAction("退出", this, [this] { close(); });
        tray_->setContextMenu(tray_menu);
        connect(tray_, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger ||
                        reason == QSystemTrayIcon::DoubleClick) {
                        showNormal();
                        raise();
                        activateWindow();
                    }
                });
        tray_->show();
    }

    return page;
}




void MainWindow::finishLogout() {
    if (logout_finishing_) {
        return;
    }
    logout_finishing_ = true;
    logged_in_ = false;
    if (network_ != nullptr) {
        network_->requestStop();
    }
    clearSession();
    login_page_->resetToLogin();
    login_page_->setStatus("已退出登录");
    setStatus("已退出登录");
}

void MainWindow::refreshThemeDependentIcons() {
    // Reloading the stylesheet repaints everything QSS drives, but icons that
    // baked the old colour into a QPixmap keep the stale pixels. Every such
    // asset belongs in this function so a new one cannot be forgotten inside
    // an appearance-changed lambda.
    if (more_button_ != nullptr) {
        more_button_->setIcon(chatbox::client::moreDotsIcon(QColor(theme::kInk)));
    }
    // Sidebar glyphs are tinted pixmaps, so they need the same treatment.
    if (search_users_button_ != nullptr) {
        search_users_button_->setIcon(
            tintedIcon(":/icons/search.png", QColor(theme::kMuted)));
    }
}

void MainWindow::openSettings() {
    SettingsDialog dialog(chatbox::client::publicUserId(user_id_),
                          history_store_.directory(),
                          history_store_.sizeBytes(),
                          history_store_.isOpen(), this);
    connect(&dialog, &SettingsDialog::appearanceChanged, this, [this] {
        refreshThemeDependentIcons();
        if (chat_view_ != nullptr) {
            renderCurrentConversation(ScrollMode::kPreserve);
        }
    });
    connect(&dialog, &SettingsDialog::logoutRequested, this, &MainWindow::logout,
            Qt::QueuedConnection);
    connect(&dialog, &SettingsDialog::settingsSaved, this,
            [this](bool notifications, bool system_popup, bool autostart) {
                QSettings settings;
                settings.setValue("settings/notifications", notifications);
                settings.setValue("settings/system_popup", system_popup);
                const bool autostart_applied = chatbox::client::setAutostartEnabled(
                    autostart, QCoreApplication::applicationFilePath());
                // Persist the state the registry actually ended up in: a failed
                // write must not leave the checkbox claiming the opposite.
                settings.setValue("settings/autostart",
                                  autostart_applied ? autostart : !autostart);
                settings.sync();
                notifications_enabled_ = notifications;
                system_popup_enabled_ = system_popup;
                // Badges and the title counter follow the master switch.
                refreshFriendListView();
                setStatus("设置已保存");
                if (!autostart_applied) {
                    setStatus("开机自启动设置失败（其余设置已保存）", true);
                    qWarning("autostart update failed requested=%d",
                             autostart ? 1 : 0);
                }
                qInfo("settings saved notifications=%d system_popup=%d autostart=%d applied=%d",
                      notifications ? 1 : 0, system_popup ? 1 : 0,
                      autostart ? 1 : 0, autostart_applied ? 1 : 0);
            });
    // 换目录：先确认目标目录可写，再让历史库搬过去；
    // 任何一步失败都只提示错误，不写 settings/history/path。
    connect(&dialog, &SettingsDialog::historyPathChangeRequested, this,
            [this, &dialog](const QString& directory, bool migrate) {
                QString error;
                if (!chatbox::client::HistoryStore::ensureWritableDirectory(directory,
                                                                            &error)) {
                    QMessageBox::warning(&dialog, "本地聊天记录", error);
                    return;
                }
                if (!history_store_.moveTo(directory, migrate, &error)) {
                    QMessageBox::warning(&dialog, "本地聊天记录", error);
                    return;
                }
                QSettings settings;
                settings.setValue("settings/history/path", history_store_.directory());
                settings.sync();
                dialog.setHistoryInfo(history_store_.directory(),
                                      history_store_.sizeBytes(),
                                      history_store_.isOpen());
                if (offline_mode_) {
                    // 换了库：离线视图要按新库重建。
                    // 先重建再写状态：重建过程里的提示不能盖掉本次操作结果。
                    refreshOfflineAfterClear();
                }
                setStatus(migrate ? "本地聊天记录已迁移到新目录"
                                  : "本地聊天记录已切换到新目录");
                // 日志只记动作与目录，不记任何消息正文。
                qInfo("history directory changed migrate=%d dir=%s",
                      migrate ? 1 : 0, qPrintable(history_store_.directory()));
            });
    // 清空本地副本：只删本机数据，服务器历史不受影响。
    connect(&dialog, &SettingsDialog::historyClearAllRequested, this,
            [this, &dialog]() {
                if (!history_store_.clearAll()) {
                    QMessageBox::warning(&dialog, "本地聊天记录",
                                         "清空本地聊天记录失败，请稍后重试");
                    return;
                }
                dialog.setHistoryInfo(history_store_.directory(),
                                      history_store_.sizeBytes(),
                                      history_store_.isOpen());
                if (offline_mode_) {
                    // 先重建视图再写状态：状态提示由本次操作给出。
                    refreshOfflineAfterClear();
                }
                setStatus("本地聊天记录已清空");
                qInfo("local history cleared");
            });
    // 清理 N 天前的本地副本：同样只影响本机数据。
    connect(&dialog, &SettingsDialog::historyClearOlderRequested, this,
            [this, &dialog](int days) {
                if (!history_store_.clearOlderThan(days)) {
                    QMessageBox::warning(&dialog, "本地聊天记录",
                                         "清理本地聊天记录失败，请稍后重试");
                    return;
                }
                dialog.setHistoryInfo(history_store_.directory(),
                                      history_store_.sizeBytes(),
                                      history_store_.isOpen());
                if (offline_mode_) {
                    // 先重建视图再写状态：状态提示由本次操作给出。
                    refreshOfflineAfterClear();
                }
                setStatus(QString("已清理 %1 天前的本地聊天记录").arg(days));
                qInfo("local history pruned days=%d", days);
            });
    dialog.exec();
}








































































































void MainWindow::setStatus(const QString& text, bool error) {
    if (status_label_ == nullptr) {
        return;
    }
    status_label_->setText(text);
    status_label_->setStyleSheet(error
                                     ? QString("color: %1;").arg(theme::kSemanticDown)
                                     : QString("color: %1;").arg(theme::kBody));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == context_avatar_ && event->type() == QEvent::MouseButtonRelease) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton &&
            context_type_ == ContextType::Private && current_peer_id_ != 0) {
            openProfile(current_peer_id_, current_peer_name_);
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    shutting_down_ = true;
    if (network_ != nullptr) {
        network_->requestStop();
        network_->wait();
    }
    event->accept();
}
