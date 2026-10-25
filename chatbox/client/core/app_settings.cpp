#include "core/app_settings.h"

#include <QStringList>
#include <QVariant>

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
    const bool old_server =
        settings.value(QStringLiteral("server/host")).toString() ==
            QStringLiteral("frp-oil.com") &&
        settings.value(QStringLiteral("server/port"), 15318).toInt() == 15318;
    if (!old_server) {
        return false;
    }
    settings.setValue(QStringLiteral("server/host"),
                      QStringLiteral("121.5.168.189"));
    settings.setValue(QStringLiteral("server/port"), 9443);
    settings.setValue(QStringLiteral("server/tls"), true);
    // The old token is bound to the retired host and cannot be resumed there.
    settings.remove(QStringLiteral("session"));
    settings.sync();
    return true;
}

}  // namespace client
}  // namespace chatbox
