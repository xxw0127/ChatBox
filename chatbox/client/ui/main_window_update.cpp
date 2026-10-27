#include "ui/main_window_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QMessageBox>

#include "core/client_config.h"
#include "core/update_installer.h"
#include "core/update_service.h"
#include "ui/update_dialog.h"

using chatbox::client::UpdateService;

void MainWindow::checkForUpdates() {
    if (update_check_started_) {
        return;
    }
    update_check_started_ = true;
    const QString previous_result = chatbox::client::consumeUpdateResult();
    if (!previous_result.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("更新失败"),
                             QStringLiteral("上次更新未完成：%1").arg(previous_result));
    }
    if (chatbox::client::consumeUpdateInterrupted()) {
        QMessageBox::warning(
            this, QStringLiteral("更新被中断"),
            QStringLiteral("上次更新在替换文件时被中断，当前程序可能不完整。"
                           "如果启动或登录异常，请重新安装客户端。"));
    }
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
        connect(update_dialog_, &UpdateDialog::cancelRequested, this,
                &MainWindow::onUpdateCancelled);
        connect(update_dialog_, &QDialog::rejected, this,
                &MainWindow::onUpdateDialogRejected);
    }
    update_dialog_->show();
    update_dialog_->raise();
    update_dialog_->activateWindow();
}

void MainWindow::onUpdateRequested() {
    if (update_service_ == nullptr || update_dialog_ == nullptr ||
        update_download_active_) {
        return;
    }
    update_download_active_ = true;
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
    if (!update_download_active_) {
        return;
    }
    update_download_active_ = false;
    downloaded_update_zip_ = zip_path;
    installDownloadedUpdate();
}

void MainWindow::onUpdateFailed(const QString& reason) {
    update_download_active_ = false;
    if (update_dialog_ != nullptr) {
        update_dialog_->setBusy(false);
    }
    setStatus(QStringLiteral("更新失败: %1").arg(reason), true);
    qWarning("update download failed: %s", qPrintable(reason));
}

void MainWindow::onUpdateCancelled() {
    if (!update_download_active_) {
        return;
    }
    update_download_active_ = false;
    if (update_service_ != nullptr) {
        update_service_->cancel();
    }
    if (update_dialog_ != nullptr) {
        update_dialog_->setBusy(false);
    }
    setStatus(QStringLiteral("已取消更新"));
}

void MainWindow::onUpdateDialogRejected() {
    // Closing the prompt with the title-bar X must not leave a background
    // download that later replaces the client behind the user's back.
    if (update_download_active_) {
        onUpdateCancelled();
    }
}

void MainWindow::installDownloadedUpdate() {
    if (update_dialog_ == nullptr) {
        return;
    }
    QString error;
    if (!chatbox::client::canInstallUpdates(&error)) {
        update_dialog_->setBusy(false);
        QMessageBox::warning(this, QStringLiteral("无法更新"), error);
        return;
    }
    const QString staging =
        QDir::tempPath() + QStringLiteral("/ChatBox/staging-") +
        pending_update_.version;
    QDir(staging).removeRecursively();
    QString extracted_root;
    if (!chatbox::client::extractUpdateZip(downloaded_update_zip_, staging,
                                           &extracted_root, &error)) {
        update_dialog_->setBusy(false);
        QMessageBox::warning(this, QStringLiteral("更新包无效"), error);
        return;
    }
    const QString target = QCoreApplication::applicationDirPath();
    if (!chatbox::client::launchUpdateHelper(
            static_cast<quint64>(QCoreApplication::applicationPid()),
            extracted_root, target,
            QDir(target).filePath(QStringLiteral("chatbox.exe")), &error)) {
        update_dialog_->setBusy(false);
        QMessageBox::warning(this, QStringLiteral("无法启动更新"), error);
        return;
    }
    update_dialog_->hide();
    setStatus(QStringLiteral("正在安装更新，客户端将重新启动..."));
    QCoreApplication::quit();
}
