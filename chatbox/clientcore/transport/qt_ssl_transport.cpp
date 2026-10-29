#include "transport/qt_ssl_transport.h"

#include <QDebug>
#include <QMetaObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QThread>

#include <utility>

#include "common/protocol.h"

namespace chatbox {
namespace clientcore {

class QtSslTransport::Worker : public QObject {
    Q_OBJECT

public:
    explicit Worker(QObject* parent = nullptr) : QObject(parent) {}

public slots:
    void connectToServer(const TransportConfig& config) {
        if (socket_ != nullptr) {
            socket_->abort();
            socket_->deleteLater();
            socket_ = nullptr;
        }
        buffer_.clear();
        connected_ = false;

        const QList<QSslCertificate> ca_certificates =
            QSslCertificate::fromData(config.ca_pem.toUtf8(), QSsl::Pem);
        if (ca_certificates.isEmpty()) {
            qWarning() << "QtSslTransport: CA certificate is empty or invalid, bytes="
                       << config.ca_pem.size();
            emit disconnected(QStringLiteral("CA certificate is empty or invalid"));
            return;
        }

        socket_ = new QSslSocket(this);
        connect(socket_, &QSslSocket::encrypted, this, &Worker::onEncrypted);
        connect(socket_, &QSslSocket::readyRead, this, &Worker::onReadyRead);
        connect(socket_, &QSslSocket::disconnected, this, &Worker::onDisconnected);
        connect(socket_, &QSslSocket::errorOccurred, this, &Worker::onError);
        connect(socket_, &QSslSocket::sslErrors, this, &Worker::onSslErrors);

        QSslConfiguration ssl = socket_->sslConfiguration();
        ssl.setCaCertificates(ca_certificates);
        ssl.setProtocol(QSsl::TlsV1_2OrLater);
        socket_->setSslConfiguration(ssl);
        socket_->setPeerVerifyName(config.host);
        socket_->connectToHostEncrypted(config.host, config.port);
    }

    void sendFrame(const QByteArray& frame) {
        if (socket_ == nullptr || !connected_) {
            emit disconnected(QStringLiteral("not connected"));
            return;
        }
        if (socket_->write(frame) < 0) {
            emit disconnected(socket_->errorString());
            return;
        }
        socket_->flush();
    }

    void stop() {
        if (socket_ == nullptr) {
            return;
        }
        connected_ = false;
        socket_->disconnectFromHost();
        if (socket_->state() != QAbstractSocket::UnconnectedState) {
            socket_->waitForDisconnected(1000);
        }
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
        buffer_.clear();
    }

signals:
    void connected();
    void disconnected(const QString& reason);
    void frameReceived(const QByteArray& frame);

private slots:
    void onEncrypted() {
        connected_ = true;
        qInfo() << "QtSslTransport: connected";
        emit connected();
    }

    void onReadyRead() {
        if (socket_ == nullptr) {
            return;
        }
        buffer_.append(socket_->readAll());
        while (buffer_.size() >= static_cast<int>(chatbox::kFrameHeaderSize)) {
            chatbox::FrameHeader header;
            if (!chatbox::decodeHeader(buffer_.constData(), buffer_.size(), header)) {
                emit disconnected(QStringLiteral("invalid frame header"));
                socket_->abort();
                return;
            }
            std::string error;
            if (!chatbox::validateHeader(header, chatbox::kMaxOutFrameBody, error)) {
                emit disconnected(QString::fromStdString(error));
                socket_->abort();
                return;
            }
            const int total = static_cast<int>(chatbox::kFrameHeaderSize) +
                              static_cast<int>(header.body_len);
            if (buffer_.size() < total) {
                return;
            }
            const QByteArray frame = buffer_.left(total);
            buffer_.remove(0, total);
            emit frameReceived(frame);
        }
    }

    void onDisconnected() {
        if (connected_) {
            connected_ = false;
            emit disconnected(QStringLiteral("connection closed"));
        }
    }

    void onError(QAbstractSocket::SocketError) {
        if (socket_ != nullptr) {
            connected_ = false;
            qWarning() << "QtSslTransport: socket error" << socket_->errorString();
            emit disconnected(socket_->errorString());
        }
    }

    void onSslErrors(const QList<QSslError>& errors) {
        QStringList messages;
        for (const QSslError& error : errors) {
            messages.append(error.errorString());
        }
        connected_ = false;
        qWarning() << "QtSslTransport: TLS verification failed" << messages;
        emit disconnected(messages.isEmpty() ? QStringLiteral("TLS verification failed")
                                             : messages.join(QStringLiteral("; ")));
        if (socket_ != nullptr) {
            socket_->abort();
        }
    }

private:
    QSslSocket* socket_ = nullptr;
    QByteArray buffer_;
    bool connected_ = false;
};

QtSslTransport::QtSslTransport(QObject* parent) : Transport(parent) {}

QtSslTransport::~QtSslTransport() {
    stop();
}

bool QtSslTransport::start(const TransportConfig& config, QString* error) {
    if (config.host.isEmpty() || config.port == 0) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid transport config");
        }
        return false;
    }
    if (thread_ != nullptr) {
        stop();
    }

    thread_ = new QThread();
    worker_ = new Worker();
    worker_->moveToThread(thread_);
    connect(worker_, &Worker::connected, this, [this]() {
        connected_ = true;
        emit connected();
    });
    connect(worker_, &Worker::disconnected, this, [this](const QString& reason) {
        connected_ = false;
        emit disconnected(reason);
    });
    connect(worker_, &Worker::frameReceived, this, &Transport::frameReceived);
    connect(thread_, &QThread::finished, worker_, &QObject::deleteLater);
    thread_->start();
    QMetaObject::invokeMethod(
        worker_, [worker = worker_, config]() { worker->connectToServer(config); },
        Qt::QueuedConnection);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

void QtSslTransport::stop() {
    if (thread_ == nullptr) {
        return;
    }
    if (worker_ != nullptr && thread_->isRunning()) {
        QMetaObject::invokeMethod(worker_, &Worker::stop, Qt::BlockingQueuedConnection);
    }
    thread_->quit();
    thread_->wait(3000);
    worker_ = nullptr;
    delete thread_;
    thread_ = nullptr;
    connected_ = false;
}

bool QtSslTransport::sendFrame(const QByteArray& frame, QString* error) {
    if (!connected_) {
        if (error != nullptr) {
            *error = QStringLiteral("not connected");
        }
        return false;
    }
    if (worker_ == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("transport not started");
        }
        return false;
    }
    QMetaObject::invokeMethod(
        worker_, [worker = worker_, frame]() { worker->sendFrame(frame); },
        Qt::QueuedConnection);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool QtSslTransport::isConnected() const {
    return connected_;
}

}  // namespace clientcore
}  // namespace chatbox

#include "qt_ssl_transport.moc"
