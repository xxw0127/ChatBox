#include "core/file_transfer_manager.h"

#include <QDateTime>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>

#include "core/file_transfer_client.h"

namespace chatbox {
namespace client {

namespace {

constexpr quint64 kPersistStep = 1024 * 1024;

HistoryStore::PendingUpload toPending(const QString& task_id,
                                      const QString& upload_id, int kind,
                                      quint64 target_id, ChatContext context,
                                      const QString& source_path,
                                      const QString& name, const QString& mime,
                                      quint64 size, quint64 offset,
                                      const QString& state) {
    HistoryStore::PendingUpload pending;
    pending.upload_id = upload_id.isEmpty() ? task_id : upload_id;
    pending.context = context;
    pending.target_id = target_id;
    pending.source_path = source_path;
    pending.name = name;
    pending.size = size;
    pending.mime = mime;
    pending.offset = offset;
    pending.status = state;
    return pending;
}

}  // namespace

FileTransferManager::FileTransferManager(QObject* parent) : QObject(parent) {
    upload_client_ = new FileTransferClient(this);
    download_client_ = new FileTransferClient(this);
    keepalive_ = new QTimer(this);
    keepalive_->setInterval(30000);
    connect(keepalive_, &QTimer::timeout, this, [this] {
        if (!started_) {
            return;
        }
        upload_client_->sendKeepAlive();
        download_client_->sendKeepAlive();
    });

    connect(upload_client_, &FileTransferClient::uploadInitialized, this,
            [this](const QString& upload_id, qint64 offset) {
                UploadTask* task = findUpload(active_upload_id_);
                if (task == nullptr) {
                    return;
                }
                if (history_ != nullptr && task->upload_id.isEmpty() &&
                    task->task_id != upload_id) {
                    history_->removePendingUpload(task->task_id);
                }
                task->upload_id = upload_id;
                task->offset = static_cast<quint64>(offset);
                task->state = QStringLiteral("uploading");
                persistUpload(*task);
                emit uploadStateChanged(task->task_id, task->state);
            });
    connect(upload_client_, &FileTransferClient::uploadProgress, this,
            [this](qint64 sent, qint64 total) {
                UploadTask* task = findUpload(active_upload_id_);
                if (task == nullptr) {
                    return;
                }
                task->offset = static_cast<quint64>(sent);
                task->size = static_cast<quint64>(total);
                if (sent - static_cast<qint64>(task->persisted_offset) >=
                        static_cast<qint64>(kPersistStep) ||
                    sent == total) {
                    task->persisted_offset = task->offset;
                    persistUpload(*task);
                }
                emit uploadProgress(task->task_id, task->offset, task->size);
            });
    connect(upload_client_, &FileTransferClient::uploadFinished, this,
            [this](quint64 message_id, quint64 file_id,
                   const QString& created_at) {
                UploadTask* task = findUpload(active_upload_id_);
                if (task == nullptr) {
                    return;
                }
                const QString task_id = task->task_id;
                if (history_ != nullptr) {
                    if (!task->upload_id.isEmpty()) {
                        history_->removePendingUpload(task->upload_id);
                    }
                    if (task->task_id != task->upload_id) {
                        history_->removePendingUpload(task->task_id);
                    }
                }
                task->state = QStringLiteral("done");
                emit uploadStateChanged(task_id, task->state);
                active_upload_id_.clear();
                emit uploadFinished(task_id, message_id, file_id, created_at,
                                    task->name, task->size, task->mime,
                                    task->context, task->target_id);
                startNextUpload();
            });
    connect(upload_client_, &FileTransferClient::uploadFailed, this,
            [this](const QString& message) {
                UploadTask* task = findUpload(active_upload_id_);
                if (task == nullptr) {
                    return;
                }
                if (message.contains(QStringLiteral("upload not found")) ||
                    message.contains(QStringLiteral("upload is closed"))) {
                    // The server lost the temporary upload (e.g. test DB reset).
                    // Restart it from byte zero instead of failing forever.
                    if (history_ != nullptr) {
                        if (!task->upload_id.isEmpty()) {
                            history_->removePendingUpload(task->upload_id);
                        }
                        if (task->task_id != task->upload_id) {
                            history_->removePendingUpload(task->task_id);
                        }
                    }
                    task->upload_id.clear();
                    task->offset = 0;
                    task->persisted_offset = 0;
                    task->state = QStringLiteral("queued");
                    persistUpload(*task);
                    emit uploadStateChanged(task->task_id, task->state);
                    active_upload_id_.clear();
                    startNextUpload();
                    return;
                }
                task->state = QStringLiteral("failed");
                persistUpload(*task);
                emit uploadStateChanged(task->task_id, task->state);
                emit uploadFailed(task->task_id, message);
                active_upload_id_.clear();
                startNextUpload();
            });
    connect(upload_client_, &FileTransferClient::connectionError, this,
            [this](const QString& message) {
                UploadTask* task = findUpload(active_upload_id_);
                if (task == nullptr) {
                    return;
                }
                task->state = QStringLiteral("queued");
                persistUpload(*task);
                emit uploadStateChanged(task->task_id, task->state);
                emit uploadFailed(task->task_id, message);
                active_upload_id_.clear();
            });
    connect(upload_client_, &FileTransferClient::authenticated, this,
            [this](quint64) { startNextUpload(); });

    connect(download_client_, &FileTransferClient::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                DownloadTask* task = findDownload(active_download_id_);
                if (task == nullptr) {
                    return;
                }
                task->received = static_cast<quint64>(received);
                task->total = static_cast<quint64>(total);
                const bool persist =
                    task->received - task->persisted_received >= kPersistStep ||
                    task->received >= task->total;
                if (history_ != nullptr && persist) {
                    task->persisted_received = task->received;
                    history_->updateFileTask(
                        task->file_id, task->context, task->target_id,
                        QStringLiteral("downloading"), task->received,
                        task->total);
                }
                emit downloadProgress(task->file_id, task->received,
                                      task->total);
            });
    connect(download_client_, &FileTransferClient::downloadFinished, this,
            [this](quint64 file_id, const QString& save_path) {
                DownloadTask* task = findDownload(file_id);
                if (task == nullptr) {
                    return;
                }
                const bool open_after = task->open_after;
                const QString mime = task->mime;
                if (history_ != nullptr) {
                    history_->removeFileTask(task->file_id, task->context,
                                             task->target_id);
                }
                for (int i = 0; i < downloads_.size(); ++i) {
                    if (downloads_[i].file_id == file_id &&
                        downloads_[i].save_path == save_path) {
                        downloads_.removeAt(i);
                        break;
                    }
                }
                active_download_id_ = 0;
                emit downloadFinished(file_id, save_path, mime, open_after);
                startNextDownload();
            });
    connect(download_client_, &FileTransferClient::downloadFailed, this,
            [this](quint64 file_id, const QString& message) {
                DownloadTask* task = findDownload(file_id);
                if (task != nullptr && history_ != nullptr) {
                    history_->updateFileTask(task->file_id, task->context,
                                             task->target_id,
                                             QStringLiteral("paused"),
                                             task->received, task->total);
                }
                active_download_id_ = 0;
                emit downloadFailed(file_id, message);
                startNextDownload();
            });
    connect(download_client_, &FileTransferClient::authenticated, this,
            [this](quint64) { startNextDownload(); });

    connect(upload_client_, &FileTransferClient::connectionError, this,
            &FileTransferManager::connectionError);
    connect(upload_client_, &FileTransferClient::fileOperationFailed, this,
            &FileTransferManager::fileOperationFailed);
    connect(download_client_, &FileTransferClient::connectionError, this,
            &FileTransferManager::connectionError);
    connect(upload_client_, &FileTransferClient::connectionError, this,
            [this](const QString&) {
                QTimer::singleShot(1500, this,
                                   [this] { reconnectIdle(); });
            });
    connect(download_client_, &FileTransferClient::connectionError, this,
            [this](const QString&) {
                QTimer::singleShot(1500, this,
                                   [this] { reconnectIdle(); });
            });
    connect(upload_client_, &FileTransferClient::fileListReady, this,
            &FileTransferManager::fileListReady);
    connect(upload_client_, &FileTransferClient::fileInfoReady, this,
            &FileTransferManager::fileInfoReady);
    connect(upload_client_, &FileTransferClient::fileDeleted, this,
            &FileTransferManager::fileDeleted);
    connect(upload_client_, &FileTransferClient::thumbnailReady, this,
            &FileTransferManager::thumbnailReady);
}

FileTransferManager::~FileTransferManager() = default;

void FileTransferManager::setHistoryStore(HistoryStore* store) {
    history_ = store;
}

void FileTransferManager::start(const QString& host, quint16 port, bool tls,
                                const QString& ca_path, const QString& token) {
    started_ = true;
    host_ = host;
    port_ = port;
    tls_ = tls;
    ca_path_ = ca_path;
    token_ = token;
    upload_client_->start(host, port, tls, ca_path, token);
    download_client_->start(host, port, tls, ca_path, token);
    keepalive_->start();
}

bool FileTransferManager::isReady() const {
    return upload_client_ != nullptr && upload_client_->isReady();
}

void FileTransferManager::stop() {
    started_ = false;
    if (keepalive_ != nullptr) {
        keepalive_->stop();
    }
    active_upload_id_.clear();
    active_download_id_ = 0;
    uploads_.clear();
    downloads_.clear();
    restored_ = false;
    upload_client_->stop();
    download_client_->stop();
}

QString FileTransferManager::enqueueUpload(const QString& local_path, int kind,
                                           quint64 target_id,
                                           const QString& display_name,
                                           const QString& mime,
                                           ChatContext context,
                                           const QString& task_id) {
    UploadTask task;
    task.task_id = task_id.isEmpty()
                       ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                       : task_id;
    task.kind = kind;
    task.target_id = target_id;
    task.context = context;
    task.source_path = local_path;
    task.name = display_name;
    task.mime = mime;
    task.size = static_cast<quint64>(QFileInfo(local_path).size());
    task.state = QStringLiteral("queued");
    if (history_ != nullptr) {
        history_->savePendingUpload(toPending(
            task.task_id, QString(), kind, target_id, context, local_path,
            display_name, mime, task.size, 0, task.state));
    }
    uploads_.append(task);
    emit uploadQueued(task.task_id, task.name, task.size, context, target_id,
                      mime);
    startNextUpload();
    return task.task_id;
}

void FileTransferManager::pauseUpload(const QString& task_id) {
    if (task_id == active_upload_id_) {
        upload_client_->pauseUpload();
        UploadTask* task = findUpload(task_id);
        if (task != nullptr) {
            task->state = QStringLiteral("paused");
            persistUpload(*task);
            emit uploadStateChanged(task_id, task->state);
        }
        return;
    }
    UploadTask* task = findUpload(task_id);
    if (task != nullptr && task->state == QStringLiteral("queued")) {
        task->state = QStringLiteral("paused");
        persistUpload(*task);
        emit uploadStateChanged(task_id, task->state);
    }
}

void FileTransferManager::resumeUpload(const QString& task_id) {
    UploadTask* task = findUpload(task_id);
    if (task == nullptr) {
        return;
    }
    if (task_id == active_upload_id_) {
        task->state = QStringLiteral("uploading");
        persistUpload(*task);
        emit uploadStateChanged(task_id, task->state);
        upload_client_->resumeUpload(task->upload_id, task->source_path,
                                     task->kind, task->target_id, task->name,
                                     task->mime, task->size);
        return;
    }
    task->state = QStringLiteral("queued");
    persistUpload(*task);
    emit uploadStateChanged(task_id, task->state);
    startNextUpload();
}

void FileTransferManager::cancelUpload(const QString& task_id) {
    UploadTask* task = findUpload(task_id);
    if (task == nullptr) {
        return;
    }
    if (task_id == active_upload_id_) {
        upload_client_->cancelUpload();
        active_upload_id_.clear();
    }
    if (history_ != nullptr) {
        if (!task->upload_id.isEmpty()) {
            history_->removePendingUpload(task->upload_id);
        }
        if (task->task_id != task->upload_id) {
            history_->removePendingUpload(task->task_id);
        }
    }
    task->state = QStringLiteral("cancelled");
    emit uploadStateChanged(task_id, task->state);
    startNextUpload();
}

void FileTransferManager::requestDownload(
    quint64 file_id, ChatContext context, quint64 target_id,
    quint64 message_id, const QString& name, const QString& mime,
    const QString& save_path, bool open_after) {
    if (file_id == 0 || save_path.isEmpty()) {
        return;
    }
    DownloadTask task;
    task.file_id = file_id;
    task.context = context;
    task.target_id = target_id;
    task.message_id = message_id;
    task.name = name;
    task.mime = mime;
    task.save_path = save_path;
    task.open_after = open_after;
    task.state = QStringLiteral("queued");
    if (history_ != nullptr) {
        HistoryStore::FileTask stored;
        stored.file_id = file_id;
        stored.context = context;
        stored.target_id = target_id;
        stored.message_id = message_id;
        stored.name = name;
        stored.mime = mime;
        stored.save_path = save_path;
        stored.state = QStringLiteral("queued");
        history_->saveFileTask(stored);
    }
    downloads_.append(task);
    startNextDownload();
}

void FileTransferManager::toggleDownload(quint64 file_id) {
    DownloadTask* task = findDownload(file_id);
    if (task == nullptr) {
        return;
    }
    if (active_download_id_ == file_id) {
        if (task->state == QStringLiteral("paused")) {
            task->state = QStringLiteral("downloading");
            download_client_->resumeDownload();
        } else {
            download_client_->pauseDownload();
            task->state = QStringLiteral("paused");
            emit downloadPaused(file_id);
        }
        if (history_ != nullptr) {
            history_->updateFileTask(task->file_id, task->context,
                                     task->target_id, task->state,
                                     task->received, task->total);
        }
        return;
    }
    task->state = QStringLiteral("queued");
    if (history_ != nullptr) {
        history_->updateFileTask(task->file_id, task->context, task->target_id,
                                 QStringLiteral("queued"), task->received,
                                 task->total);
    }
    startNextDownload();
}

void FileTransferManager::restorePending() {
    if (history_ == nullptr || restored_) {
        return;
    }
    restored_ = true;
    const QVector<HistoryStore::PendingUpload> pending =
        history_->pendingUploads();
    for (const HistoryStore::PendingUpload& item : pending) {
        UploadTask task;
        task.task_id = item.upload_id;
        if (item.upload_id.contains('-')) {
            task.upload_id.clear();
        } else {
            task.upload_id = item.upload_id;
        }
        task.kind = item.context == ChatContext::kRoom ? 1 : 2;
        task.target_id = item.target_id;
        task.context = item.context;
        task.source_path = item.source_path;
        task.name = item.name;
        task.mime = item.mime;
        task.size = item.size;
        task.offset = item.offset;
        task.persisted_offset = item.offset;
        task.state = QStringLiteral("queued");
        bool exists = false;
        for (const UploadTask& known : uploads_) {
            if (known.task_id == task.task_id) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            uploads_.append(task);
            emit uploadQueued(task.task_id, task.name, task.size, task.context,
                              task.target_id, task.mime);
        }
    }
    const QVector<HistoryStore::FileTask> downloads =
        history_->pendingFileTasks();
    for (const HistoryStore::FileTask& item : downloads) {
        DownloadTask task;
        task.file_id = item.file_id;
        task.context = item.context;
        task.target_id = item.target_id;
        task.message_id = item.message_id;
        task.name = item.name;
        task.mime = item.mime;
        task.save_path = item.save_path;
        task.received = item.received;
        task.total = item.total;
        task.open_after = false;
        downloads_.append(task);
    }
    startNextUpload();
    startNextDownload();
}

void FileTransferManager::reconnectIdle() {
    if (!started_) {
        return;
    }
    if (active_upload_id_.isEmpty() && !upload_client_->isReady()) {
        upload_client_->start(host_, port_, tls_, ca_path_, token_);
    }
    if (active_download_id_ == 0 && !download_client_->isReady()) {
        download_client_->start(host_, port_, tls_, ca_path_, token_);
    }
}

void FileTransferManager::listFiles(int kind, quint64 target_id) {
    upload_client_->listFiles(kind, target_id);
}

void FileTransferManager::deleteFile(quint64 file_id) {
    upload_client_->deleteFile(file_id);
}

void FileTransferManager::requestFileInfo(quint64 file_id) {
    upload_client_->requestFileInfo(file_id);
}

void FileTransferManager::requestThumbnail(quint64 file_id) {
    upload_client_->requestThumbnail(file_id);
}

void FileTransferManager::startNextUpload() {
    if (!started_ || !active_upload_id_.isEmpty() ||
        !upload_client_->isIdle()) {
        return;
    }
    for (UploadTask& task : uploads_) {
        if (task.state != QStringLiteral("queued")) {
            continue;
        }
        if (!QFileInfo::exists(task.source_path)) {
            task.state = QStringLiteral("failed");
            persistUpload(task);
            emit uploadStateChanged(task.task_id, task.state);
            emit uploadFailed(task.task_id, QStringLiteral("原文件不存在"));
            continue;
        }
        active_upload_id_ = task.task_id;
        task.state = QStringLiteral("uploading");
        emit uploadStateChanged(task.task_id, task.state);
        if (!task.upload_id.isEmpty()) {
            upload_client_->resumeUpload(task.upload_id, task.source_path,
                                         task.kind, task.target_id, task.name,
                                         task.mime, task.size);
        } else {
            task.upload_id.clear();
            upload_client_->uploadFile(task.source_path, task.kind,
                                       task.target_id, task.name, task.mime);
        }
        return;
    }
}

void FileTransferManager::startNextDownload() {
    if (!started_ || active_download_id_ != 0 ||
        !download_client_->isIdle()) {
        return;
    }
    for (int i = 0; i < downloads_.size(); ++i) {
        DownloadTask& task = downloads_[i];
        if (task.save_path.isEmpty() ||
            task.state != QStringLiteral("queued")) {
            continue;
        }
        task.state = QStringLiteral("downloading");
        active_download_id_ = task.file_id;
        download_client_->downloadFile(task.file_id, task.save_path);
        return;
    }
}

void FileTransferManager::persistUpload(const UploadTask& task) const {
    if (history_ == nullptr) {
        return;
    }
    history_->savePendingUpload(toPending(
        task.task_id, task.upload_id, task.kind, task.target_id, task.context,
        task.source_path, task.name, task.mime, task.size, task.offset,
        task.state));
}

FileTransferManager::UploadTask* FileTransferManager::findUpload(
    const QString& task_id) {
    for (UploadTask& task : uploads_) {
        if (task.task_id == task_id) {
            return &task;
        }
    }
    return nullptr;
}

FileTransferManager::DownloadTask* FileTransferManager::findDownload(
    quint64 file_id) {
    for (DownloadTask& task : downloads_) {
        if (task.file_id == file_id && !task.save_path.isEmpty()) {
            return &task;
        }
    }
    return nullptr;
}

void FileTransferManager::finishCurrentUpload(const QString& state) {
    UploadTask* task = findUpload(active_upload_id_);
    if (task != nullptr) {
        task->state = state;
        persistUpload(*task);
        emit uploadStateChanged(task->task_id, state);
    }
    active_upload_id_.clear();
    startNextUpload();
}

}  // namespace client
}  // namespace chatbox
