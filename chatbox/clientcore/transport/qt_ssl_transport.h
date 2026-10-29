#ifndef CHATBOX_CLIENTCORE_TRANSPORT_QT_SSL_TRANSPORT_H
#define CHATBOX_CLIENTCORE_TRANSPORT_QT_SSL_TRANSPORT_H

#include <atomic>

#include "transport/transport.h"

class QThread;

namespace chatbox {
namespace clientcore {

class QtSslTransport : public Transport {
    Q_OBJECT

public:
    explicit QtSslTransport(QObject* parent = nullptr);
    ~QtSslTransport() override;

    bool start(const TransportConfig& config, QString* error) override;
    void stop() override;
    bool sendFrame(const QByteArray& frame, QString* error) override;
    bool isConnected() const override;

private:
    class Worker;

    QThread* thread_ = nullptr;
    Worker* worker_ = nullptr;
    std::atomic<bool> connected_{false};
};

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_TRANSPORT_QT_SSL_TRANSPORT_H
