#include "connection_tester.h"

#include <QDebug>
#include <QFile>

#include "common/protocol.h"
#include "transport/qt_ssl_transport.h"

namespace {
constexpr const char* kServerHost = "121.5.168.189";
constexpr quint16 kServerPort = 9443;
}  // namespace

ConnectionTester::ConnectionTester(QObject* parent) : QObject(parent) {
    transport_ = new chatbox::clientcore::QtSslTransport(this);
    connect(transport_, &chatbox::clientcore::Transport::connected, this, [this]() {
        qInfo() << "TLS connected";
        setStatus(QStringLiteral("TLS connected"));
        chatbox::Frame ping;
        ping.header.cmd = chatbox::kCmdPing;
        ping.header.seq = 1;
        QString error;
        if (!transport_->sendFrame(
                QByteArray::fromStdString(chatbox::encodeFrame(ping)), &error)) {
            setStatus(QStringLiteral("PING 发送失败: ") + error);
        }
    });
    connect(transport_, &chatbox::clientcore::Transport::disconnected, this,
            [this](const QString& reason) {
                qWarning() << "TLS disconnected:" << reason;
                setStatus(QStringLiteral("断开: ") + reason);
            });
    connect(transport_, &chatbox::clientcore::Transport::frameReceived, this,
            [this](const QByteArray& bytes) {
                chatbox::FrameHeader header;
                if (!chatbox::decodeHeader(bytes.constData(), bytes.size(), header)) {
                    setStatus(QStringLiteral("收到非法帧"));
                    return;
                }
                if (header.cmd == chatbox::kCmdPing &&
                    (header.flags & chatbox::kFlagResponse) != 0) {
                    qInfo() << "PONG received";
                    setStatus(QStringLiteral("PONG received"));
                }
            });
}

QString ConnectionTester::status() const {
    return status_;
}

void ConnectionTester::test() {
    QFile ca_file(QStringLiteral(":/certs/ca.crt"));
    if (!ca_file.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("CA 读取失败"));
        return;
    }
    const QString ca_pem = QString::fromUtf8(ca_file.readAll());
    chatbox::clientcore::TransportConfig config;
    config.host = QString::fromLatin1(kServerHost);
    config.port = kServerPort;
    config.use_tls = true;
    config.ca_pem = ca_pem;
    setStatus(QStringLiteral("连接中..."));
    QString error;
    if (!transport_->start(config, &error)) {
        setStatus(QStringLiteral("连接失败: ") + error);
    }
}

void ConnectionTester::setStatus(const QString& status) {
    if (status_ == status) {
        return;
    }
    status_ = status;
    emit statusChanged();
}
