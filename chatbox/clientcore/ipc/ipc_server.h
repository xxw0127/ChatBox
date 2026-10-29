#ifndef CHATBOX_CLIENTCORE_IPC_IPC_SERVER_H
#define CHATBOX_CLIENTCORE_IPC_IPC_SERVER_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>

#include <functional>

class QLocalSocket;

namespace chatbox {
namespace clientcore {

class IpcServer : public QObject {
    Q_OBJECT

public:
    explicit IpcServer(QObject* parent = nullptr);

    bool listen(const QString& path, QString* error);
    // When set, requests are answered synchronously by this handler and the
    // requestReceived signal is not emitted.
    void setRequestHandler(
        std::function<QJsonObject(quint64, const QJsonObject&)> handler);
    void sendResponse(QLocalSocket* socket, const QJsonObject& response);
    void sendEvent(const QJsonObject& event);

signals:
    void requestReceived(QLocalSocket* socket, quint64 id, const QJsonObject& request);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QLocalServer server_;
    QHash<QLocalSocket*, QByteArray> buffers_;
    std::function<QJsonObject(quint64, const QJsonObject&)> handler_;
};

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_IPC_IPC_SERVER_H
