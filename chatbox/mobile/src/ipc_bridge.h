#ifndef CHATBOX_MOBILE_IPC_BRIDGE_H
#define CHATBOX_MOBILE_IPC_BRIDGE_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include "ipc/ipc_client.h"

class QTimer;

// QML-facing wrapper around the service IPC client. It keeps retrying until
// the service socket is up and translates JSON envelopes into QVariantMaps.
class IpcBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)

public:
    explicit IpcBridge(QObject* parent = nullptr);

    bool connected() const;

    Q_INVOKABLE int request(const QString& cmd, const QVariantMap& args);
    Q_INVOKABLE void requestNotifications();
    Q_INVOKABLE void openNotificationSettings();
    Q_INVOKABLE void openBatterySettings();
    // Returns {kind, target_id} once for a conversation opened from a
    // notification tap, otherwise an empty map.
    Q_INVOKABLE QVariantMap takePendingConversation();

signals:
    void response(int id, const QVariantMap& data);
    void event(const QString& name, const QVariantMap& data);
    void connectedChanged();

private slots:
    void onResponse(quint64 id, const QJsonObject& response);
    void onEvent(const QString& name, const QJsonObject& data);
    void reconnect();

private:
    chatbox::clientcore::IpcClient client_;
    QTimer* retry_timer_ = nullptr;
};

#endif  // CHATBOX_MOBILE_IPC_BRIDGE_H
