#ifndef CHATBOX_CLIENTCORE_SERVICE_CHAT_SERVICE_H
#define CHATBOX_CLIENTCORE_SERVICE_CHAT_SERVICE_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "common/protocol.h"
#include "secure/secure_store.h"
#include "session/session_client.h"
#include "store/mobile_store.h"

class QTimer;

namespace chatbox {
namespace clientcore {

class Transport;

struct ServiceConfig {
    SessionConfig session;
    QString db_path;
    SecureStore* secure_store = nullptr;
};

// Service-side orchestrator: owns the transport lifecycle, authentication,
// conversation sync, the local store, the outbox, and push persistence. The UI
// never touches the transport directly.
class ChatService : public QObject {
    Q_OBJECT

public:
    explicit ChatService(Transport* transport = nullptr,
                         QObject* parent = nullptr);
    ~ChatService() override;

    bool start(const ServiceConfig& config, QString* error);
    void stop();

    void login(const QString& username, const QString& password);
    void logout();
    void sendText(int kind, quint64 target_id, const QString& content);
    void markRead(int kind, quint64 target_id, quint64 message_id);
    void setActiveConversation(int kind, quint64 target_id);
    void requestSessionList();
    void revokeSession(const QString& session_id);
    QJsonObject snapshot() const;

signals:
    void stateChanged(const QString& state, const QString& detail);
    void snapshotChanged(const QJsonObject& snapshot);
    void eventReceived(const QJsonObject& event);
    void sessionListReceived(const QVector<chatbox::SessionInfo>& sessions);
    void sessionRevoked(const QString& session_id);

private:
    struct PendingSend {
        QString client_msg_id;
        int kind = 0;
        quint64 target_id = 0;
        QString content;
        QString created_at;
    };

    void setState(const QString& state, const QString& detail = QString());
    void emitSnapshot();
    void emitEvent(QJsonObject event);

    void onTransportConnected();
    void onTransportDisconnected(const QString& reason);
    void onLoginSucceeded(const LoginResult& result);
    void onLoginFailed(int code, const QString& message);
    void onFrame(const QByteArray& raw);
    void onRequestFailed(int code, const QString& message);

    void requestInitialLists();
    void beginSync();
    void finishSync();
    void scheduleReconnect();
    void handleTerminal(int code, const QString& message);
    void flushOutbox();

    void handleErrorFrame(const chatbox::Frame& frame);
    void handlePrivatePush(const chatbox::Frame& frame);
    void handleChatPush(const chatbox::Frame& frame);
    void handleReadState(const chatbox::Frame& frame);
    void handleEcho(const chatbox::Frame& frame);
    void handleSendResponse(const chatbox::Frame& frame, bool private_send);
    void handleSyncResponse(const chatbox::Frame& frame);
    void handlePrivateConversations(const chatbox::Frame& frame);
    void handleProfile(const chatbox::Frame& frame);
    void handleGroupPush(const chatbox::Frame& frame);

    bool saveIncoming(const MessageRow& message, const QString& title,
                      bool increment_unread);
    void touchConversation(MessageRow message, const QString& title,
                           bool increment_unread);
    QJsonObject messageToJson(const MessageRow& message) const;

    Transport* transport_ = nullptr;
    bool owns_transport_ = false;
    SessionClient* session_ = nullptr;
    QTimer* reconnect_timer_ = nullptr;
    QTimer* heartbeat_timer_ = nullptr;
    MobileStore store_;
    ServiceConfig config_;
    SecureStore* secure_ = nullptr;

    QString state_ = QStringLiteral("disconnected");
    QString token_;
    QString username_;
    QString password_;
    QString resume_token_;
    quint64 user_id_ = 0;
    QString nickname_;
    quint32 server_caps_ = 0;
    int reconnect_attempt_ = 0;
    bool running_ = false;
    bool shutting_down_ = false;
    bool logged_out_ = false;
    int active_kind_ = 0;
    quint64 active_target_id_ = 0;
    quint32 sync_seq_ = 0;
    QHash<quint32, PendingSend> pending_sends_;
};

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_SERVICE_CHAT_SERVICE_H
