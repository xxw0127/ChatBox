#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "connection_tester.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("ChatBox"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ConnectionTester tester;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("tester"), &tester);
    engine.rootContext()->setContextProperty(QStringLiteral("qtVersion"),
                                             QStringLiteral(QT_VERSION_STR));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("ChatBox"), QStringLiteral("Main"));
    return app.exec();
}
