#include "core/app_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QStringList>
#include <QVariant>

#include "core/client_config.h"

namespace chatbox {
namespace client {

bool migrateLegacySettings(QSettings& current, QSettings& legacy) {
    if (!current.allKeys().isEmpty()) {
        return false;
    }
    const QStringList keys = legacy.allKeys();
    if (keys.isEmpty()) {
        return false;
    }
    for (const QString& key : keys) {
        current.setValue(key, legacy.value(key));
    }
    current.sync();
    return true;
}

bool migrateServerEndpoint(QSettings& settings) {
    if (!settings.contains(QStringLiteral("server/host")) &&
        !settings.contains(QStringLiteral("server/port"))) {
        return false;
    }
    const QString host = settings.value(QStringLiteral("server/host")).toString();
    const int port = settings.value(QStringLiteral("server/port"), 0).toInt();
    const bool tls = settings.value(QStringLiteral("server/tls"), true).toBool();
    if (host == QString::fromLatin1(kServerHost) && port == kServerPort &&
        tls == kServerTls) {
        return false;
    }
    // The product endpoint is now a compile-time constant. This also covers the
    // old frp-oil.com host and any custom/LAN address saved by older builds.
    settings.setValue(QStringLiteral("server/host"),
                      QString::fromLatin1(kServerHost));
    settings.setValue(QStringLiteral("server/port"), kServerPort);
    settings.setValue(QStringLiteral("server/tls"), kServerTls);
    settings.remove(QStringLiteral("session"));
    settings.sync();
    return true;
}

bool isHistoryDirectoryInsideInstall(const QString& directory) {
    const QString install = QDir::cleanPath(
        QDir(QCoreApplication::applicationDirPath()).absolutePath());
    const QString target =
        QDir::cleanPath(QDir(directory).absolutePath());
    if (target.compare(install, Qt::CaseInsensitive) == 0) {
        return true;
    }
    const QString prefix =
        install.endsWith(QLatin1Char('/')) ? install : install + QLatin1Char('/');
    return target.startsWith(prefix, Qt::CaseInsensitive);
}

}  // namespace client
}  // namespace chatbox
