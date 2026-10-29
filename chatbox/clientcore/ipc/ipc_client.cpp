#include "ipc/ipc_client.h"

#include <QJsonValue>

#include "ipc/ipc_frame.h"

namespace chatbox {
namespace clientcore {

IpcClient::IpcClient(QObject* parent) : QObject(parent) {
    connect(&socket_, &QLocalSocket::connected, this, &IpcClient::onConnected);
    connect(&socket_, &QLocalSocket::disconnected, this,
            &IpcClient::onDisconnected);
    connect(&socket_, &QLocalSocket::readyRead, this, &IpcClient::onReadyRead);
    connect(&socket_, &QLocalSocket::errorOccurred, this,
            &IpcClient::onErrorOccurred);
}

void IpcClient::connectToService(const QString& name) {
    socket_.abort();
    buffer_.clear();
    socket_.connectToServer(name);
}

void IpcClient::disconnectFromService() {
    pending_.clear();
    socket_.disconnectFromServer();
}

bool IpcClient::isConnected() const {
    return socket_.state() == QLocalSocket::ConnectedState;
}

quint64 IpcClient::request(const QString& cmd, const QJsonObject& args) {
    if (cmd.isEmpty()) {
        return 0;
    }
    QJsonObject object;
    const quint64 id = next_id_++;
    object.insert(QStringLiteral("id"), static_cast<qint64>(id));
    object.insert(QStringLiteral("cmd"), cmd);
    object.insert(QStringLiteral("args"), args);
    if (socket_.state() == QLocalSocket::ConnectedState) {
        const QByteArray frame = encodeIpcFrame(object);
        if (frame.isEmpty()) {
            return 0;
        }
        socket_.write(frame);
        socket_.flush();
    } else {
        pending_.push_back(object);
    }
    return id;
}

void IpcClient::flushPending() {
    if (socket_.state() != QLocalSocket::ConnectedState) {
        return;
    }
    for (const QJsonObject& object : pending_) {
        const QByteArray frame = encodeIpcFrame(object);
        if (!frame.isEmpty()) {
            socket_.write(frame);
        }
    }
    pending_.clear();
    socket_.flush();
}

void IpcClient::onConnected() {
    emit connectedChanged(true);
    flushPending();
}

void IpcClient::onDisconnected() {
    buffer_.clear();
    emit connectedChanged(false);
}

void IpcClient::onReadyRead() {
    buffer_.append(socket_.readAll());
    while (true) {
        QJsonObject object;
        QString error;
        if (!decodeIpcFrame(buffer_, object, &error)) {
            if (!error.isEmpty()) {
                buffer_.clear();
                socket_.disconnectFromServer();
            }
            return;
        }
        if (object.contains(QStringLiteral("event"))) {
            emit eventReceived(
                object.value(QStringLiteral("event")).toString(),
                object.value(QStringLiteral("data")).toObject());
            continue;
        }
        const quint64 id = static_cast<quint64>(
            object.value(QStringLiteral("id")).toVariant().toULongLong());
        emit responseReceived(id, object);
    }
}

void IpcClient::onErrorOccurred(QLocalSocket::LocalSocketError error) {
    Q_UNUSED(error);
    emit connectedChanged(false);
}

}  // namespace clientcore
}  // namespace chatbox
