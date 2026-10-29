#ifndef CHATBOX_CLIENTCORE_SESSION_SESSION_CLIENT_H
#define CHATBOX_CLIENTCORE_SESSION_SESSION_CLIENT_H

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

#include "common/protocol.h"
#include "common/session_protocol.h"
#include "transport/transport.h"

namespace chatbox {
namespace clientcore {

struct SessionConfig {
    TransportConfig transport;
    QString device_id;
    QString device_name;
    QString platform = QStringLiteral("android");
    QString app_version;
};

struct LoginResult {
    quint64 user_id = 0;
    QString nickname;
    QString token;
    quint32 server_caps = 0;
    quint32 ttl_seconds = 0;
};

// Owns authentication, session management, and the request/response sequence
// numbers on top of a Transport. It never starts the transport; ChatService
// does that and hands the live Transport to this object.
class SessionClient : public QObject {
    Q_OBJECT

public:
    explicit SessionClient(Transport* transport, QObject* parent = nullptr);

    void setConfig(const SessionConfig& config);
    const SessionConfig& config() const;

    void login(const QString& username, const QString& password);
    void resume(const QString& token);
    void logout();
    void requestSessionList();
    void revokeSession(const QString& session_id);
    // Sends any command through the shared sequence counter and returns the
    // sequence number, or 0 when the transport rejected the frame.
    quint32 sendRequest(uint16_t command, const std::string& body);

signals:
    void loginSucceeded(const chatbox::clientcore::LoginResult& result);
    void loginFailed(int code, const QString& message);
    void logoutSucceeded();
    void sessionListReceived(const QVector<chatbox::SessionInfo>& sessions);
    void sessionRevoked(const QString& session_id);
    // Failures for non-auth requests (session list, revoke, logout).
    void requestFailed(int code, const QString& message);
    // Raw frames this client does not consume (chat pushes, history, ...).
    void frameReceived(const QByteArray& raw_frame);

private:
    void handleFrame(const QByteArray& raw);
    void handleLoginResponse(const chatbox::Frame& frame, bool is_resume,
                             const QString& resume_token);
    void handleError(const chatbox::Frame& frame);
    void handleSessionList(const chatbox::Frame& frame);
    void handleSessionRevoke(const chatbox::Frame& frame);
    bool send(chatbox::Frame frame, QString* error);

    Transport* transport_ = nullptr;
    SessionConfig config_;
    quint32 next_seq_ = 1;
    quint32 login_seq_ = 0;
    quint32 resume_seq_ = 0;
    quint32 logout_seq_ = 0;
    quint32 session_list_seq_ = 0;
    quint32 revoke_seq_ = 0;
    QString pending_revoke_session_id_;
    QString resume_token_;
};

}  // namespace clientcore
}  // namespace chatbox

Q_DECLARE_METATYPE(chatbox::clientcore::LoginResult)
Q_DECLARE_METATYPE(chatbox::SessionInfo)
Q_DECLARE_METATYPE(QVector<chatbox::SessionInfo>)

#endif  // CHATBOX_CLIENTCORE_SESSION_SESSION_CLIENT_H
