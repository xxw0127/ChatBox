#ifndef CHATBOX_CLIENT_CORE_FILE_TRANSFER_MANAGER_H
#define CHATBOX_CLIENT_CORE_FILE_TRANSFER_MANAGER_H

#include <QObject>
#include <QQueue>
#include <QString>
#include <QVector>

#include "core/file_types.h"
#include "core/history_store.h"

class FileTransferClient;
class QTimer;

namespace chatbox {
namespace client {

// Serializes uploads and downloads onto two independent file connections, so
// neither direction ever blocks the chat connection. Task state is persisted
// in HistoryStore and restored after the next login.
class FileTransferManager : public QObject {
    Q_OBJECT

public:
    explicit FileTransferManager(QObject* parent = nullptr);
    ~FileTransferManager() override;

    void setHistoryStore(HistoryStore* store);
    void start(const QString& host, quint16 port, bool tls,
               const QString& ca_path, const QString& token);
    void stop();
    bool isReady() const;

    QString enqueueUpload(const QString& local_path, int kind,
                          quint64 target_id, const QString& display_name,
                          const QString& mime, ChatContext context,
                          const QString& task_id = QString());
    void pauseUpload(const QString& task_id);
    void resumeUpload(const QString& task_id);
    void cancelUpload(const QString& task_id);

    void toggleDownload(quint64 file_id);
    void requestDownload(quint64 file_id, ChatContext context,
                         quint64 target_id, quint64 message_id,
                         const QString& name, const QString& mime,
                         const QString& save_path, bool open_after);
    void restorePending();
    void listFiles(int kind, quint64 target_id);
    void deleteFile(quint64 file_id);
    void requestFileInfo(quint64 file_id);
    void requestThumbnail(quint64 file_id);

signals:
    void connectionError(const QString& message);
    void fileOperationFailed(const QString& message);
    void uploadQueued(const QString& task_id, const QString& name,
                      quint64 size, chatbox::client::ChatContext context,
                      quint64 target_id, const QString& mime);
    void uploadProgress(const QString& task_id, quint64 sent, quint64 total);
    void uploadFinished(const QString& task_id, quint64 message_id,
                        quint64 file_id, const QString& created_at,
                        const QString& name, quint64 size, const QString& mime,
                        chatbox::client::ChatContext context,
                        quint64 target_id);
    void uploadFailed(const QString& task_id, const QString& message);
    void uploadStateChanged(const QString& task_id, const QString& state);

    void downloadProgress(quint64 file_id, quint64 received, quint64 total);
    void downloadFinished(quint64 file_id, const QString& save_path,
                          const QString& mime, bool open_after);
    void downloadFailed(quint64 file_id, const QString& message);
    void downloadPaused(quint64 file_id);

    void fileListReady(int kind, quint64 target_id, quint32 total_count,
                       quint64 used_bytes,
                       const QVector<chatbox::client::FileEntryView>& entries);
    void fileInfoReady(const chatbox::client::FileEntryView& entry);
    void fileDeleted(quint64 file_id);
    void thumbnailReady(quint64 file_id, const QByteArray& data);

private:
    struct UploadTask {
        QString task_id;
        QString upload_id;
        int kind = 1;
        quint64 target_id = 0;
        ChatContext context = ChatContext::kRoom;
        QString source_path;
        QString name;
        QString mime;
        quint64 size = 0;
        quint64 offset = 0;
        quint64 persisted_offset = 0;
        QString state = QStringLiteral("queued");
    };

    struct DownloadTask {
        quint64 file_id = 0;
        ChatContext context = ChatContext::kRoom;
        quint64 target_id = 0;
        quint64 message_id = 0;
        QString name;
        QString mime;
        QString save_path;
        bool open_after = true;
        QString state = QStringLiteral("queued");
        quint64 received = 0;
        quint64 total = 0;
        quint64 persisted_received = 0;
    };

    void reconnectIdle();
    void startNextUpload();
    void startNextDownload();
    void persistUpload(const UploadTask& task) const;
    UploadTask* findUpload(const QString& task_id);
    DownloadTask* findDownload(quint64 file_id);
    void finishCurrentUpload(const QString& state);

    FileTransferClient* upload_client_ = nullptr;
    FileTransferClient* download_client_ = nullptr;
    HistoryStore* history_ = nullptr;

    QString active_upload_id_;
    quint64 active_download_id_ = 0;
    QVector<UploadTask> uploads_;
    QVector<DownloadTask> downloads_;
    bool started_ = false;
    QString host_;
    quint16 port_ = 0;
    bool tls_ = false;
    QString ca_path_;
    QString token_;
    QTimer* keepalive_ = nullptr;
    bool restored_ = false;
};

}  // namespace client
}  // namespace chatbox

#endif  // CHATBOX_CLIENT_CORE_FILE_TRANSFER_MANAGER_H
