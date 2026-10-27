#include "core/update_service.h"

#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QUrl>

#include "core/client_config.h"
#include "core/update_manifest.h"

namespace chatbox {
namespace client {

namespace {

constexpr qint64 kMaxManifestBytes = 64 * 1024;
constexpr qint64 kMaxUpdateBytes = 200 * 1024 * 1024;

QNetworkRequest makeRequest(const QUrl& url, const QString& ca_path, bool* ca_ok) {
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setMaximumRedirectsAllowed(3);
    // Idle timeout: a stalled TLS transfer must not leave the dialog busy
    // forever. Large packages still pass as long as data keeps arriving.
    request.setTransferTimeout(60000);
    const QList<QSslCertificate> certificates = QSslCertificate::fromPath(ca_path);
    if (certificates.isEmpty()) {
        *ca_ok = false;
        return request;
    }
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    configuration.setCaCertificates(certificates);
    request.setSslConfiguration(configuration);
    *ca_ok = true;
    return request;
}

bool isTrustedManifestUrl(const QUrl& url) {
    return url.isValid() && url.scheme() == QStringLiteral("https") &&
           url.host() == QString::fromLatin1(kServerHost);
}

}  // namespace

UpdateService::UpdateService(QObject* parent)
    : QObject(parent), manager_(new QNetworkAccessManager(this)) {}

UpdateService::~UpdateService() {
    cancel();
}

void UpdateService::checkForUpdates(const QString& manifest_url,
                                    const QString& ca_path) {
    if (manifest_reply_ != nullptr || download_reply_ != nullptr) {
        return;
    }
    const QUrl url(manifest_url);
    if (!isTrustedManifestUrl(url)) {
        failCheck(QStringLiteral("更新地址不安全"));
        return;
    }
    bool ca_ok = false;
    const QNetworkRequest request = makeRequest(url, ca_path, &ca_ok);
    if (!ca_ok) {
        failCheck(QStringLiteral("找不到更新证书"));
        return;
    }
    manifest_reply_ = manager_->get(request);
    QNetworkReply* reply = manifest_reply_;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply](qint64 received, qint64) {
                if (reply != manifest_reply_ || received <= kMaxManifestBytes) {
                    return;
                }
                reply->abort();
                failCheck(QStringLiteral("更新清单过大"));
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != manifest_reply_) {
            reply->deleteLater();
            return;
        }
        const QByteArray body = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError;
        const QUrl final_url = reply->url();
        reply->deleteLater();
        manifest_reply_ = nullptr;
        if (!ok) {
            failCheck(QStringLiteral("检查更新失败: %1").arg(reply->errorString()));
            return;
        }
        if (!isTrustedManifestUrl(final_url)) {
            failCheck(QStringLiteral("更新地址不安全"));
            return;
        }
        if (body.size() > kMaxManifestBytes) {
            failCheck(QStringLiteral("更新清单过大"));
            return;
        }
        QString error;
        const UpdateManifest manifest = parseUpdateManifest(body, &error);
        if (!manifest.valid) {
            failCheck(error);
            return;
        }
        if (!isNewerVersion(QCoreApplication::applicationVersion(),
                            manifest.version)) {
            emit upToDate();
            return;
        }
        emit updateAvailable(manifest);
    });
}

void UpdateService::downloadUpdate(const UpdateManifest& manifest,
                                   const QString& target_path) {
    if (downloading_ || download_reply_ != nullptr || !manifest.valid ||
        target_path.isEmpty()) {
        return;
    }
    download_path_ = target_path;
    active_manifest_ = manifest;
    download_received_ = 0;
    download_hash_.reset();
    download_file_.setFileName(target_path);
    if (!download_file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failDownload(QStringLiteral("无法写入更新临时文件"));
        return;
    }
    bool ca_ok = false;
    const QNetworkRequest request =
        makeRequest(manifest.url, QCoreApplication::applicationDirPath() +
                                      QStringLiteral("/certs/ca.crt"),
                    &ca_ok);
    if (!ca_ok) {
        failDownload(QStringLiteral("找不到更新证书"));
        return;
    }
    downloading_ = true;
    download_reply_ = manager_->get(request);
    QNetworkReply* reply = download_reply_;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != download_reply_) {
            return;
        }
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty()) {
            return;
        }
        if (download_received_ + chunk.size() > kMaxUpdateBytes) {
            download_reply_ = nullptr;
            reply->abort();
            failDownload(QStringLiteral("更新包过大"));
            return;
        }
        if (download_file_.write(chunk) != chunk.size()) {
            download_reply_ = nullptr;
            reply->abort();
            failDownload(QStringLiteral("写入更新包失败"));
            return;
        }
        download_hash_.addData(chunk);
        download_received_ += chunk.size();
        emit downloadProgress(
            download_received_,
            reply->header(QNetworkRequest::ContentLengthHeader).toLongLong());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != download_reply_) {
            reply->deleteLater();
            return;
        }
        const QString network_error =
            reply->error() == QNetworkReply::NoError ? QString()
                                                     : reply->errorString();
        const QUrl final_url = reply->url();
        const QByteArray rest = reply->readAll();
        reply->deleteLater();
        download_reply_ = nullptr;
        if (!network_error.isEmpty()) {
            failDownload(QStringLiteral("下载更新失败: %1").arg(network_error));
            return;
        }
        if (!isTrustedManifestUrl(final_url)) {
            failDownload(QStringLiteral("更新地址不安全"));
            return;
        }
        if (!rest.isEmpty()) {
            if (download_file_.write(rest) != rest.size()) {
                failDownload(QStringLiteral("写入更新包失败"));
                return;
            }
            download_hash_.addData(rest);
            download_received_ += rest.size();
        }
        download_file_.flush();
        download_file_.close();
        if (download_hash_.result().toHex() != active_manifest_.sha256) {
            failDownload(QStringLiteral("更新包校验失败"));
            return;
        }
        downloading_ = false;
        emit downloadFinished(download_path_);
    });
}

void UpdateService::cancel() {
    // abort() can emit finished() synchronously; the finished handler clears
    // the QPointer. Capture a raw pointer and clear the member first so this
    // function never dereferences a pointer the handler just nulled.
    if (manifest_reply_ != nullptr) {
        QNetworkReply* reply = manifest_reply_;
        manifest_reply_ = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    if (download_reply_ != nullptr) {
        QNetworkReply* reply = download_reply_;
        download_reply_ = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    cleanupDownload();
}

void UpdateService::failCheck(const QString& reason) {
    emit checkFailed(reason);
}

void UpdateService::failDownload(const QString& reason) {
    cleanupDownload();
    emit downloadFailed(reason);
}

void UpdateService::cleanupDownload() {
    downloading_ = false;
    if (download_file_.isOpen()) {
        download_file_.close();
    }
    if (!download_path_.isEmpty()) {
        QFile::remove(download_path_);
    }
    download_path_.clear();
    download_received_ = 0;
    download_hash_.reset();
}

}  // namespace client
}  // namespace chatbox
