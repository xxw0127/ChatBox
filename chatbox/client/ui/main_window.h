#ifndef CHATBOX_CLIENT_UI_MAIN_WINDOW_H
#define CHATBOX_CLIENT_UI_MAIN_WINDOW_H

#include <QByteArray>
#include <QHash>
#include <QIcon>
#include <QMainWindow>
#include <QSet>
#include <QString>
#include <QStringList>

#include "common/protocol.h"
#include "core/avatar_cache.h"
#include "core/chat_message.h"
#include "core/contact_store.h"
#include "core/friend_view.h"
#include "core/history_store.h"
#include "core/message_renderer.h"
#include "core/message_store.h"
#include "core/outbox.h"
#include "core/presence_book.h"
#include "core/update_manifest.h"
#include "core/user_profile.h"

class LoginPage;
class FriendListWidget;
class NetworkThread;
class ProfileDialog;
class ChatView;
class EmptyState;
class MessageInput;
class QAction;
class QCloseEvent;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPoint;
class QPushButton;
class QRect;
class QStackedWidget;
class QSystemTrayIcon;
class QTextBrowser;
class QTimer;
class QToolButton;
class FriendRequestDialog;
class MentionPopup;
class MessageSearchDialog;
class SettingsDialog;
class UpdateDialog;
class UserSearchDialog;

namespace chatbox {
namespace client {
class UpdateService;
}  // namespace client
}  // namespace chatbox

// Image resources for the senders in one rendered page. srcs feeds the
// renderer's <img> URLs; images feeds QTextDocument::addResource.
struct MessageAvatars {
    chatbox::client::AvatarSrcMap srcs;
    QHash<QString, QImage> images;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onLoginRequested(const QString& host, quint16 port, const QString& username,
                          const QString& password, bool tls);
    void onRegisterRequested(const QString& host, quint16 port, const QString& username,
                             const QString& password, const QString& nickname, bool tls);
    void onConnected();
    void onDisconnected(const QString& reason);
    void onFrameReceived(const QByteArray& data);
    void onSendClicked();
    void onRoomClicked(QListWidgetItem* item);
    void onConversationClicked(QListWidgetItem* item);
    void selectAdjacentConversation(int delta);
    void onFriendAvatarClicked(QListWidgetItem* item);
    void openOwnProfile();
    void onProfileSaveRequested(const QString& nickname,
                                const QString& signature, int avatar_action,
                                const QByteArray& avatar_jpeg);
    void onMemberDoubleClicked(QListWidgetItem* item);
    void onContactActivated(QListWidgetItem* item);
    void checkForUpdates();
    void showUpdateAvailable(const chatbox::client::UpdateManifest& manifest);
    void onUpdateRequested();
    void onUpdateProgress(qint64 received, qint64 total);
    void onUpdateDownloaded(const QString& zip_path);
    void onUpdateFailed(const QString& reason);
    void onUpdateCancelled();
    void onUpdateDialogRejected();
    void installDownloadedUpdate();
    void onContactContextMenu(const QPoint& position);
    void onFriendContextMenu(const QPoint& position);
    void onMemberContextMenu(const QPoint& position);
    void onLoadMoreClicked();
    // 离线模式下的「重试连接」：只有还留着 token 时才真的重连。
    void retryConnection();
private:
    enum class PendingAuth { None, Login, Register };
    enum class ContextType { None, Room, Private };
    enum class ScrollMode { kBottom, kTop, kPreserve };

    void buildUi();
    // Single entry point for colours baked into QPixmap/QIcon. Called on every
    // appearance change; add new rasterised assets here, not one-off lambdas.
    void refreshThemeDependentIcons();
    QWidget* buildChatPage();
    void startConnection();
    bool sendFrame(const chatbox::Frame& frame);
    void sendMessage(const chatbox::client::ChatMessage& message);
    void retryMessage(const QString& client_msg_id);
    void onSendTimeout();
    void onAuthTimeout();
    void sendLogin();
    void sendRegister();
    void sendResume();
    void requestRoomList();
    void requestConversations();
    void requestFriendList();
    void requestFriendRequests();
    void sendFriendRequest(quint64 user_id);
    void sendFriendResponse(quint64 requester_id, bool accept);
    void sendFriendRemove(quint64 friend_id);
    void openFriendRequests();
    void updateFriendRequestCount();
    void requestUserSearch(const QString& keyword);
    void openUserSearch();
    void openMessageSearch();
    void requestMessageSearch(const QString& keyword);
    void refreshContacts();
    void requestProfile(quint64 user_id);
    void requestAvatar(quint64 user_id, quint32 version);
    bool isCurrentProfileRequest(quint64 user_id, quint32 seq) const;
    bool isCurrentAvatarRequest(quint64 user_id, quint32 seq) const;
    void clearPendingProfileRequest(quint64 user_id, quint32 seq);
    void clearPendingAvatarRequest(quint64 user_id, quint32 seq);
    void openProfile(quint64 user_id, const QString& fallback_name = QString());
    void onProfileAvatarReceived(quint64 user_id, quint32 version, const QImage& image);
    void onProfileDataReceived(const chatbox::client::UserProfile& profile);
    void requestPeerAvatar(quint64 peer_id);
    void updatePrivateHeaderAvatar(quint64 peer_id, const QString& name);
    // Member list row icon: cached avatar when one is available, letter
    // fallback otherwise.
    QIcon memberAvatarIcon(quint64 member_id, const QString& nickname, int size);
    // Repaints the member list row of user_id; no-op when the row is absent.
    void refreshMemberListAvatar(quint64 user_id);
    void closeProfileDialog();
    void updateOwnProfileStrip();
    void applyOwnProfileUpdate(const QString& nickname, const QString& signature,
                               quint32 avatar_version);
    void applyProfileUpdatePush(quint64 user_id, const QString& nickname,
                                quint32 avatar_version);
    void updateRoomMemberNickname(quint64 user_id, const QString& nickname);
    void insertMention();
    void openMentionPopup(const QString& query);
    void closeMentionPopup();
    void updateMentionPopup();
    void insertMentionChoice(quint64 user_id, const QString& name);
    void setReplyTo(const chatbox::client::ChatMessage& message);
    void clearReply();
    void sendRecall(quint64 message_id);
    void showMessageMenu(quint64 message_id, const QString& client_msg_id,
                         const QPoint& global_pos);
    void jumpToMessage(quint64 message_id);
    void exitJumpMode(bool reload_latest);
    void positionJumpLatestButton();
    void copyMessageText(const chatbox::client::ChatMessage& message);
    void requestMembers(int room_id);
    void requestHistory(int kind, quint64 target, quint64 before_id);
    void selectRoom(int room_id, const QString& name);
    void selectPrivate(quint64 peer_id, const QString& nickname);
    // 房间列表与会话列表是两个独立控件，各自记着自己的选中项。切场景时
    // 必须清掉另一个，否则两行会同时是蓝色选中态。
    void clearStaleListSelection(ContextType keep);
    // 打开本地历史库（按账号隔离）；失败只记日志，聊天照常。
    bool openHistoryStore(quint64 user_id);
    // 关闭本地历史库：先解绑 MessageStore，再关连接。
    void closeHistoryStore();
    // 切换会话时先用本地历史首屏渲染，在线历史回来后覆盖。
    void loadInitialHistory(chatbox::client::ChatContext context, quint64 target_id);
    // 离线模式：只读地显示本机保存的聊天记录。
    bool enterOfflineMode(bool with_token);
    void leaveOfflineMode();
    // 离线侧栏：把本地存下来的所有会话都列出来（不做好友过滤）。
    void populateOfflineSidebar();
    // 清理/切换本地记录后重建离线视图：重置当前会话并按本地库重载侧栏。
    void refreshOfflineAfterClear();
    // 离线翻页：从本地历史库取更早的一页。
    void loadOlderFromLocal();
    void updateChatEmptyState();
    void updateMessageByteCounter();
    void resetContext();
    void renderCurrentConversation(ScrollMode mode);
    MessageAvatars messageAvatars(
        const QVector<chatbox::client::ChatMessage>& messages) const;
    void requestMissingMessageAvatars(
        const QVector<chatbox::client::ChatMessage>& messages);
    void addIncomingMessage(const chatbox::client::ChatMessage& message);
    bool currentChatContext(chatbox::client::ChatContext& context,
                            quint64& target_id) const;
    void restorePendingForCurrentContext();
    void setOnline(bool online, const QString& text = QString());
    void applyPresenceUpdate(quint64 user_id, bool online, quint64 seq);
    void handleError(quint16 code, const QString& message, quint32 seq);
    void clearSession();
    void scheduleReconnect();
    void addConversation(quint64 peer_id, const QString& nickname);
    bool currentContextMatches(int kind, quint64 target) const;
    void setStatus(const QString& text, bool error = false);
    void saveSession();
    void loadSession();
    void logout();
    void finishLogout();
    void openSettings();
    // 左下角账号入口：资料 / 切换账号 / 设置 / 退出登录
    void openAccountMenu();
    void switchToAccount(const QString& username);
    // 把当前账号的昵称、ID 与头像写进本地记录，供切换列表展示。
    void rememberCurrentAccount();
    // 成员面板只在房间里出现，私聊与未选会话时整列收起。
    void updateRightPanelVisibility();
    // 成员面板默认关闭，开关状态记在 QSettings 里。
    bool member_panel_visible_ = false;
    void sendMarkRead(quint64 peer_id, quint64 message_id);
    QListWidgetItem* findConversationItem(quint64 peer_id) const;
    void updateFriendView(quint64 peer_id, int unread, const QString& nickname,
                          const QString& last_content, const QString& last_time,
                          bool online);
    void updateConversationItem(quint64 peer_id, int unread, const QString& nickname,
                                const QString& last_content, const QString& last_time,
                                bool online);
    void refreshFriendListView();
    void setFriendRemark(quint64 peer_id);
    void refreshUnreadTitle();
    void showPrivateNotification(const QString& nickname, const QString& content);
    void showMentionNotification(const QString& nickname, const QString& content);

    QStackedWidget* stack_ = nullptr;
    LoginPage* login_page_ = nullptr;
    QWidget* chat_page_ = nullptr;
    QListWidget* room_list_ = nullptr;
    FriendListWidget* conversation_list_ = nullptr;
    QStackedWidget* conversation_stack_ = nullptr;
    EmptyState* friends_empty_state_ = nullptr;
    QListWidget* contact_list_ = nullptr;
    QListWidget* member_list_ = nullptr;
    QLabel* context_label_ = nullptr;
    QLabel* context_avatar_ = nullptr;
    QToolButton* more_button_ = nullptr;
    QMenu* more_menu_ = nullptr;
    QAction* load_more_action_ = nullptr;
    QAction* show_members_action_ = nullptr;
    QLabel* connection_banner_ = nullptr;
    QWidget* banner_row_ = nullptr;
    QPushButton* reconnect_button_ = nullptr;
    QPushButton* search_users_button_ = nullptr;
    QLabel* reply_label_ = nullptr;
    QPushButton* reply_cancel_button_ = nullptr;
    ChatView* chat_view_ = nullptr;
    QPushButton* jump_latest_button_ = nullptr;
    bool jump_mode_ = false;
    quint64 pending_jump_id_ = 0;
    quint64 jump_target_id_ = 0;
    int jump_kind_ = 0;  // 1 room, 2 private
    QStackedWidget* chat_stack_ = nullptr;
    EmptyState* chat_empty_state_ = nullptr;
    chatbox::client::UpdateService* update_service_ = nullptr;
    UpdateDialog* update_dialog_ = nullptr;
    chatbox::client::UpdateManifest pending_update_;
    QString downloaded_update_zip_;
    bool update_check_started_ = false;
    bool update_download_active_ = false;
    MessageInput* input_edit_ = nullptr;
    QLabel* message_byte_counter_ = nullptr;
    QPushButton* mention_button_ = nullptr;
    MentionPopup* mention_popup_ = nullptr;
    int mention_token_start_ = -1;
    QPushButton* send_button_ = nullptr;
    QWidget* right_panel_ = nullptr;
    QLabel* member_count_label_ = nullptr;
    QLabel* friend_requests_badge_ = nullptr;
    QLabel* status_label_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    UserSearchDialog* user_search_dialog_ = nullptr;
    FriendRequestDialog* friend_requests_dialog_ = nullptr;
    ProfileDialog* profile_dialog_ = nullptr;
    quint64 profile_dialog_user_id_ = 0;
    QString profile_dialog_fallback_name_;
    chatbox::client::UserProfile profile_dialog_profile_;
    QToolButton* friend_requests_button_ = nullptr;
    QPushButton* my_profile_button_ = nullptr;
    QLabel* my_profile_avatar_ = nullptr;
    QLabel* my_profile_name_ = nullptr;
    QLabel* my_profile_uid_ = nullptr;
    int friend_request_count_ = 0;
    MessageSearchDialog* message_search_dialog_ = nullptr;
    chatbox::client::ContactStore contact_store_;
    QTimer* send_timeout_timer_ = nullptr;
    QTimer* auth_timeout_timer_ = nullptr;
    QTimer* reconnect_timer_ = nullptr;
    QTimer* search_timeout_timer_ = nullptr;
    QTimer* message_search_timeout_timer_ = nullptr;
    QTimer* profile_timeout_timer_ = nullptr;
    QTimer* avatar_timeout_timer_ = nullptr;
    quint32 user_search_seq_ = 0;
    quint32 message_search_seq_ = 0;
    quint64 reply_to_id_ = 0;
    QString reply_sender_name_;
    QString reply_content_;

    NetworkThread* network_ = nullptr;
    PendingAuth pending_auth_ = PendingAuth::None;
    bool resuming_ = false;
    ContextType context_type_ = ContextType::None;
    QString host_;
    quint16 port_ = 9100;
    bool use_tls_ = true;
    QString username_;
    QString password_;
    QString nickname_;
    QString token_;
    quint64 user_id_ = 0;
    int current_room_id_ = 0;
    quint64 current_peer_id_ = 0;
    QString current_peer_name_;
    chatbox::client::MessageStore message_store_;
    chatbox::client::PresenceBook presence_book_;
    chatbox::client::HistoryStore history_store_;
    chatbox::client::Outbox outbox_;
    QHash<quint32, QString> pending_sequences_;
    quint64 next_before_id_ = 0;
    bool has_more_ = false;
    bool loading_older_history_ = false;
    bool connected_ = false;
    bool online_ = false;
    // 离线模式：只读地显示本地记录，不发任何聊天/好友/资料请求。
    bool offline_mode_ = false;
    // 离线模式是否还带着 token（带 token 才能自动重连并显示「重试连接」）。
    bool offline_session_ = false;
    bool notifications_enabled_ = true;
    bool system_popup_enabled_ = true;
    bool logged_in_ = false;
    bool shutting_down_ = false;
    bool logging_out_ = false;
    bool logout_finishing_ = false;
    bool reconnect_scheduled_ = false;
    int reconnect_attempt_ = 0;
    quint64 connection_generation_ = 0;
    quint32 next_seq_ = 1;
    QHash<quint64, QString> peer_names_;
    QHash<quint64, chatbox::client::FriendView> friend_views_;
    QSet<quint64> friend_ids_;
    chatbox::client::AvatarCache avatar_cache_;
    QSet<quint64> pending_avatar_requests_;
    QSet<quint64> pending_profile_requests_;
    QHash<quint32, quint64> profile_request_seqs_;
    QHash<quint32, quint64> avatar_request_seqs_;
    QHash<quint64, quint32> pending_profile_seqs_;
    QHash<quint64, quint32> pending_avatar_seqs_;
    QHash<quint64, quint32> pending_avatar_versions_;
    QHash<quint32, quint64> friend_request_seqs_;
    quint32 own_avatar_version_ = 0;
    quint32 pending_profile_update_seq_ = 0;
};

#endif  // CHATBOX_CLIENT_UI_MAIN_WINDOW_H
