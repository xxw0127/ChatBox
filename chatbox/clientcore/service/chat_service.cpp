#include "service/chat_service.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonValue>
#include <QTimer>
#include <QUuid>

#include "common/file_protocol.h"
#include "common/group_protocol.h"
#include "common/session_protocol.h"
#include "transport/qt_ssl_transport.h"

namespace chatbox {
namespace clientcore {

namespace {

constexpr int kMaxRecentMessages = 50;
constexpr int kReconnectDelays[] = {1, 2, 4, 8, 16, 30};

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::string toUtf8(const QString& value) {
    const QByteArray bytes = value.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

bool readString(chatbox::ByteReader& reader, QString& value) {
    std::string text;
    if (!reader.getString(text)) {
        return false;
    }
    value = fromUtf8(text);
    return true;
}

QString utcNow() {
    return QDateTime::currentDateTimeUtc().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

bool isTerminalCode(int code) {
    return code == 1002 || code == 1009 || code == 1010 || code == 1011;
}

QString conversationTitle(int kind, quint64 target_id,
                          const QString& fallback) {
    if (!fallback.isEmpty()) {
        return fallback;
    }
    if (kind == 1) {
        return QStringLiteral("群聊 %1").arg(target_id);
    }
    return QStringLiteral("用户 %1").arg(target_id);
}

const ConversationRow* findConversation(const QVector<ConversationRow>& rows,
                                        int kind, quint64 target_id) {
    for (const ConversationRow& row : rows) {
        if (row.kind == kind && row.target_id == target_id) {
            return &row;
        }
    }
    return nullptr;
}

bool decodeFrame(const QByteArray& raw, chatbox::Frame& frame) {
    if (static_cast<std::size_t>(raw.size()) < chatbox::kFrameHeaderSize) {
        return false;
    }
    chatbox::FrameHeader header;
    if (!chatbox::decodeHeader(raw.constData(),
                               static_cast<std::size_t>(raw.size()), header)) {
        return false;
    }
    std::string error;
    if (!chatbox::validateHeader(header, chatbox::kMaxInFrameBody, error)) {
        return false;
    }
    if (header.body_len !=
        static_cast<uint32_t>(raw.size() - chatbox::kFrameHeaderSize)) {
        return false;
    }
    frame.header = header;
    frame.body.assign(raw.constData() + chatbox::kFrameHeaderSize,
                      header.body_len);
    return true;
}

}  // namespace

ChatService::ChatService(Transport* transport, QObject* parent)
    : QObject(parent), transport_(transport) {
    owns_transport_ = transport_ == nullptr;
    reconnect_timer_ = new QTimer(this);
    reconnect_timer_->setSingleShot(true);
    connect(reconnect_timer_, &QTimer::timeout, this, [this]() {
        if (!running_ || shutting_down_ || logged_out_) {
            return;
        }
        QString error;
        if (!transport_->start(config_.session.transport, &error)) {
            scheduleReconnect();
        }
    });
}

ChatService::~ChatService() {
    stop();
}

bool ChatService::start(const ServiceConfig& config, QString* error) {
    if (running_) {
        return true;
    }
    if (config.secure_store == nullptr) {
        if (error != nullptr) {
            *error = QStringLiteral("secure store is required");
        }
        return false;
    }
    if (config.db_path.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("database path is required");
        }
        return false;
    }

    config_ = config;
    secure_ = config.secure_store;
    QString db_error;
    if (!store_.open(config_.db_path, &db_error)) {
        if (error != nullptr) {
            *error = db_error;
        }
        return false;
    }

    QString device_id = config_.session.device_id;
    if (device_id.isEmpty()) {
        device_id = secure_->loadDeviceId();
    }
    if (device_id.isEmpty()) {
        device_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        secure_->saveDeviceId(device_id);
    }
    config_.session.device_id = device_id;
    if (config_.session.device_name.isEmpty()) {
        config_.session.device_name = QStringLiteral("Android");
    }

    if (transport_ == nullptr) {
        transport_ = new QtSslTransport(this);
        owns_transport_ = true;
    }
    if (session_ != nullptr) {
        delete session_;
        session_ = nullptr;
    }
    session_ = new SessionClient(transport_, this);
    session_->setConfig(config_.session);

    connect(session_, &SessionClient::loginSucceeded, this,
            &ChatService::onLoginSucceeded);
    connect(session_, &SessionClient::loginFailed, this,
            &ChatService::onLoginFailed);
    connect(session_, &SessionClient::frameReceived, this,
            &ChatService::onFrame);
    connect(session_, &SessionClient::requestFailed, this,
            &ChatService::onRequestFailed);
    connect(transport_, &Transport::connected, this,
            &ChatService::onTransportConnected);
    connect(transport_, &Transport::disconnected, this,
            &ChatService::onTransportDisconnected);

    running_ = true;
    shutting_down_ = false;
    logged_out_ = false;
    reconnect_attempt_ = 0;
    token_.clear();
    resume_token_ = secure_->loadToken();
    setState(QStringLiteral("connecting"));

    QString transport_error;
    if (!transport_->start(config_.session.transport, &transport_error)) {
        running_ = false;
        setState(QStringLiteral("disconnected"), transport_error);
        if (error != nullptr) {
            *error = transport_error;
        }
        return false;
    }
    return true;
}

void ChatService::stop() {
    if (!running_) {
        return;
    }
    shutting_down_ = true;
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
    }
    if (session_ != nullptr) {
        delete session_;
        session_ = nullptr;
    }
    if (transport_ != nullptr) {
        transport_->stop();
    }
    pending_sends_.clear();
    store_.close();
    running_ = false;
    setState(QStringLiteral("disconnected"), QStringLiteral("stopped"));
}

void ChatService::setState(const QString& state, const QString& detail) {
    state_ = state;
    emit stateChanged(state_, detail);
    emitSnapshot();
}

void ChatService::emitSnapshot() {
    emit snapshotChanged(snapshot());
}

void ChatService::emitEvent(QJsonObject event) {
    emit eventReceived(event);
}

void ChatService::onTransportConnected() {
    if (!running_ || shutting_down_ || logged_out_) {
        return;
    }
    if (!resume_token_.isEmpty()) {
        setState(QStringLiteral("authenticating"));
        session_->resume(resume_token_);
        return;
    }
    if (!username_.isEmpty()) {
        setState(QStringLiteral("authenticating"));
        session_->login(username_, password_);
        return;
    }
    setState(QStringLiteral("disconnected"), QStringLiteral("login required"));
}

void ChatService::onTransportDisconnected(const QString& reason) {
    if (!running_ || shutting_down_ || logged_out_) {
        return;
    }
    pending_sends_.clear();
    setState(QStringLiteral("reconnecting"), reason);
    scheduleReconnect();
}

void ChatService::scheduleReconnect() {
    if (reconnect_timer_ == nullptr || reconnect_timer_->isActive() ||
        shutting_down_ || logged_out_) {
        return;
    }
    const int index =
        std::min<int>(reconnect_attempt_,
                      static_cast<int>(sizeof(kReconnectDelays) /
                                       sizeof(kReconnectDelays[0])) -
                          1);
    const int delay_seconds = kReconnectDelays[index];
    ++reconnect_attempt_;
    reconnect_timer_->start(delay_seconds * 1000);
}

void ChatService::handleTerminal(int code, const QString& message) {
    logged_out_ = true;
    resume_token_.clear();
    token_.clear();
    username_.clear();
    password_.clear();
    pending_sends_.clear();
    if (secure_ != nullptr) {
        secure_->clearToken();
    }
    if (reconnect_timer_ != nullptr) {
        reconnect_timer_->stop();
    }
    setState(QStringLiteral("logged_out"), message);
    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("terminal"));
    event.insert(QStringLiteral("code"), code);
    event.insert(QStringLiteral("message"), message);
    emitEvent(event);
    if (transport_ != nullptr) {
        transport_->stop();
    }
}

void ChatService::onLoginFailed(int code, const QString& message) {
    password_.clear();
    if (isTerminalCode(code)) {
        handleTerminal(code, message);
        return;
    }
    setState(QStringLiteral("logged_out"), message);
    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("login_failed"));
    event.insert(QStringLiteral("code"), code);
    event.insert(QStringLiteral("message"), message);
    emitEvent(event);
}

void ChatService::onLoginSucceeded(const LoginResult& result) {
    user_id_ = result.user_id;
    nickname_ = result.nickname;
    server_caps_ = result.server_caps;
    if (!result.token.isEmpty()) {
        token_ = result.token;
        if (secure_ != nullptr) {
            secure_->saveToken(token_);
        }
    }
    resume_token_ = token_;
    reconnect_attempt_ = 0;
    if (!store_.setMeta(QStringLiteral("user_id"),
                        QString::number(user_id_), nullptr) ||
        !store_.setMeta(QStringLiteral("nickname"), nickname_, nullptr) ||
        !store_.setMeta(QStringLiteral("server_caps"),
                        QString::number(server_caps_), nullptr)) {
        // Local meta failures are non-fatal; the in-memory profile is still
        // authoritative for this run.
    }
    setState(QStringLiteral("syncing"));
    requestInitialLists();
}

void ChatService::login(const QString& username, const QString& password) {
    if (!running_ || shutting_down_) {
        return;
    }
    username_ = username;
    password_ = password;
    logged_out_ = false;
    if (transport_ != nullptr && transport_->isConnected()) {
        setState(QStringLiteral("authenticating"));
        session_->login(username_, password_);
        return;
    }
    setState(QStringLiteral("connecting"));
}

void ChatService::logout() {
    if (!running_) {
        return;
    }
    logged_out_ = true;
    token_.clear();
    resume_token_.clear();
    username_.clear();
    password_.clear();
    pending_sends_.clear();
    if (secure_ != nullptr) {
        secure_->clearToken();
    }
    if (session_ != nullptr) {
        session_->logout();
    }
    setState(QStringLiteral("logged_out"), QStringLiteral("logged out"));
    if (transport_ != nullptr) {
        transport_->stop();
    }
}

void ChatService::onRequestFailed(int code, const QString& message) {
    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("request_failed"));
    event.insert(QStringLiteral("code"), code);
    event.insert(QStringLiteral("message"), message);
    emitEvent(event);
}

void ChatService::requestInitialLists() {
    session_->sendRequest(chatbox::kCmdGroupList, std::string());
    session_->sendRequest(chatbox::kCmdPrivateConversations, std::string());
    session_->sendRequest(chatbox::kCmdFriendList, std::string());
    chatbox::ByteWriter profile;
    profile.putU64(user_id_);
    session_->sendRequest(chatbox::kCmdProfileGet, profile.data());
    beginSync();
}

void ChatService::beginSync() {
    QVector<ConversationRow> rows;
    store_.loadConversations(rows, nullptr);
    std::vector<chatbox::SyncCursor> cursors;
    for (const ConversationRow& row : rows) {
        if (row.last_message_id == 0) {
            continue;
        }
        chatbox::SyncCursor cursor;
        cursor.kind = static_cast<uint8_t>(row.kind);
        cursor.target_id = row.target_id;
        cursor.last_message_id = row.last_message_id;
        cursors.push_back(cursor);
    }
    if (cursors.empty()) {
        finishSync();
        return;
    }
    chatbox::ByteWriter writer;
    chatbox::appendSyncPull(writer, cursors, 0);
    if (session_->sendRequest(chatbox::kCmdSyncPull, writer.data()) == 0) {
        finishSync();
        return;
    }
    setState(QStringLiteral("syncing"));
}

void ChatService::finishSync() {
    setState(QStringLiteral("online"));
    flushOutbox();
}

void ChatService::flushOutbox() {
    if (session_ == nullptr || transport_ == nullptr ||
        !transport_->isConnected()) {
        return;
    }
    QVector<OutboxRow> rows;
    if (!store_.pendingOutbox(rows, nullptr)) {
        return;
    }
    for (const OutboxRow& row : rows) {
        chatbox::ByteWriter writer;
        uint16_t command = 0;
        if (row.kind == 1) {
            if (row.target_id > 0xFFFFFFFFULL) {
                continue;
            }
            command = chatbox::kCmdChatSend;
            writer.putU32(static_cast<uint32_t>(row.target_id));
        } else if (row.kind == 2) {
            command = chatbox::kCmdPrivateSend;
            writer.putU64(row.target_id);
        } else {
            continue;
        }
        writer.putString(toUtf8(row.client_msg_id));
        writer.putString(toUtf8(row.content));
        const quint32 seq = session_->sendRequest(command, writer.data());
        if (seq == 0) {
            continue;
        }
        PendingSend pending;
        pending.client_msg_id = row.client_msg_id;
        pending.kind = row.kind;
        pending.target_id = row.target_id;
        pending.content = row.content;
        pending.created_at = row.created_at;
        pending_sends_.insert(seq, pending);
    }
}

void ChatService::onFrame(const QByteArray& raw) {
    chatbox::Frame frame;
    if (!decodeFrame(raw, frame)) {
        return;
    }
    switch (frame.header.cmd) {
        case chatbox::kCmdError:
            handleErrorFrame(frame);
            break;
        case chatbox::kCmdPrivatePush:
            handlePrivatePush(frame);
            break;
        case chatbox::kCmdChatPush:
            handleChatPush(frame);
            break;
        case chatbox::kCmdReadStatePush:
            handleReadState(frame);
            break;
        case chatbox::kCmdMessageEchoPush:
            handleEcho(frame);
            break;
        case chatbox::kCmdPrivateSendResp:
            handleSendResponse(frame, true);
            break;
        case chatbox::kCmdChatSendResp:
            handleSendResponse(frame, false);
            break;
        case chatbox::kCmdSyncResp:
            handleSyncResponse(frame);
            break;
        case chatbox::kCmdPrivateConversations:
            handlePrivateConversations(frame);
            break;
        case chatbox::kCmdProfileGet:
            handleProfile(frame);
            break;
        case chatbox::kCmdGroupPush:
            handleGroupPush(frame);
            break;
        default:
            break;
    }
}

void ChatService::handleErrorFrame(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint16_t wire_code = 0;
    QString message;
    if (!reader.getU16(wire_code) || !readString(reader, message)) {
        wire_code = 2001;
        message = QStringLiteral("malformed error frame");
    }
    const int code = static_cast<int>(wire_code);
    if (isTerminalCode(code)) {
        handleTerminal(code, message);
        return;
    }

    auto pending_it = pending_sends_.find(frame.header.seq);
    if (pending_it != pending_sends_.end()) {
        const PendingSend pending = pending_it.value();
        pending_sends_.erase(pending_it);
        QJsonObject event;
        event.insert(QStringLiteral("type"), QStringLiteral("send_failed"));
        event.insert(QStringLiteral("client_msg_id"), pending.client_msg_id);
        event.insert(QStringLiteral("code"), code);
        event.insert(QStringLiteral("message"), message);
        emitEvent(event);
        return;
    }

    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("error"));
    event.insert(QStringLiteral("code"), code);
    event.insert(QStringLiteral("message"), message);
    emitEvent(event);
}

bool ChatService::saveIncoming(const MessageRow& message, const QString& title,
                               bool increment_unread) {
    QString error;
    if (!store_.saveMessage(message, &error)) {
        return false;
    }
    touchConversation(message, title, increment_unread);
    emitEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message")},
        {QStringLiteral("kind"), message.kind},
        {QStringLiteral("target_id"), static_cast<qint64>(message.target_id)},
        {QStringLiteral("message"), messageToJson(message)}});
    emitSnapshot();
    return true;
}

void ChatService::touchConversation(MessageRow message, const QString& title,
                                    bool increment_unread) {
    QVector<ConversationRow> rows;
    store_.loadConversations(rows, nullptr);
    const ConversationRow* existing =
        findConversation(rows, message.kind, message.target_id);
    quint32 unread = existing != nullptr ? existing->unread : 0;
    const bool is_active =
        active_kind_ == message.kind && active_target_id_ == message.target_id;
    if (is_active) {
        unread = 0;
    } else if (increment_unread) {
        ++unread;
    }

    ConversationRow conversation;
    conversation.kind = message.kind;
    conversation.target_id = message.target_id;
    conversation.title = conversationTitle(
        message.kind, message.target_id,
        existing != nullptr && !existing->title.isEmpty() ? existing->title
                                                          : title);
    conversation.last_message_id = message.message_id;
    conversation.last_content =
        message.recalled ? QString() : message.content;
    conversation.last_time = message.created_at;
    conversation.unread = unread;
    store_.upsertConversation(conversation, nullptr);
}

void ChatService::handlePrivatePush(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint64_t sender_id = 0;
    uint64_t message_id = 0;
    QString nickname;
    QString content;
    QString created_at;
    if (!reader.getU64(sender_id) || !reader.getU64(message_id) ||
        !readString(reader, nickname) || !readString(reader, content) ||
        !readString(reader, created_at)) {
        return;
    }
    MessageRow message;
    message.kind = 2;
    message.target_id = sender_id;
    message.message_id = message_id;
    message.sender_id = sender_id;
    message.nickname = nickname;
    message.content = content;
    message.created_at = created_at;
    if (reader.remaining() >= 8) {
        uint64_t reply_to_id = 0;
        QString reply_sender;
        QString reply_content;
        if (reader.getU64(reply_to_id) && readString(reader, reply_sender) &&
            readString(reader, reply_content)) {
            message.reply_to_id = reply_to_id;
            message.reply_sender_name = reply_sender;
            message.reply_content = reply_content;
        }
    }
    if (reader.remaining() > 0) {
        chatbox::FileMeta meta;
        if (chatbox::readFileMeta(reader, meta) && meta.file_id != 0) {
            message.msg_type = 1;
            message.file_id = meta.file_id;
        }
    }
    if (saveIncoming(message, nickname, true) &&
        active_kind_ == 2 && active_target_id_ == sender_id) {
        markRead(2, sender_id, message_id);
    }
}

void ChatService::handleChatPush(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint32_t room_id = 0;
    uint64_t message_id = 0;
    uint64_t sender_id = 0;
    QString nickname;
    QString content;
    QString created_at;
    if (!reader.getU32(room_id) || !reader.getU64(message_id) ||
        !reader.getU64(sender_id) || !readString(reader, nickname) ||
        !readString(reader, content) || !readString(reader, created_at)) {
        return;
    }
    MessageRow message;
    message.kind = 1;
    message.target_id = room_id;
    message.message_id = message_id;
    message.sender_id = sender_id;
    message.nickname = nickname;
    message.content = content;
    message.created_at = created_at;
    if (reader.remaining() >= 8) {
        uint64_t reply_to_id = 0;
        QString reply_sender;
        QString reply_content;
        if (reader.getU64(reply_to_id) && readString(reader, reply_sender) &&
            readString(reader, reply_content)) {
            message.reply_to_id = reply_to_id;
            message.reply_sender_name = reply_sender;
            message.reply_content = reply_content;
        }
    }
    if (reader.remaining() > 0) {
        chatbox::FileMeta meta;
        if (chatbox::readFileMeta(reader, meta) && meta.file_id != 0) {
            message.msg_type = 1;
            message.file_id = meta.file_id;
        }
    }
    if (saveIncoming(message, QString(), sender_id != user_id_) &&
        active_kind_ == 1 && active_target_id_ == room_id) {
        markRead(1, room_id, message_id);
    }
}

void ChatService::handleReadState(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    chatbox::ReadStateEvent state;
    if (!chatbox::readReadState(reader, state)) {
        return;
    }
    QVector<ConversationRow> rows;
    store_.loadConversations(rows, nullptr);
    const ConversationRow* existing =
        findConversation(rows, state.kind, state.target_id);
    if (existing != nullptr) {
        ConversationRow row = *existing;
        row.unread = state.unread;
        store_.upsertConversation(row, nullptr);
    }
    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("read_state"));
    event.insert(QStringLiteral("kind"), static_cast<int>(state.kind));
    event.insert(QStringLiteral("target_id"),
                 static_cast<qint64>(state.target_id));
    event.insert(QStringLiteral("last_read_message_id"),
                 static_cast<qint64>(state.last_read_message_id));
    event.insert(QStringLiteral("unread"), static_cast<int>(state.unread));
    emitEvent(event);
    emitSnapshot();
}

void ChatService::handleEcho(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint8_t kind = 0;
    uint64_t target_id = 0;
    uint64_t message_id = 0;
    uint64_t sender_id = 0;
    QString nickname;
    QString content;
    QString created_at;
    if (!reader.getU8(kind) || !reader.getU64(target_id) ||
        !reader.getU64(message_id) || !reader.getU64(sender_id) ||
        !readString(reader, nickname) || !readString(reader, content) ||
        !readString(reader, created_at)) {
        return;
    }
    if (kind != 1 && kind != 2) {
        return;
    }
    MessageRow message;
    message.kind = kind;
    message.target_id = target_id;
    message.message_id = message_id;
    message.sender_id = sender_id;
    message.nickname = nickname;
    message.content = content;
    message.created_at = created_at;
    if (reader.remaining() >= 8) {
        uint64_t reply_to_id = 0;
        QString reply_sender;
        QString reply_content;
        if (reader.getU64(reply_to_id) && readString(reader, reply_sender) &&
            readString(reader, reply_content)) {
            message.reply_to_id = reply_to_id;
            message.reply_sender_name = reply_sender;
            message.reply_content = reply_content;
        }
    }
    saveIncoming(message, QString(), false);
}

void ChatService::handleSendResponse(const chatbox::Frame& frame,
                                     bool private_send) {
    Q_UNUSED(private_send);
    auto pending_it = pending_sends_.find(frame.header.seq);
    if (pending_it == pending_sends_.end()) {
        return;
    }
    const PendingSend pending = pending_it.value();
    pending_sends_.erase(pending_it);

    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint64_t message_id = 0;
    QString created_at;
    if (!reader.getU64(message_id) || !readString(reader, created_at)) {
        QJsonObject event;
        event.insert(QStringLiteral("type"), QStringLiteral("send_failed"));
        event.insert(QStringLiteral("client_msg_id"), pending.client_msg_id);
        emitEvent(event);
        return;
    }

    MessageRow message;
    message.kind = pending.kind;
    message.target_id = pending.target_id;
    message.message_id = message_id;
    message.sender_id = user_id_;
    message.nickname = nickname_;
    message.content = pending.content;
    message.created_at = created_at;
    message.client_msg_id = pending.client_msg_id;
    store_.saveMessage(message, nullptr);
    store_.markOutboxSent(pending.client_msg_id, message_id, nullptr);
    touchConversation(message, QString(), false);
    emitEvent(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message_sent")},
        {QStringLiteral("kind"), message.kind},
        {QStringLiteral("target_id"), static_cast<qint64>(message.target_id)},
        {QStringLiteral("message"), messageToJson(message)}});
    emitSnapshot();
}

void ChatService::handleSyncResponse(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint16_t conversation_count = 0;
    uint16_t next_index = 0;
    uint8_t has_more = 0;
    if (!reader.getU16(conversation_count) || !reader.getU16(next_index) ||
        !reader.getU8(has_more)) {
        finishSync();
        return;
    }
    Q_UNUSED(next_index);

    bool more = has_more != 0;
    for (uint16_t i = 0; i < conversation_count; ++i) {
        uint8_t kind = 0;
        uint64_t target_id = 0;
        uint16_t message_count = 0;
        uint8_t has_more_messages = 0;
        uint64_t next_after = 0;
        if (!reader.getU8(kind) || !reader.getU64(target_id) ||
            !reader.getU16(message_count) || !reader.getU8(has_more_messages) ||
            !reader.getU64(next_after)) {
            finishSync();
            return;
        }
        QVector<MessageRow> messages;
        messages.reserve(static_cast<int>(message_count));
        bool valid = true;
        for (uint16_t j = 0; j < message_count; ++j) {
            MessageRow message;
            message.kind = kind;
            message.target_id = target_id;
            uint64_t message_id = 0;
            uint64_t sender_id = 0;
            if (!reader.getU64(message_id) || !reader.getU64(sender_id) ||
                !readString(reader, message.nickname) ||
                !readString(reader, message.content) ||
                !readString(reader, message.created_at)) {
                valid = false;
                break;
            }
            message.message_id = message_id;
            message.sender_id = sender_id;
            uint64_t reply_to_id = 0;
            if (!reader.getU64(reply_to_id) ||
                !readString(reader, message.reply_sender_name) ||
                !readString(reader, message.reply_content)) {
                valid = false;
                break;
            }
            message.reply_to_id = reply_to_id;
            uint8_t recalled = 0;
            if (!reader.getU8(recalled)) {
                valid = false;
                break;
            }
            message.recalled = recalled != 0;
            messages.push_back(message);
        }
        if (!valid) {
            finishSync();
            return;
        }

        uint16_t meta_count = 0;
        if (!reader.getU16(meta_count)) {
            finishSync();
            return;
        }
        for (uint16_t meta_index = 0; meta_index < meta_count; ++meta_index) {
            uint16_t index = 0;
            chatbox::FileMeta meta;
            if (!reader.getU16(index) || !chatbox::readFileMeta(reader, meta)) {
                finishSync();
                return;
            }
            if (index < messages.size() && meta.file_id != 0) {
                messages[index].msg_type = 1;
                messages[index].file_id = meta.file_id;
            }
        }

        QString title;
        for (const MessageRow& message : messages) {
            if (message.kind == 2 && message.sender_id != user_id_) {
                title = message.nickname;
            }
        }
        for (const MessageRow& message : messages) {
            store_.saveMessage(message, nullptr);
        }
        if (!messages.isEmpty()) {
            touchConversation(messages.last(), title, false);
        } else {
            Q_UNUSED(next_after);
        }
        if (has_more_messages != 0) {
            more = true;
        }
    }

    if (more) {
        beginSync();
    } else {
        finishSync();
    }
}

void ChatService::handlePrivateConversations(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint16_t count = 0;
    if (!reader.getU16(count)) {
        return;
    }
    for (uint16_t i = 0; i < count; ++i) {
        uint64_t peer_id = 0;
        QString nickname;
        uint64_t last_message_id = 0;
        QString last_content;
        QString last_time;
        uint8_t online = 0;
        uint32_t unread = 0;
        if (!reader.getU64(peer_id) || !readString(reader, nickname) ||
            !reader.getU64(last_message_id) || !readString(reader, last_content) ||
            !readString(reader, last_time) || !reader.getU8(online) ||
            !reader.getU32(unread)) {
            return;
        }
        Q_UNUSED(online);
        ConversationRow row;
        row.kind = 2;
        row.target_id = peer_id;
        row.title = nickname;
        row.last_message_id = last_message_id;
        row.last_content = last_content;
        row.last_time = last_time;
        row.unread = unread;
        store_.upsertConversation(row, nullptr);
    }
    emitSnapshot();
}

void ChatService::handleProfile(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint64_t profile_id = 0;
    QString username;
    QString nickname;
    QString signature;
    uint32_t avatar_version = 0;
    QString avatar;
    uint8_t online = 0;
    uint8_t relation = 0;
    QString remark;
    if (!reader.getU64(profile_id) || !readString(reader, username) ||
        !readString(reader, nickname) || !readString(reader, signature) ||
        !reader.getU32(avatar_version) || !readString(reader, avatar) ||
        !reader.getU8(online) || !reader.getU8(relation) ||
        !readString(reader, remark)) {
        return;
    }
    Q_UNUSED(username);
    Q_UNUSED(signature);
    Q_UNUSED(avatar_version);
    Q_UNUSED(avatar);
    Q_UNUSED(online);
    Q_UNUSED(relation);
    Q_UNUSED(remark);
    if (profile_id == user_id_ && !nickname.isEmpty()) {
        nickname_ = nickname;
        store_.setMeta(QStringLiteral("nickname"), nickname_, nullptr);
        emitSnapshot();
    }
}

void ChatService::handleGroupPush(const chatbox::Frame& frame) {
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint8_t event_type = 0;
    uint32_t room_id = 0;
    uint64_t target_user_id = 0;
    uint8_t role = 0;
    if (!reader.getU8(event_type) || !reader.getU32(room_id) ||
        !reader.getU64(target_user_id) || !reader.getU8(role)) {
        return;
    }
    QJsonObject event;
    event.insert(QStringLiteral("type"), QStringLiteral("group_event"));
    event.insert(QStringLiteral("event"), static_cast<int>(event_type));
    event.insert(QStringLiteral("room_id"), static_cast<qint64>(room_id));
    event.insert(QStringLiteral("user_id"),
                 static_cast<qint64>(target_user_id));
    event.insert(QStringLiteral("role"), static_cast<int>(role));
    emitEvent(event);
}

void ChatService::sendText(int kind, quint64 target_id,
                           const QString& content) {
    if (content.isEmpty() || target_id == 0 || !running_) {
        return;
    }
    const QString client_msg_id =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString created_at = utcNow();
    OutboxRow row;
    row.client_msg_id = client_msg_id;
    row.kind = kind;
    row.target_id = target_id;
    row.content = content;
    row.created_at = created_at;
    row.state = QStringLiteral("pending");
    QString error;
    if (!store_.enqueueOutbox(row, &error)) {
        QJsonObject event;
        event.insert(QStringLiteral("type"), QStringLiteral("send_failed"));
        event.insert(QStringLiteral("client_msg_id"), client_msg_id);
        event.insert(QStringLiteral("message"), error);
        emitEvent(event);
        return;
    }

    chatbox::ByteWriter writer;
    uint16_t command = 0;
    if (kind == 1) {
        if (target_id > 0xFFFFFFFFULL) {
            return;
        }
        command = chatbox::kCmdChatSend;
        writer.putU32(static_cast<uint32_t>(target_id));
    } else if (kind == 2) {
        command = chatbox::kCmdPrivateSend;
        writer.putU64(target_id);
    } else {
        return;
    }
    writer.putString(toUtf8(client_msg_id));
    writer.putString(toUtf8(content));

    if (transport_ == nullptr || !transport_->isConnected()) {
        emitSnapshot();
        return;
    }
    const quint32 seq = session_->sendRequest(command, writer.data());
    if (seq == 0) {
        emitSnapshot();
        return;
    }
    PendingSend pending;
    pending.client_msg_id = client_msg_id;
    pending.kind = kind;
    pending.target_id = target_id;
    pending.content = content;
    pending.created_at = created_at;
    pending_sends_.insert(seq, pending);
    emitSnapshot();
}

void ChatService::markRead(int kind, quint64 target_id, quint64 message_id) {
    if (!running_ || target_id == 0 || message_id == 0) {
        return;
    }
    QVector<ConversationRow> rows;
    store_.loadConversations(rows, nullptr);
    const ConversationRow* existing = findConversation(rows, kind, target_id);
    if (existing != nullptr && existing->unread != 0) {
        ConversationRow row = *existing;
        row.unread = 0;
        store_.upsertConversation(row, nullptr);
        emitSnapshot();
    }

    chatbox::ByteWriter writer;
    uint16_t command = 0;
    if (kind == 1) {
        if (target_id > 0xFFFFFFFFULL) {
            return;
        }
        command = chatbox::kCmdGroupMarkRead;
        writer.putU32(static_cast<uint32_t>(target_id));
    } else if (kind == 2) {
        command = chatbox::kCmdPrivateMarkRead;
        writer.putU64(target_id);
    } else {
        return;
    }
    writer.putU64(message_id);
    if (session_ != nullptr && transport_ != nullptr &&
        transport_->isConnected()) {
        session_->sendRequest(command, writer.data());
    }
}

void ChatService::setActiveConversation(int kind, quint64 target_id) {
    active_kind_ = kind;
    active_target_id_ = target_id;
    emitSnapshot();
}

QJsonObject ChatService::messageToJson(const MessageRow& message) const {
    QJsonObject object;
    object.insert(QStringLiteral("message_id"),
                  static_cast<qint64>(message.message_id));
    object.insert(QStringLiteral("sender_id"),
                  static_cast<qint64>(message.sender_id));
    object.insert(QStringLiteral("nickname"), message.nickname);
    object.insert(QStringLiteral("content"), message.content);
    object.insert(QStringLiteral("created_at"), message.created_at);
    object.insert(QStringLiteral("recalled"), message.recalled);
    object.insert(QStringLiteral("msg_type"), static_cast<int>(message.msg_type));
    object.insert(QStringLiteral("file_id"), static_cast<qint64>(message.file_id));
    object.insert(QStringLiteral("client_msg_id"), message.client_msg_id);
    object.insert(QStringLiteral("reply_to_id"),
                  static_cast<qint64>(message.reply_to_id));
    object.insert(QStringLiteral("reply_sender_name"),
                  message.reply_sender_name);
    object.insert(QStringLiteral("reply_content"), message.reply_content);
    object.insert(QStringLiteral("outgoing"), message.sender_id == user_id_);
    return object;
}

QJsonObject ChatService::snapshot() const {
    QJsonObject object;
    object.insert(QStringLiteral("state"), state_);
    object.insert(QStringLiteral("user_id"), static_cast<qint64>(user_id_));
    object.insert(QStringLiteral("nickname"), nickname_);
    object.insert(QStringLiteral("server_caps"),
                  static_cast<qint64>(server_caps_));

    QVector<ConversationRow> rows;
    store_.loadConversations(rows, nullptr);
    QJsonArray conversations;
    for (const ConversationRow& row : rows) {
        QJsonObject item;
        item.insert(QStringLiteral("kind"), row.kind);
        item.insert(QStringLiteral("target_id"),
                    static_cast<qint64>(row.target_id));
        item.insert(QStringLiteral("title"), row.title);
        item.insert(QStringLiteral("last_message_id"),
                    static_cast<qint64>(row.last_message_id));
        item.insert(QStringLiteral("last_content"), row.last_content);
        item.insert(QStringLiteral("last_time"), row.last_time);
        item.insert(QStringLiteral("unread"), static_cast<int>(row.unread));
        conversations.append(item);
    }
    object.insert(QStringLiteral("conversations"), conversations);

    QJsonObject active;
    active.insert(QStringLiteral("kind"), active_kind_);
    active.insert(QStringLiteral("target_id"),
                  static_cast<qint64>(active_target_id_));
    QJsonArray messages;
    if (active_kind_ == 1 || active_kind_ == 2) {
        QVector<MessageRow> recent;
        store_.loadRecentMessages(active_kind_, active_target_id_,
                                  kMaxRecentMessages, recent, nullptr);
        for (const MessageRow& message : recent) {
            messages.append(messageToJson(message));
        }
    }
    active.insert(QStringLiteral("messages"), messages);
    object.insert(QStringLiteral("active"), active);
    return object;
}

}  // namespace clientcore
}  // namespace chatbox

