#include "ipc/ipc_server.h"

#include <QDebug>
#include <QLocalSocket>

#include <utility>

#include "ipc/ipc_frame.h"
#include "ipc/ipc_protocol.h"

namespace chatbox {
namespace clientcore {

IpcServer::IpcServer(QObject* parent) : QObject(parent) {
    connect(&server_, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
}

bool IpcServer::listen(const QString& path, QString* error) {
    if (path.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("empty IPC server path");
        }
        return false;
    }
    QLocalServer::removeServer(path);
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_.listen(path)) {
        if (error != nullptr) {
            *error = server_.errorString();
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void IpcServer::setRequestHandler(
    std::function<QJsonObject(quint64, const QJsonObject&)> handler) {
    handler_ = std::move(handler);
}

void IpcServer::sendResponse(QLocalSocket* socket, const QJsonObject& response) {
    if (socket == nullptr || socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    const QByteArray frame = encodeIpcFrame(response);
    if (frame.isEmpty()) {
        qWarning() << "IPC response too large";
        return;
    }
    socket->write(frame);
    socket->flush();
}

void IpcServer::sendEvent(const QJsonObject& event) {
    const QByteArray frame = encodeIpcFrame(event);
    if (frame.isEmpty()) {
        qWarning() << "IPC event too large";
        return;
    }
    for (auto it = buffers_.begin(); it != buffers_.end(); ++it) {
        QLocalSocket* socket = it.key();
        if (socket != nullptr && socket->state() == QLocalSocket::ConnectedState) {
            socket->write(frame);
            socket->flush();
        }
    }
}

void IpcServer::onNewConnection() {
    while (server_.hasPendingConnections()) {
        QLocalSocket* socket = server_.nextPendingConnection();
        if (socket == nullptr) {
            continue;
        }
        buffers_.insert(socket, QByteArray());
        connect(socket, &QLocalSocket::readyRead, this, &IpcServer::onReadyRead);
        connect(socket, &QLocalSocket::disconnected, this, &IpcServer::onDisconnected);
    }
}

void IpcServer::onReadyRead() {
    QLocalSocket* socket = qobject_cast<QLocalSocket*>(sender());
    if (socket == nullptr || !buffers_.contains(socket)) {
        return;
    }
    QByteArray& buffer = buffers_[socket];
    buffer.append(socket->readAll());
    while (true) {
        QJsonObject request;
        QString error;
        if (!decodeIpcFrame(buffer, request, &error)) {
            if (!error.isEmpty()) {
                qWarning() << "IPC protocol error:" << error;
                QJsonObject response;
                response.insert(QStringLiteral("id"), 0);
                response.insert(QStringLiteral("ok"), false);
                QJsonObject error_object;
                error_object.insert(QStringLiteral("code"), 400);
                error_object.insert(QStringLiteral("message"), error);
                response.insert(QStringLiteral("error"), error_object);
                sendResponse(socket, response);
                socket->disconnectFromServer();
            }
            return;
        }
        const quint64 id =
            static_cast<quint64>(request.value(QStringLiteral("id")).toVariant().toULongLong());
        if (handler_) {
            QJsonObject response = handler_(id, request);
            if (response.isEmpty()) {
                response = ipcError(id, 500, QStringLiteral("internal error"));
            }
            sendResponse(socket, response);
        } else {
            emit requestReceived(socket, id, request);
        }
    }
}

void IpcServer::onDisconnected() {
    QLocalSocket* socket = qobject_cast<QLocalSocket*>(sender());
    if (socket == nullptr) {
        return;
    }
    buffers_.remove(socket);
    socket->deleteLater();
}

}  // namespace clientcore
}  // namespace chatbox
