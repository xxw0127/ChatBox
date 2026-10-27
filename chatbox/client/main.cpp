#include "core/app_settings.h"
#include "core/client_log.h"
#include "core/client_policy.h"
#include "core/focus_ring.h"
#include "core/socket_compat.h"
#include "core/ui_theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QStyleFactory>

#include "ui/main_window.h"

int main(int argc, char** argv) {
    // Windows commonly uses 125% or 150%. Qt's default integer rounding can
    // turn 150% into 200%, making the fixed three-pane layout larger than the
    // usable screen; preserve the fractional factor instead.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("ChatBox");
    QCoreApplication::setApplicationName("ChatBox");
    QCoreApplication::setApplicationVersion("0.2.5");
    new chatbox::client::FocusRingFilter(&application);

    // One instance only: two clients would race on the same settings store and
    // (from the history batch on) the same local database. A crashed instance
    // leaves a stale lock that expires instead of blocking forever.
    // --profile <dir> is an explicit acceptance/testing escape hatch: it gets
    // its own lock and INI settings, while normal starts keep the registry
    // store and the default single-instance behaviour.
    const QStringList arguments = QCoreApplication::arguments();
    const int profile_index = arguments.indexOf(QStringLiteral("--profile"));
    QString profile_directory;
    if (profile_index >= 0 && profile_index + 1 < arguments.size()) {
        profile_directory = QDir(arguments.at(profile_index + 1)).absolutePath();
    }

    QString lock_directory;
    if (!profile_directory.isEmpty()) {
        QDir().mkpath(profile_directory);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           profile_directory);
        lock_directory = profile_directory;
    } else {
        lock_directory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(lock_directory);
    const QString profile_lock =
        QDir(lock_directory).filePath(QStringLiteral("chatbox.lock"));
    QLockFile instance_lock(profile_lock);
    instance_lock.setStaleLockTime(30000);
    if (!instance_lock.tryLock(200)) {
        QMessageBox::information(nullptr, "ChatBox", "ChatBox 已在运行。");
        return 0;
    }

    application.setStyle(QStyleFactory::create("Fusion"));
    application.setWindowIcon(QIcon(":/app.ico"));
    installClientLogging();
    qInfo("client starting version=%s", qPrintable(QCoreApplication::applicationVersion()));

    // Sessions and preferences used to live under the Chatroom/ChatroomClient
    // identifiers; copy them once so the ChatBox rename does not log everyone
    // out. The legacy store is left in place so a rollback keeps working.
    {
        QSettings current;
        QSettings legacy(QSettings::UserScope, QStringLiteral("Chatroom"),
                         QStringLiteral("ChatroomClient"));
        if (chatbox::client::migrateLegacySettings(current, legacy)) {
            qInfo("settings migrated from the pre-ChatBox store");
        }
        if (chatbox::client::migrateServerEndpoint(current)) {
            qInfo("server endpoint migrated to 121.5.168.189:9443");
        }
    }
    chatbox::client::theme::applyThemeFromSettings(&application);

    std::string error;
    if (!chatbox::initializeSockets(error)) {
        QMessageBox::critical(nullptr, "Socket init failed", QString::fromStdString(error));
        return 1;
    }

    const bool start_minimized = arguments.contains(QStringLiteral("--minimized"));
    MainWindow window;
    if (!chatbox::client::shouldStartHidden(
            start_minimized, chatbox::client::shellNotificationAreaAvailable())) {
        window.show();
    }
    const int result = application.exec();
    chatbox::cleanupSockets();
    return result;
}
