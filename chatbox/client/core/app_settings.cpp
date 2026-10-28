#include "core/app_settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
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

namespace {

QString canonicalOrAbsolute(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? QDir(path).absolutePath()
                                               : canonical);
}

}  // namespace

bool isHistoryDirectoryInsideInstall(const QString& directory) {
    const QString install =
        canonicalOrAbsolute(QCoreApplication::applicationDirPath());
    const QString target = canonicalOrAbsolute(directory);
    if (target.compare(install, Qt::CaseInsensitive) == 0) {
        return true;
    }
    const QString prefix =
        install.endsWith(QLatin1Char('/')) ? install : install + QLatin1Char('/');
    return target.startsWith(prefix, Qt::CaseInsensitive);
}

QString defaultFileSaveDirectory() {
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (base.isEmpty()) {
        base = QDir::homePath() + QStringLiteral("/Downloads");
    }
    return QDir(base).filePath(QStringLiteral("ChatBox"));
}

QString fileSaveDirectory() {
    QSettings settings;
    const QString stored =
        settings.value(QStringLiteral("files/save_dir")).toString().trimmed();
    return stored.isEmpty() ? defaultFileSaveDirectory() : stored;
}

bool validateFileSaveDirectory(const QString& directory, QString* error) {
    if (directory.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("保存目录不能为空");
        }
        return false;
    }
    const QString absolute = QDir(directory).absolutePath();
    const QString install = QDir(QCoreApplication::applicationDirPath())
                                .absolutePath();
    if (absolute == install || absolute.startsWith(install + "/")) {
        if (error != nullptr) {
            *error = QStringLiteral("不能保存到程序安装目录");
        }
        return false;
    }
    QDir dir(absolute);
    if (!dir.exists() && !QDir().mkpath(absolute)) {
        if (error != nullptr) {
            *error = QStringLiteral("无法创建保存目录");
        }
        return false;
    }
    const QFileInfo info(absolute);
    if (!info.isDir() || !info.isWritable()) {
        if (error != nullptr) {
            *error = QStringLiteral("保存目录不可写");
        }
        return false;
    }
    return true;
}

bool setFileSaveDirectory(const QString& directory, QString* error) {
    if (!validateFileSaveDirectory(directory, error)) {
        return false;
    }
    QSettings settings;
    settings.setValue(QStringLiteral("files/save_dir"),
                      QDir(directory).absolutePath());
    settings.sync();
    return true;
}

QString uniqueFilePath(const QString& directory, const QString& name) {
    const QFileInfo info(name);
    QString stem = info.completeBaseName();
    if (stem.isEmpty()) {
        stem = QStringLiteral("file");
    }
    const QString suffix =
        info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
    QString candidate = QDir(directory).filePath(stem + suffix);
    int index = 1;
    while (QFile::exists(candidate) || QFile::exists(candidate + ".part")) {
        candidate = QDir(directory).filePath(
            QStringLiteral("%1 (%2)%3").arg(stem).arg(index++).arg(suffix));
    }
    return candidate;
}

}  // namespace client
}  // namespace chatbox
