#include "core/file_transfer_client.h"


#include <QDir>
#include <QFileInfo>
#include <QSslCertificate>
#include <QSslSocket>
#include <QTimer>

#include <algorithm>

#include <QDebug>

#include "common/file_protocol.h"
#include "core/protocol_qt.h"
#include "core/ui_theme.h"

namespace {

QString errorText(const QByteArray& body) {
    chatbox::ByteReader reader(body.constData(),
                               static_cast<std::size_t>(body.size()));
    uint16_t code = 0;
    std::string message;
    if (reader.getU16(code) && reader.getString(message)) {
        return QString::fromUtf8(message.data(),
                                 static_cast<int>(message.size()));
    }
    return QStringLiteral("文件连接错误");
}

}  // namespace

FileTransferClient::FileTransferClient(QObject* parent) : QObject(parent) {
    chunk_timer_ = new QTimer(this);
    chunk_timer_->setSingleShot(true);
    chunk_timer_->setInterval(0);
    connect(chunk_timer_, &QTimer::timeout, this,
            &FileTransferClient::startNextChunk);
    connect(this, &FileTransferClient::uploadFailed, this,
            [this](const QString&) { resetAfterFailure(); });
    connect(this, &FileTransferClient::downloadFailed, this,
            [this](quint64, const QString&) { resetAfterFailure(); });
}

FileTransferClient::~FileTransferClient() {
    stop();
}

void FileTransferClient::start(const QString& host, quint16 port, bool tls,
                               const QString& ca_path, const QString& token) {
    stop();
    token_ = token;
    buffer_.clear();
    state_ = State::kConnecting;
    if (tls) {
        auto* ssl = new QSslSocket(this);
        QList<QSslCertificate> certs = QSslCertificate::fromPath(ca_path);
        ssl->setCaCertificates(certs);
        socket_ = ssl;
        connect(ssl, &QSslSocket::encrypted, this,
                &FileTransferClient::onConnected);
    } else {
        socket_ = new QTcpSocket(this);
        connect(socket_, &QTcpSocket::connected, this,
                &FileTransferClient::onConnected);
    }
    connect(socket_, &QTcpSocket::readyRead, this,
            &FileTransferClient::onReadyRead);
    connect(socket_, &QTcpSocket::errorOccurred, this,
            &FileTransferClient::onSocketError);
    if (tls) {
        static_cast<QSslSocket*>(socket_)->connectToHostEncrypted(host, port);
    } else {
        socket_->connectToHost(host, port);
    }
}

void FileTransferClient::stop() {
    if (chunk_timer_ != nullptr) {
        chunk_timer_->stop();
    }
    if (upload_file_.isOpen()) {
        upload_file_.close();
    }
    if (download_file_.isOpen()) {
        download_file_.close();
    }
    if (socket_ != nullptr) {
        socket_->disconnect(this);
        socket_->abort();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    state_ = State::kDisconnected;
}

bool FileTransferClient::isReady() const {
    return state_ == State::kReady || state_ == State::kUploading ||
           state_ == State::kDownloading;
}

bool FileTransferClient::isIdle() const {
    return state_ == State::kReady;
}

void FileTransferClient::onConnected() {
    state_ = State::kAuthenticating;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(token_));
    auth_seq_ = next_seq_++;
    sendFrame(chatbox::kCmdFileAuth, auth_seq_, QByteArray::fromStdString(writer.data()));
}

void FileTransferClient::onSocketError() {
    if (socket_ == nullptr) {
        return;
    }
    const QString message = socket_->errorString();
    if (state_ == State::kUploading) {
        emit uploadFailed(message);
    } else if (state_ == State::kDownloading) {
        emit downloadFailed(download_file_id_, message);
    }
    emit connectionError(message);
    state_ = State::kDisconnected;
}

void FileTransferClient::onReadyRead() {
    if (socket_ == nullptr) {
        return;
    }
    buffer_ += socket_->readAll();
    while (true) {
        chatbox::Frame frame;
        if (!chatbox::decodeFromByteArray(buffer_, frame)) {
            break;
        }
        const int consumed = static_cast<int>(chatbox::kFrameHeaderSize +
                                              frame.header.body_len);
        buffer_.remove(0, consumed);
        const QByteArray body(frame.body.data(),
                              static_cast<int>(frame.body.size()));
        if (frame.header.flags & chatbox::kFlagError) {
            const QString message = errorText(body);
            if (frame.header.cmd == chatbox::kCmdFileList ||
                frame.header.cmd == chatbox::kCmdFileInfo ||
                frame.header.cmd == chatbox::kCmdFileThumb ||
                frame.header.cmd == chatbox::kCmdFileDelete) {
                emit fileOperationFailed(message);
                continue;
            }
            if (state_ == State::kUploading) {
                emit uploadFailed(message);
            } else if (state_ == State::kDownloading) {
                emit downloadFailed(download_file_id_, message);
            } else {
                emit connectionError(message);
            }
            continue;
        }
        handleFrame(frame.header.cmd, frame.header.seq, body);
    }
}

void FileTransferClient::sendFrame(quint16 cmd, quint32 seq,
                                   const QByteArray& body) {
    if (socket_ == nullptr) {
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = cmd;
    frame.header.seq = seq;
    frame.body.assign(body.constData(), static_cast<std::size_t>(body.size()));
    socket_->write(chatbox::encodeToByteArray(frame));
}

void FileTransferClient::handleFrame(quint16 cmd, quint32 seq,
                                     const QByteArray& body) {
    (void)seq;
    if (cmd == chatbox::kCmdError) {
        const QString message = errorText(body);
        if (state_ == State::kUploading) {
            emit uploadFailed(message);
        } else if (state_ == State::kDownloading) {
            emit downloadFailed(download_file_id_, message);
        } else {
            emit connectionError(message);
        }
        return;
    }
    if (cmd == chatbox::kCmdFileAuth) {
        chatbox::ByteReader reader(body.constData(),
                                   static_cast<std::size_t>(body.size()));
        uint64_t user_id = 0;
        if (reader.getU64(user_id)) {
            state_ = State::kReady;
            emit authenticated(user_id);
        }
        return;
    }
    if (cmd == chatbox::kCmdFileUploadInit) {
        std::string upload_id;
        uint64_t offset = 0;
        if (!chatbox::decodeUploadInitResponse(
                std::string(body.constData(), static_cast<std::size_t>(body.size())),
                upload_id, offset)) {
            emit uploadFailed("上传初始化响应无效");
            return;
        }
        upload_id_ = QString::fromStdString(upload_id);
        upload_offset_ = offset;
        upload_paused_ = false;
        if (!upload_file_.isOpen() &&
            !upload_file_.open(QIODevice::ReadOnly)) {
            emit uploadFailed("原文件不存在，请重新选择");
            return;
        }
        upload_size_ = static_cast<quint64>(upload_file_.size());
        emit uploadInitialized(upload_id_, static_cast<qint64>(upload_offset_));
        if (upload_file_.seek(static_cast<qint64>(upload_offset_))) {
            emit uploadProgress(static_cast<qint64>(upload_offset_),
                                static_cast<qint64>(upload_size_));
            startNextChunk();
        } else {
            emit uploadFailed("无法读取原文件");
        }
        return;
    }
    if (cmd == chatbox::kCmdFileUploadChunk) {
        if (upload_chunk_seq_ == 0 || seq != upload_chunk_seq_) {
            return;
        }
        upload_chunk_seq_ = 0;
        uint64_t offset = 0;
        if (!chatbox::decodeUploadAck(
                std::string(body.constData(), static_cast<std::size_t>(body.size())),
                offset)) {
            emit uploadFailed("上传确认无效");
            return;
        }
        upload_offset_ = offset;
        emit uploadProgress(static_cast<qint64>(upload_offset_),
                            static_cast<qint64>(upload_size_));
        if (upload_paused_) {
            return;
        }
        if (upload_offset_ < upload_size_) {
            startNextChunk();
        } else {
            finishUpload();
        }
        return;
    }
    if (cmd == chatbox::kCmdFileUploadStatus) {
        uint64_t offset = 0;
        uint8_t status = 0;
        if (!chatbox::decodeUploadStatusResponse(
                std::string(body.constData(),
                            static_cast<std::size_t>(body.size())),
                offset, status)) {
            emit uploadFailed("上传状态响应无效");
            return;
        }
        if (status == 1) {
            // The previous commit response was lost; ask for the original ids.
            chatbox::ByteWriter commit_writer;
            commit_writer.putString(chatbox::toUtf8(upload_id_));
            sendFrame(chatbox::kCmdFileUploadCommit, next_seq_++,
                      QByteArray::fromStdString(commit_writer.data()));
            return;
        }
        if (status == 2) {
            emit uploadFailed("上传任务已取消");
            return;
        }
        upload_offset_ = offset;
        upload_paused_ = false;
        if (!upload_file_.isOpen() &&
            !upload_file_.open(QIODevice::ReadOnly)) {
            emit uploadFailed("原文件不存在，请重新选择");
            return;
        }
        emit uploadInitialized(upload_id_, static_cast<qint64>(upload_offset_));
        emit uploadProgress(static_cast<qint64>(upload_offset_),
                            static_cast<qint64>(upload_size_));
        if (upload_offset_ >= upload_size_) {
            finishUpload();
        } else if (upload_file_.seek(static_cast<qint64>(upload_offset_))) {
            startNextChunk();
        } else {
            emit uploadFailed("无法读取原文件");
        }
        return;
    }
    if (cmd == chatbox::kCmdFileUploadCommit) {
        uint64_t message_id = 0;
        uint64_t file_id = 0;
        std::string created_at;
        if (!chatbox::decodeUploadCommitResponse(
                std::string(body.constData(), static_cast<std::size_t>(body.size())),
                message_id, file_id, created_at)) {
            emit uploadFailed("上传完成响应无效");
            return;
        }
        if (upload_file_.isOpen()) {
            upload_file_.close();
        }
        state_ = State::kReady;
        emit uploadFinished(message_id, file_id,
                            QString::fromStdString(created_at));
        return;
    }
    if (cmd == chatbox::kCmdFileOpen) {
        uint64_t total = 0;
        std::string token;
        if (!chatbox::decodeFileOpenResponse(
                std::string(body.constData(), static_cast<std::size_t>(body.size())),
                total, token)) {
            emit downloadFailed(download_file_id_, "打开文件响应无效");
            return;
        }
        download_size_ = total;
        download_token_ = QString::fromStdString(token);
        const QString part = download_path_ + ".part";
        download_part_path_ = part;
        QDir().mkpath(QFileInfo(part).absolutePath());
        download_file_.setFileName(part);
        if (!download_file_.open(QIODevice::ReadWrite)) {
            emit downloadFailed(download_file_id_, "无法写入保存位置");
            return;
        }
        download_received_ = static_cast<quint64>(download_file_.size());
        if (download_received_ > download_size_) {
            download_file_.resize(0);
            download_received_ = 0;
        }
        emit downloadProgress(static_cast<qint64>(download_received_),
                              static_cast<qint64>(download_size_));
        if (download_received_ >= download_size_) {
            finishDownload();
        } else {
            requestNextDownloadChunk();
        }
        return;
    }
    if (cmd == chatbox::kCmdFileChunk) {
        uint64_t file_id = 0;
        uint64_t offset = 0;
        std::string data;
        if (!chatbox::decodeFileChunk(
                std::string(body.constData(), static_cast<std::size_t>(body.size())),
                file_id, offset, data) ||
            file_id != download_file_id_) {
            emit downloadFailed(download_file_id_, "文件块无效");
            return;
        }
        if (offset != download_received_) {
            emit downloadFailed(download_file_id_, "文件块偏移不连续");
            return;
        }
        download_file_.write(data.data(), static_cast<qint64>(data.size()));
        download_file_.flush();
        download_received_ += data.size();
        emit downloadProgress(static_cast<qint64>(download_received_),
                              static_cast<qint64>(download_size_));
        sendFrame(chatbox::kCmdFileReadAck, next_seq_++,
                  QByteArray());
        if (download_paused_) {
            return;
        }
        if (download_received_ < download_size_) {
            requestNextDownloadChunk();
        } else {
            finishDownload();
        }
        return;
    }
    if (cmd == chatbox::kCmdFileThumb) {
        if (body.size() < 8) {
            emit fileOperationFailed(QStringLiteral("???????"));
            return;
        }
        chatbox::ByteReader reader(body.constData(),
                                   static_cast<std::size_t>(body.size()));
        uint64_t file_id = 0;
        if (!reader.getU64(file_id)) {
            emit fileOperationFailed(QStringLiteral("???????"));
            return;
        }
        const int header = static_cast<int>(body.size()) - reader.remaining();
        emit thumbnailReady(file_id, body.mid(header));
        return;
    }
    if (cmd == chatbox::kCmdFileList) {
        std::vector<chatbox::FileEntry> wire_entries;
        uint32_t total = 0;
        uint64_t used = 0;
        if (!chatbox::decodeFileListResponse(
                std::string(body.constData(),
                            static_cast<std::size_t>(body.size())),
                wire_entries, total, used)) {
            emit connectionError("????????");
            return;
        }
        QVector<chatbox::client::FileEntryView> entries;
        for (const chatbox::FileEntry& entry : wire_entries) {
            entries.push_back(chatbox::client::fileEntryFromWire(entry));
        }
        emit fileListReady(static_cast<int>(list_kind_), list_target_, total,
                           used, entries);
        return;
    }
    if (cmd == chatbox::kCmdFileInfo) {
        chatbox::FileEntry wire;
        if (chatbox::decodeFileInfo(
                std::string(body.constData(),
                            static_cast<std::size_t>(body.size())),
                wire)) {
            emit fileInfoReady(chatbox::client::fileEntryFromWire(wire));
        }
        return;
    }
    if (cmd == chatbox::kCmdFileDelete) {
        uint64_t file_id = 0;
        if (chatbox::decodeFileIdResponse(
                std::string(body.constData(),
                            static_cast<std::size_t>(body.size())),
                file_id)) {
            emit fileDeleted(file_id);
        }
        return;
    }
}

void FileTransferClient::uploadFile(const QString& local_path, int kind,
                                    quint64 target_id,
                                    const QString& display_name,
                                    const QString& mime) {
    if (!isReady()) {
        emit uploadFailed("文件连接尚未准备好");
        return;
    }
    if (upload_file_.isOpen()) {
        upload_file_.close();
    }
    upload_file_.setFileName(local_path);
    if (!upload_file_.open(QIODevice::ReadOnly)) {
        emit uploadFailed("原文件不存在，请重新选择");
        return;
    }
    upload_size_ = static_cast<quint64>(upload_file_.size());
    upload_offset_ = 0;
    upload_kind_ = kind;
    upload_target_ = target_id;
    upload_name_ = display_name.isEmpty() ? QFileInfo(local_path).fileName()
                                           : display_name;
    upload_mime_ = mime;
    upload_paused_ = false;
    state_ = State::kUploading;
    chatbox::UploadInitRequest request;
    request.kind = static_cast<chatbox::FileKind>(kind);
    request.target_id = target_id;
    request.name = chatbox::toUtf8(upload_name_);
    request.size = upload_size_;
    request.mime = chatbox::toUtf8(mime);
    upload_seq_ = next_seq_++;
    sendFrame(chatbox::kCmdFileUploadInit, upload_seq_,
              QByteArray::fromStdString(chatbox::encodeUploadInit(request)));
}

void FileTransferClient::resumeUpload(const QString& upload_id,
                                      const QString& local_path, int kind,
                                      quint64 target_id,
                                      const QString& display_name,
                                      const QString& mime, quint64 size) {
    if (!isReady()) {
        emit uploadFailed("文件连接尚未准备好");
        return;
    }
    if (upload_file_.isOpen()) {
        upload_file_.close();
    }
    upload_file_.setFileName(local_path);
    if (!upload_file_.open(QIODevice::ReadOnly)) {
        emit uploadFailed("原文件不存在，无法续传");
        return;
    }
    upload_id_ = upload_id;
    upload_size_ = size > 0 ? size : static_cast<quint64>(upload_file_.size());
    upload_offset_ = 0;
    upload_kind_ = kind;
    upload_target_ = target_id;
    upload_name_ = display_name;
    upload_mime_ = mime;
    upload_paused_ = false;
    state_ = State::kUploading;
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(upload_id));
    sendFrame(chatbox::kCmdFileUploadStatus, next_seq_++,
              QByteArray::fromStdString(writer.data()));
}

void FileTransferClient::pauseUpload() {
    upload_paused_ = true;
}

void FileTransferClient::cancelUpload() {
    if (upload_id_.isEmpty()) {
        return;
    }
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(upload_id_));
    sendFrame(chatbox::kCmdFileUploadCancel, next_seq_++, QByteArray::fromStdString(writer.data()));
    if (upload_file_.isOpen()) {
        upload_file_.close();
    }
    upload_id_.clear();
    upload_paused_ = false;
    upload_chunk_seq_ = 0;
    state_ = State::kReady;
}

void FileTransferClient::startNextChunk() {
    if (state_ != State::kUploading || !upload_file_.isOpen() || upload_paused_) {
        return;
    }
    if (upload_offset_ >= upload_size_) {
        finishUpload();
        return;
    }
    upload_file_.seek(static_cast<qint64>(upload_offset_));
    const QByteArray data = upload_file_.read(60 * 1024);
    if (data.isEmpty()) {
        emit uploadFailed("读取原文件失败");
        return;
    }
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(upload_id_));
    writer.putU64(upload_offset_);
    writer.putString(std::string(data.constData(),
                                 static_cast<std::size_t>(data.size())));
    upload_chunk_seq_ = next_seq_++;
    sendFrame(chatbox::kCmdFileUploadChunk, upload_chunk_seq_,
              QByteArray::fromStdString(writer.data()));
}

void FileTransferClient::finishUpload() {
    if (upload_id_.isEmpty()) {
        return;
    }
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(upload_id_));
    sendFrame(chatbox::kCmdFileUploadCommit, next_seq_++, QByteArray::fromStdString(writer.data()));
}

void FileTransferClient::downloadFile(quint64 file_id,
                                      const QString& save_path) {
    if (!isReady()) {
        emit downloadFailed(file_id, "文件连接尚未准备好");
        return;
    }
    download_file_id_ = file_id;
    download_path_ = save_path;
    download_size_ = 0;
    download_received_ = 0;
    download_token_.clear();
    state_ = State::kDownloading;
    download_paused_ = false;
    chatbox::ByteWriter writer;
    writer.putU64(file_id);
    download_seq_ = next_seq_++;
    sendFrame(chatbox::kCmdFileOpen, download_seq_, QByteArray::fromStdString(writer.data()));
}

void FileTransferClient::pauseDownload() {
    download_paused_ = true;
}

void FileTransferClient::resumeDownload() {
    download_paused_ = false;
    if (state_ == State::kDownloading) {
        requestNextDownloadChunk();
    }
}

void FileTransferClient::requestNextDownloadChunk() {
    if (state_ != State::kDownloading || download_token_.isEmpty()) {
        return;
    }
    chatbox::ByteWriter writer;
    writer.putString(chatbox::toUtf8(download_token_));
    writer.putU64(download_received_);
    const quint64 remaining = download_size_ - download_received_;
    writer.putU32(static_cast<uint32_t>(std::min<quint64>(remaining, 60 * 1024)));
    download_seq_ = next_seq_++;
    sendFrame(chatbox::kCmdFileRead, download_seq_, QByteArray::fromStdString(writer.data()));
}

void FileTransferClient::finishDownload() {
    if (download_file_.isOpen()) {
        download_file_.close();
    }
    if (download_part_path_ != download_path_) {
        QFile::remove(download_path_);
        if (!QFile::rename(download_part_path_, download_path_)) {
            emit downloadFailed(download_file_id_, "无法保存文件");
            return;
        }
    }
    state_ = State::kReady;
    emit downloadFinished(download_file_id_, download_path_);
}

void FileTransferClient::requestThumbnail(quint64 file_id) {
    thumb_file_id_ = file_id;
    chatbox::ByteWriter writer;
    writer.putU64(file_id);
    sendFrame(chatbox::kCmdFileThumb, next_seq_++,
              QByteArray::fromStdString(writer.data()));
}

void FileTransferClient::listFiles(int kind, quint64 target_id) {
    list_kind_ = kind;
    list_target_ = target_id;
    chatbox::ByteWriter writer;
    writer.putU8(static_cast<uint8_t>(kind));
    writer.putU64(target_id);
    writer.putU64(0);
    writer.putU16(100);
    sendFrame(chatbox::kCmdFileList, next_seq_++,
              QByteArray::fromStdString(writer.data()));
}

void FileTransferClient::requestFileInfo(quint64 file_id) {
    chatbox::ByteWriter writer;
    writer.putU64(file_id);
    sendFrame(chatbox::kCmdFileInfo, next_seq_++,
              QByteArray::fromStdString(writer.data()));
}

void FileTransferClient::resetAfterFailure() {
    if (upload_file_.isOpen()) {
        upload_file_.close();
    }
    if (download_file_.isOpen()) {
        download_file_.close();
    }
    upload_id_.clear();
    upload_paused_ = false;
    upload_chunk_seq_ = 0;
    download_paused_ = false;
    if (socket_ != nullptr &&
        socket_->state() == QAbstractSocket::ConnectedState) {
        state_ = State::kReady;
    }
}

void FileTransferClient::sendKeepAlive() {
    if (!isReady()) {
        return;
    }
    sendFrame(chatbox::kCmdFileReadAck, next_seq_++, QByteArray());
}

void FileTransferClient::deleteFile(quint64 file_id) {
    chatbox::ByteWriter writer;
    writer.putU64(file_id);
    sendFrame(chatbox::kCmdFileDelete, next_seq_++,
              QByteArray::fromStdString(writer.data()));
}

