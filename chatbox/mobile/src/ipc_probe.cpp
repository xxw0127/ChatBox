#include "ipc_probe.h"

#include <QDebug>
#include <QJsonObject>
#include <QLocalSocket>

#include "ipc/ipc_frame.h"
#include "ipc/ipc_names.h"

IpcProbe::IpcProbe(QObject* parent) : QObject(parent) {
    socket_ = new QLocalSocket(this);
    connect(socket_, &QLocalSocket::connected, this, [this]() {
        setStatus(QStringLiteral("service: 已连接"));
        QJsonObject request;
        request.insert(QStringLiteral("id"), 1);
        request.insert(QStringLiteral("cmd"), QStringLiteral("ping"));
        socket_->write(chatbox::clientcore::encodeIpcFrame(request));
        socket_->flush();
    });
    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
        setStatus(QStringLiteral("service: 已断开"));
    });
    connect(socket_, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) {
                setStatus(QStringLiteral("service: ") + socket_->errorString());
            });
    connect(socket_, &QLocalSocket::readyRead, this, [this]() {
        buffer_.append(socket_->readAll());
        while (true) {
            QJsonObject response;
            QString error;
            if (!chatbox::clientcore::decodeIpcFrame(buffer_, response, &error)) {
                if (!error.isEmpty()) {
                    setStatus(QStringLiteral("service: ") + error);
                }
                return;
            }
            const QJsonObject data = response.value(QStringLiteral("data")).toObject();
            if (response.value(QStringLiteral("ok")).toBool() &&
                data.value(QStringLiteral("pong")).toBool()) {
                qInfo() << "IPC pong received";
                setStatus(QStringLiteral("service: pong"));
            } else {
                setStatus(QStringLiteral("service: 响应异常"));
            }
        }
    });
}

QString IpcProbe::status() const {
    return status_;
}

void IpcProbe::ping() {
    if (socket_->state() == QLocalSocket::ConnectedState) {
        QJsonObject request;
        request.insert(QStringLiteral("id"), 2);
        request.insert(QStringLiteral("cmd"), QStringLiteral("ping"));
        socket_->write(chatbox::clientcore::encodeIpcFrame(request));
        socket_->flush();
        setStatus(QStringLiteral("service: 等待响应"));
        return;
    }
    setStatus(QStringLiteral("service: 连接中..."));
    socket_->abort();
    socket_->connectToServer(chatbox::clientcore::ipcServerPath());
}

void IpcProbe::setStatus(const QString& status) {
    if (status_ == status) {
        return;
    }
    status_ = status;
    emit statusChanged();
}
