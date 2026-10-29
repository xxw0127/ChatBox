#include <QDebug>
#include <QGuiApplication>
#include <QJniObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QtCore/qcoreapplication_platform.h>

#include <cstring>

#ifdef Q_OS_ANDROID
#include <QtCore/private/qandroidextras_p.h>
#endif

#include "connection_tester.h"
#include "ipc_probe.h"
#include "service_app.h"

int main(int argc, char* argv[]) {
#ifdef Q_OS_ANDROID
    if (argc > 1 && std::strcmp(argv[1], "-service") == 0) {
        QAndroidService app(argc, argv);
        QCoreApplication::setOrganizationName(QStringLiteral("ChatBox"));
        QCoreApplication::setApplicationName(QStringLiteral("ChatBox"));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
        ServiceApp service;
        QString error;
        if (!service.start(&error)) {
            qCritical() << "service start failed:" << error;
            return 1;
        }
        return app.exec();
    }
#endif

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ChatBox"));
    QCoreApplication::setApplicationName(QStringLiteral("ChatBox"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        "com/chatbox/mobile/ChatBoxService", "start",
        "(Landroid/content/Context;)V", context.object());
#endif

    ConnectionTester tester;
    IpcProbe ipc_probe;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("tester"), &tester);
    engine.rootContext()->setContextProperty(QStringLiteral("ipcProbe"), &ipc_probe);
    engine.rootContext()->setContextProperty(QStringLiteral("qtVersion"),
                                             QStringLiteral(QT_VERSION_STR));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("ChatBox"), QStringLiteral("Main"));
    return app.exec();
}
