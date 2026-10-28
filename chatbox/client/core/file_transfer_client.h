#ifndef CHATBOX_CLIENT_CORE_FILE_TRANSFER_CLIENT_H
#define CHATBOX_CLIENT_CORE_FILE_TRANSFER_CLIENT_H

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include <QVector>

#include <memory>

#include "core/file_types.h"

class QSslSocket;
class QTimer;

// One independent TCP/TLS connection for file frames. The chat connection is
// never used for file bytes. Chunks are sent one at a time, so a 2GB upload
// never blocks the UI event loop.
class FileTransferClient : public QObject {
    Q_OBJECT

public:
    explicit FileTransferClient(QObject* parent = nullptr);
    ~FileTransferClient() override;

    void start(const QString& host, quint16 port, bool tls,
               const QString& ca_path, const QString& token);
    void stop();
    bool isReady() const;

    void uploadFile(const QString& local_path, int kind, quint64 target_id,
                    const QString& display_name, const QString& mime);
    void resumeUpload(const QString& upload_id, const QString& local_path,
                      int kind, quint64 target_id, const QString& display_name,
                      const QString& mime, quint64 size);
    void pauseUpload();
    void cancelUpload();
    void pauseDownload();
    void resumeDownload();
    void downloadFile(quint64 file_id, const QString& save_path);
    void requestThumbnail(quint64 file_id);
    void sendKeepAlive();
    void listFiles(int kind, quint64 target_id);
    void requestFileInfo(quint64 file_id);
    void deleteFile(quint64 file_id);

signals:
    void authenticated(quint64 user_id);
    void connectionError(const QString& message);
    void uploadInitialized(const QString& upload_id, qint64 offset);
    void uploadProgress(qint64 sent, qint64 total);
    void uploadFinished(quint64 message_id, quint64 file_id,
                        const QString& created_at);
    void uploadFailed(const QString& message);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(quint64 file_id, const QString& save_path);
    void downloadFailed(quint64 file_id, const QString& message);
    void thumbnailReady(quint64 file_id, const QByteArray& data);
    void fileListReady(int kind, quint64 target_id, quint32 total_count,
                       quint64 used_bytes,
                       const QVector<chatbox::client::FileEntryView>& entries);
    void fileInfoReady(const chatbox::client::FileEntryView& entry);
    void fileDeleted(quint64 file_id);

private:
    enum class State {
        kDisconnected,
        kConnecting,
        kAuthenticating,
        kReady,
        kUploading,
        kDownloading,
    };

    void onConnected();
    void onReadyRead();
    void onSocketError();
    void sendFrame(quint16 cmd, quint32 seq, const QByteArray& body);
    void handleFrame(quint16 cmd, quint32 seq, const QByteArray& body);
    void startNextChunk();
    void finishUpload();
    void requestNextDownloadChunk();
    void finishDownload();

    QTcpSocket* socket_ = nullptr;
    QTimer* chunk_timer_ = nullptr;
    QByteArray buffer_;
    State state_ = State::kDisconnected;
    QString token_;
    quint32 next_seq_ = 1;
    quint32 auth_seq_ = 0;
    quint32 upload_seq_ = 0;
    quint32 download_seq_ = 0;

    QFile upload_file_;
    QString upload_id_;
    quint64 upload_size_ = 0;
    quint64 upload_offset_ = 0;
    int upload_kind_ = 0;
    quint64 upload_target_ = 0;
    QString upload_name_;
    QString upload_mime_;
    bool upload_paused_ = false;
    bool download_paused_ = false;

    QFile download_file_;
    QString download_path_;
    QString download_part_path_;
    quint64 download_file_id_ = 0;
    quint64 download_size_ = 0;
    quint64 download_received_ = 0;
    QString download_token_;

    quint64 thumb_file_id_ = 0;
    int list_kind_ = 1;
    quint64 list_target_ = 0;
};


#endif  // CHATBOX_CLIENT_CORE_FILE_TRANSFER_CLIENT_H
