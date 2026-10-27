#include "ui/main_window_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>

#include "core/client_config.h"
#include "core/update_service.h"
#include "ui/update_dialog.h"

using chatbox::client::UpdateService;

void MainWindow::checkForUpdates() {
    if (update_check_started_) {
        return;
    }
    update_check_started_ = true;
    update_service_ = new UpdateService(this);
    connect(update_service_, &UpdateService::updateAvailable, this,
            &MainWindow::showUpdateAvailable);
    connect(update_service_, &UpdateService::checkFailed, this,
            [](const QString& reason) { qInfo("update check failed: %s",
                                              qPrintable(reason)); });
    connect(update_service_, &UpdateService::downloadProgress, this,
            &MainWindow::onUpdateProgress);
    connect(update_service_, &UpdateService::downloadFinished, this,
            &MainWindow::onUpdateDownloaded);
    connect(update_service_, &UpdateService::downloadFailed, this,
            &MainWindow::onUpdateFailed);
    const QString ca_path = QCoreApplication::applicationDirPath() +
                            QStringLiteral("/certs/ca.crt");
    update_service_->checkForUpdates(
        QString::fromLatin1(chatbox::client::kUpdateManifestUrl), ca_path);
}

void MainWindow::showUpdateAvailable(
    const chatbox::client::UpdateManifest& manifest) {
    pending_update_ = manifest;
    if (update_dialog_ == nullptr) {
        update_dialog_ = new UpdateDialog(manifest, this);
        connect(update_dialog_, &UpdateDialog::updateRequested, this,
                &MainWindow::onUpdateRequested);
    }
    update_dialog_->show();
    update_dialog_->raise();
    update_dialog_->activateWindow();
}

void MainWindow::onUpdateRequested() {
    if (update_service_ == nullptr || update_dialog_ == nullptr) {
        return;
    }
    const QString directory =
        QDir::tempPath() + QStringLiteral("/ChatBox/update");
    QDir().mkpath(directory);
    downloaded_update_zip_ = QDir(directory).filePath(
        QStringLiteral("chatbox-%1.zip").arg(pending_update_.version));
    update_dialog_->setBusy(true);
    update_service_->downloadUpdate(pending_update_, downloaded_update_zip_);
}

void MainWindow::onUpdateProgress(qint64 received, qint64 total) {
    if (update_dialog_ != nullptr) {
        update_dialog_->setDownloadProgress(received, total);
    }
}

void MainWindow::onUpdateDownloaded(const QString& zip_path) {
    downloaded_update_zip_ = zip_path;
    installDownloadedUpdate();
}

void MainWindow::onUpdateFailed(const QString& reason) {
    if (update_dialog_ != nullptr) {
        update_dialog_->setBusy(false);
    }
    setStatus(QStringLiteral("更新失败: %1").arg(reason), true);
}

void MainWindow::installDownloadedUpdate() {
    // Implemented by the updater helper task; keep the download UI usable in
    // the intermediate build while that helper is wired in.
    qInfo("update downloaded: %s", qPrintable(downloaded_update_zip_));
    if (update_dialog_ != nullptr) {
        update_dialog_->setBusy(false);
    }
}
