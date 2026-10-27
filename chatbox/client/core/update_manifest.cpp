#include "core/update_manifest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QVersionNumber>

#include "core/client_config.h"

namespace chatbox {
namespace client {

namespace {

constexpr int kMaxManifestBytes = 64 * 1024;

void setError(QString* error, const QString& message) {
    if (error != nullptr) {
        *error = message;
    }
}

}  // namespace

UpdateManifest parseUpdateManifest(const QByteArray& json, QString* error) {
    UpdateManifest manifest;
    if (json.size() > kMaxManifestBytes) {
        setError(error, QStringLiteral("更新清单过大"));
        return manifest;
    }
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("更新清单 JSON 无效"));
        return manifest;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toInt() != 1) {
        setError(error, QStringLiteral("更新清单版本不支持"));
        return manifest;
    }

    manifest.version = object.value(QStringLiteral("version")).toString().trimmed();
    if (manifest.version.isEmpty() || QVersionNumber::fromString(manifest.version).isNull()) {
        setError(error, QStringLiteral("更新版本号无效"));
        return manifest;
    }

    manifest.url = QUrl(object.value(QStringLiteral("url")).toString());
    if (!manifest.url.isValid() || manifest.url.scheme() != QStringLiteral("https") ||
        manifest.url.host() != QString::fromLatin1(kServerHost)) {
        setError(error, QStringLiteral("更新地址不是受信任的 HTTPS 地址"));
        return manifest;
    }

    manifest.sha256 =
        object.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    static const QRegularExpression sha_pattern(QStringLiteral("^[0-9a-f]{64}$"));
    if (!sha_pattern.match(manifest.sha256).hasMatch()) {
        setError(error, QStringLiteral("更新包 SHA-256 无效"));
        return manifest;
    }

    manifest.notes = object.value(QStringLiteral("notes")).toString();
    manifest.valid = true;
    return manifest;
}

bool isNewerVersion(const QString& current, const QString& remote) {
    const QVersionNumber local = QVersionNumber::fromString(current);
    const QVersionNumber available = QVersionNumber::fromString(remote);
    if (local.isNull() || available.isNull()) {
        return false;
    }
    return available > local;
}

}  // namespace client
}  // namespace chatbox
