#ifndef CHATBOX_CLIENT_CORE_UPDATE_SERVICE_H
#define CHATBOX_CLIENT_CORE_UPDATE_SERVICE_H

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>

#include "core/update_manifest.h"

class QNetworkReply;

namespace chatbox {
namespace client {

// Asynchronous HTTPS transport for the update feed. The manifest parser is
// deliberately separate so its validation can be tested without networking.
class UpdateService : public QObject {
    Q_OBJECT

public:
    explicit UpdateService(QObject* parent = nullptr);
    ~UpdateService() override;

    void checkForUpdates(const QString& manifest_url, const QString& ca_path);
    void downloadUpdate(const UpdateManifest& manifest, const QString& target_path);
    void cancel();

signals:
    void updateAvailable(const chatbox::client::UpdateManifest& manifest);
    void upToDate();
    void checkFailed(const QString& reason);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString& zip_path);
    void downloadFailed(const QString& reason);

private:
    void failCheck(const QString& reason);
    void failDownload(const QString& reason);
    void cleanupDownload();

    QNetworkAccessManager* manager_ = nullptr;
    QPointer<QNetworkReply> manifest_reply_;
    QPointer<QNetworkReply> download_reply_;
    QFile download_file_;
    QCryptographicHash download_hash_{QCryptographicHash::Sha256};
    UpdateManifest active_manifest_;
    QString download_path_;
    qint64 download_received_ = 0;
    bool downloading_ = false;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_UPDATE_SERVICE_H
