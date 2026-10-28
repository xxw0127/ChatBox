#include "ui/main_window_support.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QUuid>

#include "core/app_settings.h"
#include "core/file_transfer_manager.h"
#include "core/file_thumbnail_cache.h"
#include "core/file_types.h"
#include "ui/file_manager_dialog.h"
#include "ui/image_preview_dialog.h"

using chatbox::client::FileTransferManager;
using chatbox::client::formatFileSize;

void MainWindow::startFileTransfer() {
    if (token_.isEmpty()) {
        return;
    }
    if (file_transfer_ == nullptr) {
        file_transfer_ = new FileTransferManager(this);
        file_transfer_->setHistoryStore(&history_store_);
        connect(file_transfer_, &FileTransferManager::connectionError, this,
                [this](const QString& message) {
                    setStatus(QString("文件连接: %1").arg(message), true);
                });
        connect(file_transfer_, &FileTransferManager::uploadQueued, this,
                [this](const QString& task_id, const QString& name,
                       quint64 size, chatbox::client::ChatContext context,
                       quint64 target_id, const QString& mime) {
                    if (message_store_.findByClientId(task_id) != nullptr ||
                        !message_store_.isCurrent(context, target_id)) {
                        return;
                    }
                    chatbox::client::ChatMessage card;
                    card.client_msg_id = task_id;
                    card.context = context;
                    card.target_id = target_id;
                    card.sender_id = user_id_;
                    card.sender_name = nickname_;
                    card.content = "[\u6587\u4ef6] " + name;
                    card.created_at_utc = QDateTime::currentDateTimeUtc();
                    card.outgoing = true;
                    card.status = chatbox::client::MessageStatus::kSending;
                    card.type = chatbox::client::MessageType::kFile;
                    card.file.name = name;
                    card.file.size = size;
                    card.file.mime = mime;
                    card.transfer_state = "queued";
                    message_store_.addPending(card);
                    renderCurrentConversation(ScrollMode::kBottom);
                });
        connect(file_transfer_, &FileTransferManager::uploadProgress, this,
                [this](const QString& task_id, quint64 sent, quint64 total) {
                    const int percent =
                        total == 0 ? 0
                                   : static_cast<int>(sent * 100 / total);
                    message_store_.updateFileTransfer(
                        task_id, sent >= total ? "done" : "uploading", sent);
                    if (percent != last_upload_percent_ &&
                        message_store_.findByClientId(task_id) != nullptr) {
                        last_upload_percent_ = percent;
                        renderCurrentConversation(ScrollMode::kPreserve);
                    }
                    setStatus(QString("上传中 %1% · %2 / %3")
                                  .arg(percent)
                                  .arg(formatFileSize(sent),
                                       formatFileSize(total)));
                });
        connect(file_transfer_, &FileTransferManager::uploadFinished, this,
                &MainWindow::onFileUploadFinished);
        connect(file_transfer_, &FileTransferManager::uploadFailed, this,
                &MainWindow::onFileUploadFailed);
        connect(file_transfer_, &FileTransferManager::uploadStateChanged, this,
                [this](const QString& task_id, const QString& state) {
                    if (state == "paused" || state == "failed") {
                        message_store_.updateFileTransfer(task_id, state, 0);
                    }
                });
        connect(file_transfer_, &FileTransferManager::downloadProgress, this,
                [this](quint64, quint64 received, quint64 total) {
                    const int percent =
                        total == 0 ? 0
                                   : static_cast<int>(received * 100 / total);
                    if (percent == last_download_percent_) {
                        return;
                    }
                    last_download_percent_ = percent;
                    setStatus(QString("下载中 %1% · %2 / %3")
                                  .arg(percent)
                                  .arg(formatFileSize(received),
                                       formatFileSize(total)));
                });
        connect(file_transfer_, &FileTransferManager::downloadFinished, this,
                [this](quint64, const QString& path, const QString& mime,
                       bool open_after) {
                    setStatus("文件已保存");
                    pending_download_mime_.clear();
                    if (!open_after) {
                        return;
                    }
                    if (mime.startsWith("image/")) {
                        auto* preview = new ImagePreviewDialog(path, this);
                        preview->setAttribute(Qt::WA_DeleteOnClose, true);
                        preview->show();
                    } else {
                        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
                    }
                });
        connect(file_transfer_, &FileTransferManager::downloadPaused, this,
                [this](quint64) {
                    setStatus(QStringLiteral("\u4e0b\u8f7d\u5df2\u6682\u505c"));
                });
        connect(file_transfer_, &FileTransferManager::downloadFailed, this,
                [this](quint64, const QString& message) {
                    setStatus(QString("下载失败: %1").arg(message), true);
                });
        connect(file_transfer_, &FileTransferManager::fileListReady, this,
                [this](int kind, quint64 target, quint32 total, quint64 used,
                       const QVector<chatbox::client::FileEntryView>& entries) {
                    const int current_kind =
                        context_type_ == ContextType::Room ? 1 : 2;
                    const quint64 current_target =
                        context_type_ == ContextType::Room
                            ? static_cast<quint64>(current_room_id_)
                            : current_peer_id_;
                    if (kind != current_kind || target != current_target) {
                        return;
                    }
                    if (file_manager_dialog_ != nullptr) {
                        file_manager_dialog_->setBusy(false);
                        file_manager_dialog_->setFiles(total, used, entries);
                    }
                });
        connect(file_transfer_, &FileTransferManager::fileOperationFailed, this,
                [this](const QString& message) {
                    if (file_manager_dialog_ != nullptr) {
                        file_manager_dialog_->setBusy(false);
                        file_manager_dialog_->setStatus(message, true);
                    }
                });
        connect(file_transfer_, &FileTransferManager::fileInfoReady, this,
                [this](const chatbox::client::FileEntryView& entry) {
                    chatbox::client::FileMeta meta;
                    meta.file_id = entry.file_id;
                    meta.name = entry.name;
                    meta.size = entry.size;
                    meta.mime = entry.mime;
                    meta.sha256 = entry.sha256;
                    meta.thumb_available = entry.thumb_available;
                    meta.width = entry.width;
                    meta.height = entry.height;
                    meta.duration_ms = entry.duration_ms;
                    if (message_store_.updateFileMetadata(entry.message_id,
                                                          meta)) {
                        renderCurrentConversation(ScrollMode::kPreserve);
                    }
                });
        connect(file_transfer_, &FileTransferManager::thumbnailReady, this,
                [this](quint64 file_id, const QByteArray& data) {
                    if (chatbox::client::saveThumbnail(file_id, data)) {
                        thumbnail_requests_.remove(file_id);
                        renderCurrentConversation(ScrollMode::kPreserve);
                    }
                });
        connect(file_transfer_, &FileTransferManager::fileDeleted, this,
                [this](quint64) {
                    if (file_manager_dialog_ != nullptr) {
                        file_transfer_->listFiles(
                            context_type_ == ContextType::Room ? 1 : 2,
                            context_type_ == ContextType::Room
                                ? static_cast<quint64>(current_room_id_)
                                : current_peer_id_);
                    }
                });
    }
    const QString ca_path =
        QCoreApplication::applicationDirPath() + "/certs/ca.crt";
    file_transfer_->start(host_, port_, use_tls_, ca_path, token_);
    file_transfer_->restorePending();
}

void MainWindow::chooseAndUploadFile() {
    const QString path = QFileDialog::getOpenFileName(this, "\u53d1\u9001\u6587\u4ef6");
    if (!path.isEmpty()) {
        uploadLocalFile(path);
    }
}

void MainWindow::uploadLocalFile(const QString& path) {
    if (context_type_ == ContextType::None) {
        setStatus("\u8bf7\u5148\u9009\u62e9\u4e00\u4e2a\u4f1a\u8bdd", true);
        return;
    }
    if (offline_mode_ || !online_) {
        setStatus("\u5f53\u524d\u79bb\u7ebf\uff0c\u4e0d\u80fd\u53d1\u9001\u6587\u4ef6", true);
        return;
    }
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        setStatus("\u6587\u4ef6\u4e0d\u5b58\u5728", true);
        return;
    }
    if (static_cast<quint64>(info.size()) > chatbox::kMaxFileSize) {
        setStatus("\u5355\u4e2a\u6587\u4ef6\u4e0d\u80fd\u8d85\u8fc7 2GB", true);
        return;
    }
    const auto context = context_type_ == ContextType::Room
                             ? chatbox::client::ChatContext::kRoom
                             : chatbox::client::ChatContext::kPrivate;
    const quint64 target =
        context_type_ == ContextType::Room ? static_cast<quint64>(current_room_id_)
                                           : current_peer_id_;
    const int kind = context_type_ == ContextType::Room ? 1 : 2;
    const QString mime = QMimeDatabase().mimeTypeForFile(info).name();
    pending_upload_task_id_ =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    pending_upload_kind_ = static_cast<quint64>(kind);
    pending_upload_target_ = target;
    pending_upload_name_ = info.fileName();
    pending_upload_size_ = info.size();
    pending_upload_mime_ = mime;

    chatbox::client::ChatMessage card;
    card.client_msg_id = pending_upload_task_id_;
    card.context = context;
    card.target_id = target;
    card.sender_id = user_id_;
    card.sender_name = nickname_;
    card.content = "[\u6587\u4ef6] " + pending_upload_name_;
    card.created_at_utc = QDateTime::currentDateTimeUtc();
    card.outgoing = true;
    card.status = chatbox::client::MessageStatus::kSending;
    card.type = chatbox::client::MessageType::kFile;
    card.file.name = pending_upload_name_;
    card.file.size = static_cast<quint64>(pending_upload_size_);
    card.file.mime = mime;
    card.transfer_state = "queued";
    if (message_store_.isCurrent(context, target)) {
        message_store_.addPending(card);
        renderCurrentConversation(ScrollMode::kBottom);
    }
    last_upload_percent_ = -1;
    setStatus("\u6b63\u5728\u4e0a\u4f20\u6587\u4ef6...");
    if (file_transfer_ == nullptr) {
        startFileTransfer();
    }
    file_transfer_->enqueueUpload(path, kind, target, pending_upload_name_,
                                  mime, context, pending_upload_task_id_);
}

void MainWindow::handleDroppedFiles(const QStringList& paths) {
    for (const QString& path : paths) {
        uploadLocalFile(path);
    }
}

void MainWindow::uploadPastedImage(const QImage& image) {
    if (image.isNull()) {
        return;
    }
    QString directory = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation);
    if (directory.isEmpty()) {
        directory = QDir::tempPath();
    }
    directory += "/ChatBox";
    QDir().mkpath(directory);
    const QString path =
        QDir(directory).filePath("paste-" +
                                 QUuid::createUuid().toString(QUuid::WithoutBraces) +
                                 ".png");
    if (image.save(path, "PNG")) {
        uploadLocalFile(path);
    }
}

void MainWindow::onFileUploadFinished(
    const QString& task_id, quint64 message_id, quint64 file_id,
    const QString& created_at, const QString& name, quint64 size,
    const QString& mime, chatbox::client::ChatContext context,
    quint64 target_id) {
    const QDateTime created = chatbox::client::parseServerUtc(created_at);
    bool confirmed = message_store_.confirmFileUpload(task_id, message_id,
                                                      file_id, created);
    if (!confirmed) {
        chatbox::client::ChatMessage message;
        message.message_id = message_id;
        message.context = pending_upload_kind_ == 1
                              ? chatbox::client::ChatContext::kRoom
                              : chatbox::client::ChatContext::kPrivate;
        message.target_id = pending_upload_target_;
        message.sender_id = user_id_;
        message.sender_name = nickname_;
        message.content = "[文件] " + pending_upload_name_;
        message.created_at_utc = created;
        message.outgoing = true;
        message.status = chatbox::client::MessageStatus::kSent;
        message.type = chatbox::client::MessageType::kFile;
        message.file.file_id = file_id;
        message.file.name = name;
        message.file.size = size;
        message.file.mime = mime;
        const bool current = message_store_.isCurrent(message.context,
                                                      message.target_id);
        if (current) {
            message_store_.appendIncoming(message);
            renderCurrentConversation(ScrollMode::kBottom);
        } else if (history_store_.isOpen()) {
            history_store_.saveMessage(message);
        }
    } else if (message_store_.isCurrent(context, target_id)) {
        renderCurrentConversation(ScrollMode::kBottom);
    }
    setStatus("文件已发送");
    if (file_transfer_ != nullptr && file_id != 0) {
        file_transfer_->requestFileInfo(file_id);
    }
    if (file_manager_dialog_ != nullptr && file_transfer_ != nullptr) {
        file_transfer_->listFiles(static_cast<int>(pending_upload_kind_),
                                  pending_upload_target_);
    }
    pending_upload_task_id_.clear();
}

void MainWindow::onFileUploadFailed(const QString& task_id,
                                    const QString& message) {
    message_store_.updateFileTransfer(task_id, "failed", 0);
    if (message_store_.findByClientId(task_id) != nullptr) {
        renderCurrentConversation(ScrollMode::kPreserve);
    }
    setStatus(QString("文件上传失败: %1").arg(message), true);
}

void MainWindow::downloadFileTo(quint64 file_id, const QString& path,
                                const QString& name, const QString& mime,
                                quint64 message_id) {
    if (file_transfer_ == nullptr) {
        startFileTransfer();
    }
    if (file_transfer_ == nullptr) {
        setStatus("文件连接尚未准备好", true);
        return;
    }
    const auto context = context_type_ == ContextType::Room
                             ? chatbox::client::ChatContext::kRoom
                             : chatbox::client::ChatContext::kPrivate;
    const quint64 target =
        context_type_ == ContextType::Room ? static_cast<quint64>(current_room_id_)
                                           : current_peer_id_;
    pending_download_mime_ = mime;
    last_download_percent_ = -1;
    setStatus("正在下载文件...");
    file_transfer_->requestDownload(file_id, context, target, message_id, name,
                                    mime, path, true);
}

QString MainWindow::uniqueDownloadPath(const QString& name) const {
    QString directory = chatbox::client::fileSaveDirectory();
    QString error;
    if (!chatbox::client::validateFileSaveDirectory(directory, &error)) {
        directory = chatbox::client::defaultFileSaveDirectory();
        QDir().mkpath(directory);
    }
    return chatbox::client::uniqueFilePath(directory, name);
}

void MainWindow::requestMissingThumbnails() {
    if (file_transfer_ == nullptr) {
        return;
    }
    for (const chatbox::client::ChatMessage& message :
         message_store_.messages()) {
        if (message.type != chatbox::client::MessageType::kFile ||
            !message.file.thumb_available || message.file.file_id == 0 ||
            !message.file.mime.startsWith(QStringLiteral("image/"))) {
            continue;
        }
        if (QFile::exists(chatbox::client::thumbnailCachePath(
                message.file.file_id))) {
            continue;
        }
        if (thumbnail_requests_.contains(message.file.file_id)) {
            continue;
        }
        thumbnail_requests_.insert(message.file.file_id);
        file_transfer_->requestThumbnail(message.file.file_id);
    }
}

void MainWindow::openFileManager() {
    if (context_type_ == ContextType::None) {
        setStatus("请先选择一个会话", true);
        return;
    }
    if (offline_mode_) {
        setStatus("离线模式下不能管理文件", true);
        return;
    }
    const int kind = context_type_ == ContextType::Room ? 1 : 2;
    const quint64 target =
        context_type_ == ContextType::Room ? static_cast<quint64>(current_room_id_)
                                           : current_peer_id_;
    if (file_manager_dialog_ != nullptr) {
        file_manager_dialog_->close();
        file_manager_dialog_->deleteLater();
        file_manager_dialog_ = nullptr;
    }
    auto* dialog = new FileManagerDialog(kind, target, true, this);
    file_manager_dialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (file_manager_dialog_ == dialog) {
            file_manager_dialog_ = nullptr;
        }
    });
    connect(file_manager_dialog_, &FileManagerDialog::uploadRequested, this,
            &MainWindow::chooseAndUploadFile);
    connect(file_manager_dialog_, &FileManagerDialog::downloadRequested, this,
            [this](quint64 file_id, const QString& name, const QString& mime) {
                downloadFileTo(file_id, uniqueDownloadPath(name), name, mime, 0);
            });
    connect(file_manager_dialog_,
            &FileManagerDialog::downloadToggleRequested, this,
            [this](quint64 file_id) {
                if (file_transfer_ != nullptr) {
                    file_transfer_->toggleDownload(file_id);
                    file_manager_dialog_->setStatus("\u4e0b\u8f7d\u5df2\u6682\u505c/\u7ee7\u7eed");
                }
            });
    connect(file_manager_dialog_, &FileManagerDialog::deleteRequested, this,
            [this](quint64 file_id) {
                if (file_transfer_ != nullptr) {
                    file_transfer_->deleteFile(file_id);
                    file_manager_dialog_->setStatus("正在删除...");
                }
            });
    connect(file_manager_dialog_, &FileManagerDialog::refreshRequested, this,
            [this, kind, target] {
                if (file_transfer_ != nullptr) {
                    file_transfer_->listFiles(kind, target);
                }
            });
    file_manager_dialog_->setBusy(true);
    file_manager_dialog_->show();
    if (file_transfer_ == nullptr) {
        startFileTransfer();
    }
    if (file_transfer_ != nullptr) {
        file_transfer_->listFiles(kind, target);
    }
}

void MainWindow::openFileMessage(quint64 message_id) {
    const chatbox::client::ChatMessage* message =
        message_store_.findMessage(message_id);
    if (message == nullptr || message->file.file_id == 0) {
        setStatus("文件消息不存在", true);
        return;
    }
    downloadFileTo(message->file.file_id,
                   uniqueDownloadPath(message->file.name), message->file.name,
                   message->file.mime, message_id);
}
