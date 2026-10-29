#include "android_notification_sink.h"

#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>

namespace chatbox {
namespace mobile {

namespace {

constexpr const char* kNotificationsClass =
    "com/chatbox/mobile/ChatBoxNotifications";

QJniObject androidContext() {
    return QNativeInterface::QAndroidApplication::context();
}

}  // namespace

void AndroidNotificationSink::postMessage(int id, const QString& title,
                                          const QString& body, int kind,
                                          quint64 target_id) {
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        return;
    }
    const QJniObject jtitle = QJniObject::fromString(title);
    const QJniObject jbody = QJniObject::fromString(body);
    QJniObject::callStaticMethod<void>(
        kNotificationsClass, "postMessage",
        "(Landroid/content/Context;ILjava/lang/String;Ljava/lang/String;IJ)V",
        context.object(), static_cast<jint>(id), jtitle.object(),
        jbody.object(), static_cast<jint>(kind),
        static_cast<jlong>(target_id));
}

void AndroidNotificationSink::cancel(int id) {
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        return;
    }
    QJniObject::callStaticMethod<void>(
        kNotificationsClass, "cancel", "(Landroid/content/Context;I)V",
        context.object(), static_cast<jint>(id));
}

void AndroidNotificationSink::setForegroundState(const QString& text) {
    const QJniObject context = androidContext();
    if (!context.isValid()) {
        return;
    }
    const QJniObject jtext = QJniObject::fromString(text);
    QJniObject::callStaticMethod<void>(
        kNotificationsClass, "setForegroundState",
        "(Landroid/content/Context;Ljava/lang/String;)V", context.object(),
        jtext.object());
}

}  // namespace mobile
}  // namespace chatbox
