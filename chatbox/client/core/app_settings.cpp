#include "core/app_settings.h"

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
    if (host == QString::fromLatin1(kServerHost) && port == kServerPort && tls) {
        return false;
    }
    // The product endpoint is now a compile-time constant. This also covers the
    // old frp-oil.com host and any custom/LAN address saved by older builds.
    settings.setValue(QStringLiteral("server/host"),
                      QString::fromLatin1(kServerHost));
    settings.setValue(QStringLiteral("server/port"), kServerPort);
    settings.setValue(QStringLiteral("server/tls"), true);
    settings.remove(QStringLiteral("session"));
    settings.sync();
    return true;
}

}  // namespace client
}  // namespace chatbox
