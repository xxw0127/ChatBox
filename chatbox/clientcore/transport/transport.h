#ifndef CHATBOX_CLIENTCORE_TRANSPORT_TRANSPORT_H
#define CHATBOX_CLIENTCORE_TRANSPORT_TRANSPORT_H

#include <QByteArray>
#include <QObject>
#include <QString>

namespace chatbox {
namespace clientcore {

struct TransportConfig {
    QString host;
    quint16 port = 0;
    bool use_tls = true;
    QString ca_pem;
};

class Transport : public QObject {
    Q_OBJECT

public:
    explicit Transport(QObject* parent = nullptr);
    ~Transport() override;

    virtual bool start(const TransportConfig& config, QString* error) = 0;
    virtual void stop() = 0;
    virtual bool sendFrame(const QByteArray& frame, QString* error) = 0;
    virtual bool isConnected() const = 0;

signals:
    void connected();
    void disconnected(const QString& reason);
    void frameReceived(const QByteArray& frame);
};

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_TRANSPORT_TRANSPORT_H
