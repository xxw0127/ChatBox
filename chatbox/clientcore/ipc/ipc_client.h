#ifndef CHATBOX_CLIENTCORE_IPC_IPC_CLIENT_H
#define CHATBOX_CLIENTCORE_IPC_IPC_CLIENT_H

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QVector>

namespace chatbox {
namespace clientcore {

// UI-side IPC client. Requests issued before the service socket is up are
// queued and flushed on connect.
class IpcClient : public QObject {
    Q_OBJECT

public:
    explicit IpcClient(QObject* parent = nullptr);

    void connectToService(const QString& name);
    void disconnectFromService();
    bool isConnected() const;
    // Returns the request id, or 0 when the request was rejected locally.
    quint64 request(const QString& cmd, const QJsonObject& args);

signals:
    void connectedChanged(bool connected);
    void responseReceived(quint64 id, const QJsonObject& response);
    void eventReceived(const QString& event, const QJsonObject& data);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onErrorOccurred(QLocalSocket::LocalSocketError error);

private:
    void flushPending();

    QLocalSocket socket_;
    QByteArray buffer_;
    quint64 next_id_ = 1;
    QVector<QJsonObject> pending_;
};

}  // namespace clientcore
}  // namespace chatbox

#endif  // CHATBOX_CLIENTCORE_IPC_IPC_CLIENT_H
