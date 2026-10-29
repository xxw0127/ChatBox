#include "android_secure_store.h"

#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace chatbox {
namespace mobile {

namespace {

constexpr const char* kKeystoreClass = "com/chatbox/mobile/ChatBoxKeystore";
constexpr const char* kTokenAlias = "session_token";
constexpr const char* kDeviceAlias = "device_id";

QString readSecret(const char* alias) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return QString();
    }
    const QJniObject jalias = QJniObject::fromString(QString::fromLatin1(alias));
    const QJniObject value = QJniObject::callStaticObjectMethod(
        kKeystoreClass, "getSecret",
        "(Landroid/content/Context;Ljava/lang/String;)Ljava/lang/String;",
        context.object(), jalias.object());
    return value.isValid() ? value.toString() : QString();
}

void writeSecret(const char* alias, const QString& value) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return;
    }
    const QJniObject jalias = QJniObject::fromString(QString::fromLatin1(alias));
    const QJniObject jvalue = QJniObject::fromString(value);
    QJniObject::callStaticMethod<void>(
        kKeystoreClass, "putSecret",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)V",
        context.object(), jalias.object(), jvalue.object());
}

void eraseSecret(const char* alias) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return;
    }
    const QJniObject jalias = QJniObject::fromString(QString::fromLatin1(alias));
    QJniObject::callStaticMethod<void>(
        kKeystoreClass, "clearSecret",
        "(Landroid/content/Context;Ljava/lang/String;)V", context.object(),
        jalias.object());
}

}  // namespace

QString AndroidSecureStore::loadToken() {
    return readSecret(kTokenAlias);
}

void AndroidSecureStore::saveToken(const QString& token) {
    if (token.isEmpty()) {
        clearToken();
        return;
    }
    writeSecret(kTokenAlias, token);
}

void AndroidSecureStore::clearToken() {
    eraseSecret(kTokenAlias);
}

QString AndroidSecureStore::loadDeviceId() {
    return readSecret(kDeviceAlias);
}

void AndroidSecureStore::saveDeviceId(const QString& device_id) {
    if (device_id.isEmpty()) {
        eraseSecret(kDeviceAlias);
        return;
    }
    writeSecret(kDeviceAlias, device_id);
}

}  // namespace mobile
}  // namespace chatbox
