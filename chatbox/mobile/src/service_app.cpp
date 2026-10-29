#include "service_app.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJniObject>
#include <QJsonObject>
#include <QLocalSocket>
#include <QtCore/qcoreapplication_platform.h>

#include "ipc/ipc_names.h"
#include "ipc/ipc_server.h"

namespace {
constexpr const char* kNotificationsClass = "com/chatbox/mobile/ChatBoxNotifications";
}  // namespace

ServiceApp::ServiceApp(QObject* parent) : QObject(parent) {}

bool ServiceApp::start(QString* error) {
    server_ = new chatbox::clientcore::IpcServer(this);
    connect(server_, &chatbox::clientcore::IpcServer::requestReceived, this,
            [this](QLocalSocket* socket, quint64 id, const QJsonObject& request) {
                const QString cmd = request.value(QStringLiteral("cmd")).toString();
                QJsonObject response;
                response.insert(QStringLiteral("id"),
                                static_cast<double>(id));
                if (cmd == QStringLiteral("ping")) {
                    response.insert(QStringLiteral("ok"), true);
                    QJsonObject data;
                    data.insert(QStringLiteral("pong"), true);
                    response.insert(QStringLiteral("data"), data);
                } else if (cmd == QStringLiteral("notify_test")) {
                    QJniObject context = QNativeInterface::QAndroidApplication::context();
                    QJniObject::callStaticMethod<void>(
                        kNotificationsClass, "notifyTest",
                        "(Landroid/content/Context;)V", context.object());
                    response.insert(QStringLiteral("ok"), true);
                } else {
                    response.insert(QStringLiteral("ok"), false);
                    QJsonObject error_object;
                    error_object.insert(QStringLiteral("code"), 404);
                    error_object.insert(QStringLiteral("message"),
                                        QStringLiteral("unknown IPC command"));
                    response.insert(QStringLiteral("error"), error_object);
                }
                server_->sendResponse(socket, response);
            });

    if (!server_->listen(chatbox::clientcore::ipcServerPath(), error)) {
        return false;
    }

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(kNotificationsClass, "notifyTest",
                                       "(Landroid/content/Context;)V",
                                       context.object());
    qInfo() << "ChatBox service listening at" << chatbox::clientcore::ipcServerPath();
    return true;
}
