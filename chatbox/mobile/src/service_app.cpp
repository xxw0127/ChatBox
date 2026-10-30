#include "service_app.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJniObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QtCore/qcoreapplication_platform.h>

#include "android_notification_sink.h"
#include "android_secure_store.h"
#include "ipc/ipc_names.h"
#include "ipc/ipc_protocol.h"
#include "ipc/ipc_server.h"
#include "notify/notification_sink.h"
#include "service/chat_service.h"

namespace {

constexpr const char* kServerHost = "121.5.168.189";
constexpr quint16 kServerPort = 9443;
constexpr const char* kNotificationsClass =
    "com/chatbox/mobile/ChatBoxNotifications";

QString androidDeviceName() {
    const QJniObject model = QJniObject::getStaticObjectField(
        "android/os/Build", "MODEL", "Ljava/lang/String;");
    const QString name = model.isValid() ? model.toString().trimmed() : QString();
    return name.isEmpty() ? QStringLiteral("Android") : name;
}

QString foregroundText(const QString& state) {
    if (state == QStringLiteral("online")) {
        return QStringLiteral("已连接");
    }
    if (state == QStringLiteral("reconnecting")) {
        return QStringLiteral("连接断开，正在重连");
    }
    if (state == QStringLiteral("logged_out") ||
        state == QStringLiteral("disconnected")) {
        return QStringLiteral("未登录");
    }
    return QStringLiteral("正在连接…");
}

}  // namespace

ServiceApp::ServiceApp(QObject* parent) : QObject(parent) {}

ServiceApp::~ServiceApp() = default;

bool ServiceApp::start(QString* error) {
    if (chat_ != nullptr) {
        return true;
    }

    notifications_.reset(new chatbox::mobile::AndroidNotificationSink());
    secure_.reset(new chatbox::mobile::AndroidSecureStore());

    server_ = new chatbox::clientcore::IpcServer(this);
    chat_ = new chatbox::clientcore::ChatService(nullptr, this);

    connect(chat_, &chatbox::clientcore::ChatService::stateChanged, this,
            [this](const QString& state, const QString& detail) {
                if (notifications_) {
                    notifications_->setForegroundState(foregroundText(state));
                }
                if (state == QStringLiteral("logged_out")) {
                    for (const int id : notification_ids_) {
                        notifications_->cancel(id);
                    }
                    notification_ids_.clear();
                }
                QJsonObject data;
                data.insert(QStringLiteral("state"), state);
                data.insert(QStringLiteral("detail"), detail);
                if (server_ != nullptr) {
                    server_->sendEvent(chatbox::clientcore::ipcEvent(
                        QStringLiteral("state"), data));
                }
            });
    connect(chat_, &chatbox::clientcore::ChatService::snapshotChanged, this,
            [this](const QJsonObject& snapshot) {
                if (server_ != nullptr) {
                    server_->sendEvent(chatbox::clientcore::ipcEvent(
                        QStringLiteral("snapshot"), snapshot));
                }
            });
    connect(chat_, &chatbox::clientcore::ChatService::eventReceived, this,
            [this](const QJsonObject& event) {
                postMessageNotification(event);
                forwardChatEvent(event);
            });
    connect(chat_, &chatbox::clientcore::ChatService::sessionListReceived,
            this, [this](const QVector<chatbox::SessionInfo>& sessions) {
                QJsonArray array;
                for (const chatbox::SessionInfo& session : sessions) {
                    QJsonObject item;
                    item.insert(QStringLiteral("session_id"),
                                QString::fromStdString(session.session_id));
                    item.insert(QStringLiteral("device_id"),
                                QString::fromStdString(session.device_id));
                    item.insert(QStringLiteral("device_name"),
                                QString::fromStdString(session.device_name));
                    item.insert(QStringLiteral("platform"),
                                QString::fromStdString(session.platform));
                    item.insert(QStringLiteral("app_version"),
                                QString::fromStdString(session.app_version));
                    item.insert(QStringLiteral("created_at"),
                                QString::fromStdString(session.created_at));
                    item.insert(QStringLiteral("last_seen_at"),
                                QString::fromStdString(session.last_seen_at));
                    item.insert(QStringLiteral("current"), session.current);
                    array.append(item);
                }
                QJsonObject data;
                data.insert(QStringLiteral("sessions"), array);
                if (server_ != nullptr) {
                    server_->sendEvent(chatbox::clientcore::ipcEvent(
                        QStringLiteral("session_list"), data));
                }
            });
    connect(chat_, &chatbox::clientcore::ChatService::sessionRevoked, this,
            [this](const QString& session_id) {
                QJsonObject data;
                data.insert(QStringLiteral("session_id"), session_id);
                if (server_ != nullptr) {
                    server_->sendEvent(chatbox::clientcore::ipcEvent(
                        QStringLiteral("session_revoked"), data));
                }
            });

    server_->setRequestHandler([this](quint64 id, const QJsonObject& request) {
        return handleRequest(id, request);
    });
    connect(server_, &chatbox::clientcore::IpcServer::clientDisconnected, this,
            [this]() {
                active_kind_ = 0;
                active_target_id_ = 0;
                if (chat_ != nullptr) {
                    chat_->setActiveConversation(0, 0);
                }
            });
    if (!server_->listen(chatbox::clientcore::ipcServerPath(), error)) {
        return false;
    }

    QFile ca_file(QStringLiteral(":/certs/ca.crt"));
    if (!ca_file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot read CA certificate");
        }
        return false;
    }
    const QString ca_pem = QString::fromUtf8(ca_file.readAll());
    ca_file.close();

    QString directory =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (directory.isEmpty()) {
        directory = QDir::homePath() + QStringLiteral("/.chatbox");
    }
    QDir().mkpath(directory);

    chatbox::clientcore::ServiceConfig config;
    config.session.transport.host = QString::fromLatin1(kServerHost);
    config.session.transport.port = kServerPort;
    config.session.transport.use_tls = true;
    config.session.transport.ca_pem = ca_pem;
    config.session.device_name = androidDeviceName();
    config.session.platform = QStringLiteral("android");
    config.session.app_version =
        QCoreApplication::applicationVersion().isEmpty()
            ? QStringLiteral("0.1.0")
            : QCoreApplication::applicationVersion();
    config.db_path = directory + QStringLiteral("/chatbox.db");
    config.secure_store = secure_.get();
    qInfo() << "ChatBox service config host" << config.session.transport.host
            << "port" << config.session.transport.port << "ca_bytes"
            << ca_pem.size() << "db" << config.db_path;

    if (!chat_->start(config, error)) {
        return false;
    }

    qInfo() << "ChatBox service listening at"
            << chatbox::clientcore::ipcServerPath();
    return true;
}

QJsonObject ServiceApp::handleRequest(quint64 id, const QJsonObject& request) {
    QString cmd;
    QJsonObject args;
    int code = 0;
    QString error;
    if (!chatbox::clientcore::decodeIpcRequest(request, cmd, args, code, error)) {
        return chatbox::clientcore::ipcError(id, code, error);
    }

    if (cmd == QStringLiteral("ping")) {
        QJsonObject data;
        data.insert(QStringLiteral("pong"), true);
        return chatbox::clientcore::ipcOk(id, data);
    }
    if (cmd == QStringLiteral("snapshot")) {
        return chatbox::clientcore::ipcOk(
            id, chat_ != nullptr ? chat_->snapshot() : QJsonObject());
    }
    if (cmd == QStringLiteral("login")) {
        const QString username = args.value(QStringLiteral("username")).toString();
        const QString password = args.value(QStringLiteral("password")).toString();
        if (username.isEmpty() || password.isEmpty()) {
            return chatbox::clientcore::ipcError(
                id, 400, QStringLiteral("username and password are required"));
        }
        chat_->login(username, password);
        QJsonObject data;
        data.insert(QStringLiteral("accepted"), true);
        return chatbox::clientcore::ipcOk(id, data);
    }
    if (cmd == QStringLiteral("logout")) {
        chat_->logout();
        QJsonObject data;
        data.insert(QStringLiteral("accepted"), true);
        return chatbox::clientcore::ipcOk(id, data);
    }
    if (cmd == QStringLiteral("send_text")) {
        const int kind = args.value(QStringLiteral("kind")).toInt();
        const quint64 target_id = static_cast<quint64>(
            args.value(QStringLiteral("target_id"))
                .toVariant()
                .toULongLong());
        const QString content =
            args.value(QStringLiteral("content")).toString();
        if ((kind != 1 && kind != 2) || target_id == 0 || content.isEmpty()) {
            return chatbox::clientcore::ipcError(
                id, 400, QStringLiteral("invalid send_text arguments"));
        }
        if (content.toUtf8().size() > 2000) {
            return chatbox::clientcore::ipcError(
                id, 400, QStringLiteral("message too long"));
        }
        chat_->sendText(kind, target_id, content);
        QJsonObject data;
        data.insert(QStringLiteral("accepted"), true);
        return chatbox::clientcore::ipcOk(id, data);
    }
    if (cmd == QStringLiteral("mark_read")) {
        const int kind = args.value(QStringLiteral("kind")).toInt();
        const quint64 target_id = static_cast<quint64>(
            args.value(QStringLiteral("target_id"))
                .toVariant()
                .toULongLong());
        const quint64 message_id = static_cast<quint64>(
            args.value(QStringLiteral("message_id"))
                .toVariant()
                .toULongLong());
        if ((kind != 1 && kind != 2) || target_id == 0 || message_id == 0) {
            return chatbox::clientcore::ipcError(
                id, 400, QStringLiteral("invalid mark_read arguments"));
        }
        if (kind != active_kind_ || target_id != active_target_id_) {
            // A background ChatPage must not clear unread it did not show.
            QJsonObject data;
            data.insert(QStringLiteral("accepted"), false);
            return chatbox::clientcore::ipcOk(id, data);
        }
        chat_->markRead(kind, target_id, message_id);
        QJsonObject data;
        data.insert(QStringLiteral("accepted"), true);
        return chatbox::clientcore::ipcOk(id, data);
    }
    if (cmd == QStringLiteral("set_active")) {
        const int kind = args.value(QStringLiteral("kind")).toInt();
        const quint64 target_id = static_cast<quint64>(
            args.value(QStringLiteral("target_id"))
                .toVariant()
                .toULongLong());
        if (kind != 0 && kind != 1 && kind != 2) {
            return chatbox::clientcore::ipcError(
                id, 400, QStringLiteral("invalid set_active arguments"));
        }
        active_kind_ = kind;
        active_target_id_ = target_id;
        if (chat_ != nullptr) {
            chat_->setActiveConversation(kind, target_id);
        }
        if (notifications_ != nullptr && kind != 0 && target_id != 0) {
            notifications_->cancel(notificationId(kind, target_id));
            notification_ids_.remove(notificationId(kind, target_id));
        }
        QJsonObject data;
        data.insert(QStringLiteral("accepted"), true);
        return chatbox::clientcore::ipcOk(id, data);
    }
    if (cmd == QStringLiteral("session_list")) {
        chat_->requestSessionList();
        QJsonObject data;
        data.insert(QStringLiteral("accepted"), true);
        return chatbox::clientcore::ipcOk(id, data);
    }
    if (cmd == QStringLiteral("session_revoke")) {
        const QString session_id =
            args.value(QStringLiteral("session_id")).toString();
        if (session_id.isEmpty()) {
            return chatbox::clientcore::ipcError(
                id, 400, QStringLiteral("session_id is required"));
        }
        chat_->revokeSession(session_id);
        QJsonObject data;
        data.insert(QStringLiteral("accepted"), true);
        return chatbox::clientcore::ipcOk(id, data);
    }
    if (cmd == QStringLiteral("notify_test")) {
        const QJniObject context =
            QNativeInterface::QAndroidApplication::context();
        if (context.isValid()) {
            QJniObject::callStaticMethod<void>(
                kNotificationsClass, "notifyTest",
                "(Landroid/content/Context;)V", context.object());
        }
        QJsonObject data;
        data.insert(QStringLiteral("accepted"), true);
        return chatbox::clientcore::ipcOk(id, data);
    }

    return chatbox::clientcore::ipcError(
        id, 404, QStringLiteral("unknown IPC command"));
}

void ServiceApp::forwardChatEvent(const QJsonObject& event) {
    if (server_ == nullptr) {
        return;
    }
    const QString type = event.value(QStringLiteral("type")).toString();
    server_->sendEvent(
        chatbox::clientcore::ipcEvent(type.isEmpty() ? QStringLiteral("chat")
                                                     : type,
                                      event));
}

void ServiceApp::postMessageNotification(const QJsonObject& event) {
    if (notifications_ == nullptr ||
        event.value(QStringLiteral("type")).toString() !=
            QStringLiteral("message")) {
        return;
    }
    const int kind = event.value(QStringLiteral("kind")).toInt();
    const quint64 target_id = static_cast<quint64>(
        event.value(QStringLiteral("target_id")).toVariant().toULongLong());
    if (kind == active_kind_ && target_id == active_target_id_) {
        return;
    }
    const QJsonObject message =
        event.value(QStringLiteral("message")).toObject();
    if (message.value(QStringLiteral("outgoing")).toBool()) {
        return;
    }
    QString title = message.value(QStringLiteral("nickname")).toString();
    if (title.isEmpty()) {
        title = QStringLiteral("ChatBox");
    }
    QString body;
    if (message.value(QStringLiteral("msg_type")).toInt() == 1) {
        body = QStringLiteral("[文件]");
    } else {
        body = message.value(QStringLiteral("content")).toString();
    }
    if (body.isEmpty()) {
        body = QStringLiteral("[新消息]");
    }
    notifications_->postMessage(notificationId(kind, target_id), title, body,
                                kind, target_id);
    notification_ids_.insert(notificationId(kind, target_id));
}

int ServiceApp::notificationId(int kind, quint64 target_id) {
    const int base = kind == 1 ? 100000 : 0;
    return 2000 + base + static_cast<int>(target_id % 90000ULL);
}
