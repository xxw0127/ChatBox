#include "ipc_bridge.h"

#include <QJsonValue>
#include <QTimer>
#include <QVariant>

#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

#include "ipc/ipc_names.h"

namespace {

constexpr const char* kPermissionsClass = "com/chatbox/mobile/ChatBoxPermissions";

QJniObject androidContext() {
    return QNativeInterface::QAndroidApplication::context();
}

}  // namespace

IpcBridge::IpcBridge(QObject* parent) : QObject(parent) {
    connect(&client_, &chatbox::clientcore::IpcClient::connectedChanged, this,
            [this](bool) {
                if (client_.isConnected() && retry_timer_ != nullptr) {
                    retry_timer_->stop();
                } else if (!client_.isConnected() && retry_timer_ != nullptr &&
                           !retry_timer_->isActive()) {
                    retry_timer_->start();
                }
                emit connectedChanged();
            });
    connect(&client_, &chatbox::clientcore::IpcClient::responseReceived, this,
            &IpcBridge::onResponse);
    connect(&client_, &chatbox::clientcore::IpcClient::eventReceived, this,
            &IpcBridge::onEvent);

    retry_timer_ = new QTimer(this);
    retry_timer_->setInterval(1500);
    connect(retry_timer_, &QTimer::timeout, this, &IpcBridge::reconnect);
    client_.connectToService(chatbox::clientcore::ipcServerPath());
    retry_timer_->start();
}

bool IpcBridge::connected() const {
    return client_.isConnected();
}

int IpcBridge::request(const QString& cmd, const QVariantMap& args) {
    return static_cast<int>(client_.request(
        cmd, QJsonObject::fromVariantMap(args)));
}

void IpcBridge::onResponse(quint64 id, const QJsonObject& payload) {
    emit response(static_cast<int>(id), payload.toVariantMap());
}

void IpcBridge::onEvent(const QString& name, const QJsonObject& data) {
    emit event(name, data.toVariantMap());
}

void IpcBridge::reconnect() {
    if (!client_.isConnected()) {
        client_.connectToService(chatbox::clientcore::ipcServerPath());
    }
}

void IpcBridge::requestNotifications() {
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        return;
    }
    QJniObject::callStaticMethod<void>(
        kPermissionsClass, "requestNotifications", "(Landroid/app/Activity;)V",
        context.object());
}

void IpcBridge::openNotificationSettings() {
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        return;
    }
    QJniObject::callStaticMethod<void>(
        kPermissionsClass, "openNotificationSettings",
        "(Landroid/content/Context;)V", context.object());
}

void IpcBridge::openBatterySettings() {
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        return;
    }
    QJniObject::callStaticMethod<void>(
        kPermissionsClass, "openBatteryOptimizationSettings",
        "(Landroid/content/Context;)V", context.object());
}

QVariantMap IpcBridge::takePendingConversation() {
    QVariantMap result;
    const jint kind = QJniObject::callStaticMethod<jint>(
        "com/chatbox/mobile/ChatBoxActivity", "consumePendingKind", "()I");
    if (kind != 1 && kind != 2) {
        return result;
    }
    const jlong target = QJniObject::callStaticMethod<jlong>(
        "com/chatbox/mobile/ChatBoxActivity", "consumePendingTargetId", "()J");
    if (target == 0) {
        return result;
    }
    result.insert(QStringLiteral("kind"), static_cast<int>(kind));
    result.insert(QStringLiteral("target_id"),
                  static_cast<qlonglong>(target));
    return result;
}
