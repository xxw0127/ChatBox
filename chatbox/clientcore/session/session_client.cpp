#include "session/session_client.h"

#include <cstddef>
#include <string>

namespace chatbox {
namespace clientcore {

namespace {

constexpr uint32_t kClientCaps = kCapMultiDevice | kCapSyncPull |
                                 kCapReadStatePush | kCapMessageEcho |
                                 kCapSessionManage | kCapDeviceMetadata;

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::string toUtf8(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

bool readQString(chatbox::ByteReader& reader, QString& value) {
    std::string text;
    if (!reader.getString(text)) {
        return false;
    }
    value = fromUtf8(text);
    return true;
}

bool readErrorBody(const chatbox::Frame& frame, int& code, QString& message) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint16_t wire_code = 0;
    if (!reader.getU16(wire_code) || !readQString(reader, message)) {
        return false;
    }
    code = static_cast<int>(wire_code);
    return true;
}

}  // namespace

SessionClient::SessionClient(Transport* transport, QObject* parent)
    : QObject(parent), transport_(transport) {
    qRegisterMetaType<LoginResult>("chatbox::clientcore::LoginResult");
    qRegisterMetaType<chatbox::SessionInfo>("chatbox::SessionInfo");
    qRegisterMetaType<QVector<chatbox::SessionInfo>>(
        "QVector<chatbox::SessionInfo>");
    if (transport_ != nullptr) {
        connect(transport_, &Transport::frameReceived, this,
                &SessionClient::handleFrame);
    }
}

void SessionClient::setConfig(const SessionConfig& config) {
    config_ = config;
}

const SessionConfig& SessionClient::config() const {
    return config_;
}

bool SessionClient::send(chatbox::Frame frame, QString* error) {
    if (transport_ == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("transport not configured");
        }
        return false;
    }
    frame.header.seq = next_seq_++;
    const std::string encoded = chatbox::encodeFrame(frame);
    return transport_->sendFrame(
        QByteArray(encoded.data(), static_cast<int>(encoded.size())), error);
}

void SessionClient::login(const QString& username, const QString& password) {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdLogin;
    chatbox::ByteWriter writer;
    writer.putString(toUtf8(username));
    writer.putString(toUtf8(password));
    chatbox::DeviceInfo device;
    device.device_id = toUtf8(config_.device_id);
    device.device_name = toUtf8(config_.device_name);
    device.platform = toUtf8(config_.platform);
    device.app_version = toUtf8(config_.app_version);
    device.caps = kClientCaps;
    chatbox::appendDeviceInfo(writer, device);
    frame.body = writer.data();
    login_seq_ = next_seq_;
    QString error;
    if (!send(frame, &error)) {
        login_seq_ = 0;
        emit loginFailed(2001, error);
    }
}

void SessionClient::resume(const QString& token) {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdResume;
    chatbox::ByteWriter writer;
    writer.putString(toUtf8(token));
    chatbox::DeviceInfo device;
    device.device_id = toUtf8(config_.device_id);
    device.device_name = toUtf8(config_.device_name);
    device.platform = toUtf8(config_.platform);
    device.app_version = toUtf8(config_.app_version);
    device.caps = kClientCaps;
    chatbox::appendDeviceInfo(writer, device);
    frame.body = writer.data();
    resume_token_ = token;
    resume_seq_ = next_seq_;
    QString error;
    if (!send(frame, &error)) {
        resume_seq_ = 0;
        resume_token_.clear();
        emit loginFailed(2001, error);
    }
}

void SessionClient::logout() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdLogout;
    logout_seq_ = next_seq_;
    QString error;
    if (!send(frame, &error)) {
        logout_seq_ = 0;
        emit requestFailed(2001, error);
    }
}

void SessionClient::requestSessionList() {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdSessionList;
    session_list_seq_ = next_seq_;
    QString error;
    if (!send(frame, &error)) {
        session_list_seq_ = 0;
        emit requestFailed(2001, error);
    }
}

void SessionClient::revokeSession(const QString& session_id) {
    if (session_id.isEmpty()) {
        emit requestFailed(1001, QStringLiteral("invalid session id"));
        return;
    }
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdSessionRevoke;
    chatbox::ByteWriter writer;
    writer.putString(toUtf8(session_id));
    frame.body = writer.data();
    pending_revoke_session_id_ = session_id;
    revoke_seq_ = next_seq_;
    QString error;
    if (!send(frame, &error)) {
        revoke_seq_ = 0;
        pending_revoke_session_id_.clear();
        emit requestFailed(2001, error);
    }
}

void SessionClient::handleFrame(const QByteArray& raw) {
    if (static_cast<std::size_t>(raw.size()) < chatbox::kFrameHeaderSize) {
        return;
    }
    chatbox::FrameHeader header;
    if (!chatbox::decodeHeader(raw.constData(),
                               static_cast<std::size_t>(raw.size()), header)) {
        return;
    }
    std::string error;
    if (!chatbox::validateHeader(header, chatbox::kMaxInFrameBody, error)) {
        return;
    }
    if (header.body_len !=
        static_cast<uint32_t>(raw.size() - chatbox::kFrameHeaderSize)) {
        return;
    }

    chatbox::Frame frame;
    frame.header = header;
    frame.body.assign(raw.constData() + chatbox::kFrameHeaderSize,
                      header.body_len);

    if (frame.header.cmd == chatbox::kCmdError) {
        handleError(frame);
        return;
    }
    if (frame.header.cmd == chatbox::kCmdLogin) {
        if (frame.header.seq == login_seq_) {
            login_seq_ = 0;
            handleLoginResponse(frame, false, QString());
        }
        return;
    }
    if (frame.header.cmd == chatbox::kCmdResume) {
        if (frame.header.seq == resume_seq_) {
            resume_seq_ = 0;
            handleLoginResponse(frame, true, resume_token_);
            resume_token_.clear();
        }
        return;
    }
    if (frame.header.cmd == chatbox::kCmdLogout) {
        if (frame.header.seq == logout_seq_) {
            logout_seq_ = 0;
            emit logoutSucceeded();
        }
        return;
    }
    if (frame.header.cmd == chatbox::kCmdSessionList) {
        if (frame.header.seq == session_list_seq_) {
            session_list_seq_ = 0;
            handleSessionList(frame);
        }
        return;
    }
    if (frame.header.cmd == chatbox::kCmdSessionRevoke) {
        if (frame.header.seq == revoke_seq_) {
            revoke_seq_ = 0;
            handleSessionRevoke(frame);
        }
        return;
    }

    emit frameReceived(raw);
}

void SessionClient::handleLoginResponse(const chatbox::Frame& frame,
                                        bool is_resume,
                                        const QString& resume_token) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint64_t user_id = 0;
    QString nickname;
    QString token;
    if (!reader.getU64(user_id) || !readQString(reader, nickname)) {
        emit loginFailed(2001, QStringLiteral("malformed auth response"));
        return;
    }
    if (!is_resume && !readQString(reader, token)) {
        emit loginFailed(2001, QStringLiteral("malformed login response"));
        return;
    }
    if (is_resume) {
        token = resume_token;
    }

    uint32_t server_caps = 0;
    uint32_t ttl_seconds = 0;
    if (reader.remaining() >= 8) {
        if (!chatbox::readAuthExtras(reader, server_caps, ttl_seconds)) {
            emit loginFailed(2001, QStringLiteral("malformed auth extras"));
            return;
        }
    }

    LoginResult result;
    result.user_id = static_cast<quint64>(user_id);
    result.nickname = nickname;
    result.token = token;
    result.server_caps = server_caps;
    result.ttl_seconds = ttl_seconds;
    emit loginSucceeded(result);
}

void SessionClient::handleError(const chatbox::Frame& frame) {
    int code = 0;
    QString message;
    if (!readErrorBody(frame, code, message)) {
        code = 2001;
        message = QStringLiteral("malformed error frame");
    }

    if (frame.header.seq == login_seq_ || frame.header.seq == resume_seq_) {
        login_seq_ = 0;
        resume_seq_ = 0;
        resume_token_.clear();
        emit loginFailed(code, message);
        return;
    }
    if (frame.header.seq == session_list_seq_) {
        session_list_seq_ = 0;
        emit requestFailed(code, message);
        return;
    }
    if (frame.header.seq == revoke_seq_) {
        revoke_seq_ = 0;
        pending_revoke_session_id_.clear();
        emit requestFailed(code, message);
        return;
    }
    if (frame.header.seq == logout_seq_) {
        logout_seq_ = 0;
        emit requestFailed(code, message);
        return;
    }

    const std::string encoded = chatbox::encodeFrame(frame);
    emit frameReceived(
        QByteArray(encoded.data(), static_cast<int>(encoded.size())));
}

void SessionClient::handleSessionList(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    std::vector<chatbox::SessionInfo> sessions;
    if (!chatbox::readSessionList(reader, sessions)) {
        emit requestFailed(2001, QStringLiteral("malformed session list"));
        return;
    }
    QVector<chatbox::SessionInfo> result;
    result.reserve(static_cast<int>(sessions.size()));
    for (const chatbox::SessionInfo& session : sessions) {
        result.push_back(session);
    }
    emit sessionListReceived(result);
}

void SessionClient::handleSessionRevoke(const chatbox::Frame& frame) {
    Q_UNUSED(frame);
    const QString session_id = pending_revoke_session_id_;
    pending_revoke_session_id_.clear();
    emit sessionRevoked(session_id);
}

}  // namespace clientcore
}  // namespace chatbox
