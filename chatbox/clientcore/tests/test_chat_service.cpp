#include "test_chat_service.h"

#include <QtTest>

#include <string>
#include <utility>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "fake_transport.h"
#include "secure/secure_store.h"
#include "service/chat_service.h"
#include "store/mobile_store.h"

using chatbox::clientcore::ChatService;
using chatbox::clientcore::ConversationRow;
using chatbox::clientcore::MemorySecureStore;
using chatbox::clientcore::MobileStore;
using chatbox::clientcore::OutboxRow;
using chatbox::clientcore::ServiceConfig;

namespace {

constexpr quint32 kAllCaps =
    chatbox::kCapMultiDevice | chatbox::kCapSyncPull |
    chatbox::kCapReadStatePush | chatbox::kCapMessageEcho |
    chatbox::kCapSessionManage | chatbox::kCapDeviceMetadata;

QString makeToken() {
    return QString(64, QLatin1Char('b'));
}

int findFrame(const FakeTransport& transport, uint16_t command) {
    for (int i = transport.sent.size() - 1; i >= 0; --i) {
        const chatbox::Frame frame = transport.sentFrame(i);
        if (frame.header.cmd == command) {
            return i;
        }
    }
    return -1;
}

chatbox::Frame makeAuthResponse(uint16_t command, uint32_t seq,
                                quint64 user_id, const QString& nickname,
                                const QString& token) {
    chatbox::Frame frame;
    frame.header.flags = chatbox::kFlagResponse;
    frame.header.cmd = command;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU64(user_id);
    writer.putString(nickname.toStdString());
    if (command == chatbox::kCmdLogin) {
        writer.putString(token.toStdString());
    }
    chatbox::appendAuthExtras(writer, kAllCaps, 3600);
    frame.body = writer.data();
    return frame;
}

chatbox::Frame makePrivatePush(quint64 sender_id, quint64 message_id,
                               const QString& nickname, const QString& content,
                               const QString& created_at) {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdPrivatePush;
    chatbox::ByteWriter writer;
    writer.putU64(sender_id);
    writer.putU64(message_id);
    writer.putString(nickname.toStdString());
    writer.putString(content.toStdString());
    writer.putString(created_at.toStdString());
    writer.putU64(0);
    writer.putString("");
    writer.putString("");
    frame.body = writer.data();
    return frame;
}

chatbox::Frame makeReadState(uint8_t kind, quint64 target_id,
                             quint64 last_read, quint32 unread) {
    chatbox::Frame frame;
    frame.header.cmd = chatbox::kCmdReadStatePush;
    chatbox::ReadStateEvent state;
    state.kind = kind;
    state.target_id = target_id;
    state.last_read_message_id = last_read;
    state.unread = unread;
    chatbox::ByteWriter writer;
    chatbox::appendReadState(writer, state);
    frame.body = writer.data();
    return frame;
}

chatbox::Frame makeSendResponse(uint16_t command, uint32_t seq,
                                quint64 message_id, const QString& created_at) {
    chatbox::Frame frame;
    frame.header.flags = chatbox::kFlagResponse;
    frame.header.cmd = command;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU64(message_id);
    writer.putString(created_at.toStdString());
    frame.body = writer.data();
    return frame;
}

chatbox::Frame makeSyncResponse(
    uint32_t seq, int kind, quint64 target_id,
    const std::vector<std::pair<quint64, QString>>& messages,
    quint64 next_after) {
    chatbox::Frame frame;
    frame.header.flags = chatbox::kFlagResponse;
    frame.header.cmd = chatbox::kCmdSyncResp;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU16(1);
    writer.putU16(1);
    writer.putU8(0);
    writer.putU8(static_cast<uint8_t>(kind));
    writer.putU64(target_id);
    writer.putU16(static_cast<uint16_t>(messages.size()));
    writer.putU8(0);
    writer.putU64(next_after);
    for (const auto& message : messages) {
        writer.putU64(message.first);
        writer.putU64(kind == 2 ? 9 : 1);
        writer.putString("Bob");
        writer.putString(message.second.toStdString());
        writer.putString("2026-10-29 10:00:00");
        writer.putU64(0);
        writer.putString("");
        writer.putString("");
        writer.putU8(0);
    }
    writer.putU16(0);
    frame.body = writer.data();
    return frame;
}

ServiceConfig makeConfig(const QString& db_path, MemorySecureStore& secure) {
    ServiceConfig config;
    config.session.transport.host = QStringLiteral("test");
    config.session.transport.port = 1;
    config.session.transport.use_tls = false;
    config.session.device_id = QStringLiteral("device-1");
    config.session.device_name = QStringLiteral("Pixel");
    config.session.platform = QStringLiteral("android");
    config.session.app_version = QStringLiteral("0.1.0");
    config.db_path = db_path;
    config.secure_store = &secure;
    return config;
}

bool loginAndSucceed(ChatService& service, FakeTransport& transport,
                     quint64 user_id, const QString& nickname,
                     const QString& token) {
    service.login(QStringLiteral("alice"), QStringLiteral("secret"));
    const int index = findFrame(transport, chatbox::kCmdLogin);
    if (index < 0) {
        return false;
    }
    const chatbox::Frame request = transport.sentFrame(index);
    transport.deliver(makeAuthResponse(chatbox::kCmdLogin, request.header.seq,
                                       user_id, nickname, token));
    return true;
}

QJsonObject conversationFor(const QJsonObject& snapshot, quint64 target_id) {
    const QJsonArray conversations =
        snapshot.value(QStringLiteral("conversations")).toArray();
    for (const QJsonValue& value : conversations) {
        const QJsonObject item = value.toObject();
        if (static_cast<quint64>(
                item.value(QStringLiteral("target_id")).toInteger()) ==
            target_id) {
            return item;
        }
    }
    return QJsonObject();
}

}  // namespace

void TestChatService::loginStoresProfileAndRequestsLists() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeTransport transport;
    MemorySecureStore secure;
    ChatService service(&transport);
    QString error;
    QVERIFY2(service.start(makeConfig(dir.filePath("chatbox.db"), secure), &error),
             qPrintable(error));

    const QString token = makeToken();
    QVERIFY(loginAndSucceed(service, transport, 42, QStringLiteral("Alice"),
                            token));

    const QJsonObject snapshot = service.snapshot();
    QCOMPARE(snapshot.value(QStringLiteral("state")).toString(),
             QStringLiteral("online"));
    QCOMPARE(snapshot.value(QStringLiteral("user_id")).toInteger(),
             static_cast<qint64>(42));
    QCOMPARE(snapshot.value(QStringLiteral("nickname")).toString(),
             QStringLiteral("Alice"));
    QCOMPARE(secure.loadToken(), token);

    QVERIFY(findFrame(transport, chatbox::kCmdGroupList) >= 0);
    QVERIFY(findFrame(transport, chatbox::kCmdPrivateConversations) >= 0);
    QVERIFY(findFrame(transport, chatbox::kCmdFriendList) >= 0);
    const int profile_index =
        findFrame(transport, chatbox::kCmdProfileGet);
    QVERIFY(profile_index >= 0);
    const chatbox::Frame profile = transport.sentFrame(profile_index);
    chatbox::ByteReader reader(profile.body.data(), profile.body.size());
    uint64_t target_id = 0;
    QVERIFY(reader.getU64(target_id));
    QCOMPARE(target_id, static_cast<uint64_t>(42));
}

void TestChatService::loginSavesTokenAndResumeUsesIt() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("chatbox.db");
    const QString token = makeToken();
    MemorySecureStore secure;
    {
        FakeTransport transport;
        ChatService service(&transport);
        QString error;
        QVERIFY2(service.start(makeConfig(path, secure), &error),
                 qPrintable(error));
        QVERIFY(loginAndSucceed(service, transport, 42,
                                QStringLiteral("Alice"), token));
    }
    QCOMPARE(secure.loadToken(), token);

    FakeTransport transport;
    ChatService service(&transport);
    QString error;
    QVERIFY2(service.start(makeConfig(path, secure), &error), qPrintable(error));

    const int resume_index = findFrame(transport, chatbox::kCmdResume);
    QVERIFY(resume_index >= 0);
    const chatbox::Frame request = transport.sentFrame(resume_index);
    chatbox::ByteReader reader(request.body.data(), request.body.size());
    std::string wire_token;
    QVERIFY(reader.getString(wire_token));
    QCOMPARE(QString::fromStdString(wire_token), token);
    chatbox::DeviceInfo device;
    bool present = false;
    QVERIFY(chatbox::readDeviceInfo(reader, device, present));
    QVERIFY(present);
    QCOMPARE(QString::fromStdString(device.device_id),
             QStringLiteral("device-1"));
    QCOMPARE(service.snapshot().value(QStringLiteral("state")).toString(),
             QStringLiteral("authenticating"));
}

void TestChatService::terminalErrorClearsToken() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeTransport transport;
    MemorySecureStore secure;
    ChatService service(&transport);
    QString error;
    QVERIFY2(service.start(makeConfig(dir.filePath("chatbox.db"), secure), &error),
             qPrintable(error));
    const QString token = makeToken();
    QVERIFY(loginAndSucceed(service, transport, 42, QStringLiteral("Alice"),
                            token));
    QCOMPARE(secure.loadToken(), token);

    chatbox::Frame frame;
    frame.header.flags = chatbox::kFlagResponse | chatbox::kFlagError;
    frame.header.cmd = chatbox::kCmdError;
    frame.header.seq = 999;
    chatbox::ByteWriter writer;
    writer.putU16(1002);
    writer.putString("session revoked");
    frame.body = writer.data();
    transport.deliver(frame);

    const QJsonObject snapshot = service.snapshot();
    QCOMPARE(snapshot.value(QStringLiteral("state")).toString(),
             QStringLiteral("logged_out"));
    QVERIFY(secure.loadToken().isEmpty());
}

void TestChatService::incomingPrivatePushPersistsAndEmits() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeTransport transport;
    MemorySecureStore secure;
    ChatService service(&transport);
    QString error;
    QVERIFY2(service.start(makeConfig(dir.filePath("chatbox.db"), secure), &error),
             qPrintable(error));
    QVERIFY(loginAndSucceed(service, transport, 42, QStringLiteral("Alice"),
                            makeToken()));

    QSignalSpy events(&service, &ChatService::eventReceived);
    transport.deliver(makePrivatePush(9, 100, QStringLiteral("Bob"),
                                      QStringLiteral("hello"),
                                      QStringLiteral("2026-10-29 10:00:00")));
    QCOMPARE(events.count(), 1);
    const QJsonObject event = events.at(0).at(0).toJsonObject();
    QCOMPARE(event.value(QStringLiteral("type")).toString(),
             QStringLiteral("message"));
    QCOMPARE(event.value(QStringLiteral("message"))
                 .toObject()
                 .value(QStringLiteral("content"))
                 .toString(),
             QStringLiteral("hello"));

    service.setActiveConversation(2, 9);
    const QJsonObject snapshot = service.snapshot();
    const QJsonObject conversation = conversationFor(snapshot, 9);
    QVERIFY(!conversation.isEmpty());
    QCOMPARE(conversation.value(QStringLiteral("unread")).toInt(), 1);
    QCOMPARE(conversation.value(QStringLiteral("last_message_id")).toInteger(),
             static_cast<qint64>(100));
    const QJsonArray messages =
        snapshot.value(QStringLiteral("active"))
            .toObject()
            .value(QStringLiteral("messages"))
            .toArray();
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("message_id")).toInteger(),
             static_cast<qint64>(100));
}

void TestChatService::sendTextCreatesOutboxAndFrame() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("chatbox.db");
    FakeTransport transport;
    MemorySecureStore secure;
    ChatService service(&transport);
    QString error;
    QVERIFY2(service.start(makeConfig(path, secure), &error), qPrintable(error));
    QVERIFY(loginAndSucceed(service, transport, 42, QStringLiteral("Alice"),
                            makeToken()));

    service.sendText(2, 9, QStringLiteral("hi"));
    const int index = findFrame(transport, chatbox::kCmdPrivateSend);
    QVERIFY(index >= 0);
    const chatbox::Frame frame = transport.sentFrame(index);
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    uint64_t receiver = 0;
    std::string client_msg_id;
    std::string content;
    QVERIFY(reader.getU64(receiver));
    QVERIFY(reader.getString(client_msg_id));
    QVERIFY(reader.getString(content));
    QCOMPARE(receiver, static_cast<uint64_t>(9));
    QCOMPARE(QString::fromStdString(content), QStringLiteral("hi"));
    QVERIFY(!client_msg_id.empty());

    MobileStore inspector;
    QVERIFY2(inspector.open(path, &error), qPrintable(error));
    QVector<OutboxRow> pending;
    QVERIFY2(inspector.pendingOutbox(pending, &error), qPrintable(error));
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending.at(0).client_msg_id,
             QString::fromStdString(client_msg_id));
    QCOMPARE(pending.at(0).kind, 2);
    QCOMPARE(pending.at(0).target_id, static_cast<quint64>(9));
}

void TestChatService::sendAckMarksOutboxSent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("chatbox.db");
    FakeTransport transport;
    MemorySecureStore secure;
    ChatService service(&transport);
    QString error;
    QVERIFY2(service.start(makeConfig(path, secure), &error), qPrintable(error));
    QVERIFY(loginAndSucceed(service, transport, 42, QStringLiteral("Alice"),
                            makeToken()));

    QSignalSpy events(&service, &ChatService::eventReceived);
    service.sendText(2, 9, QStringLiteral("hi"));
    const int index = findFrame(transport, chatbox::kCmdPrivateSend);
    QVERIFY(index >= 0);
    const chatbox::Frame request = transport.sentFrame(index);
    transport.deliver(makeSendResponse(chatbox::kCmdPrivateSendResp,
                                       request.header.seq, 555,
                                       QStringLiteral("2026-10-29 10:00:01")));

    MobileStore inspector;
    QVERIFY2(inspector.open(path, &error), qPrintable(error));
    QVector<OutboxRow> pending;
    QVERIFY2(inspector.pendingOutbox(pending, &error), qPrintable(error));
    QVERIFY(pending.isEmpty());

    service.setActiveConversation(2, 9);
    const QJsonArray messages =
        service.snapshot()
            .value(QStringLiteral("active"))
            .toObject()
            .value(QStringLiteral("messages"))
            .toArray();
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("message_id")).toInteger(),
             static_cast<qint64>(555));
    QVERIFY(!events.isEmpty());
    QCOMPARE(events.last().at(0).toJsonObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("message_sent"));
}

void TestChatService::readStatePushUpdatesUnread() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FakeTransport transport;
    MemorySecureStore secure;
    ChatService service(&transport);
    QString error;
    QVERIFY2(service.start(makeConfig(dir.filePath("chatbox.db"), secure), &error),
             qPrintable(error));
    QVERIFY(loginAndSucceed(service, transport, 42, QStringLiteral("Alice"),
                            makeToken()));

    transport.deliver(makePrivatePush(9, 100, QStringLiteral("Bob"),
                                      QStringLiteral("hello"),
                                      QStringLiteral("2026-10-29 10:00:00")));
    QCOMPARE(conversationFor(service.snapshot(), 9)
                 .value(QStringLiteral("unread"))
                 .toInt(),
             1);

    QSignalSpy events(&service, &ChatService::eventReceived);
    transport.deliver(makeReadState(2, 9, 100, 0));
    QCOMPARE(events.count(), 1);
    QCOMPARE(events.at(0).at(0).toJsonObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("read_state"));
    QCOMPARE(conversationFor(service.snapshot(), 9)
                 .value(QStringLiteral("unread"))
                 .toInt(),
             0);
}

void TestChatService::syncPullAppliesAfterCursor() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("chatbox.db");
    {
        MobileStore seed;
        QString error;
        QVERIFY2(seed.open(path, &error), qPrintable(error));
        ConversationRow row;
        row.kind = 2;
        row.target_id = 9;
        row.title = QStringLiteral("Bob");
        row.last_message_id = 4;
        row.last_content = QStringLiteral("old");
        row.last_time = QStringLiteral("2026-10-29 09:00:00");
        QVERIFY2(seed.upsertConversation(row, &error), qPrintable(error));
    }

    FakeTransport transport;
    MemorySecureStore secure;
    ChatService service(&transport);
    QString error;
    QVERIFY2(service.start(makeConfig(path, secure), &error), qPrintable(error));
    QVERIFY(loginAndSucceed(service, transport, 42, QStringLiteral("Alice"),
                            makeToken()));

    const int sync_index = findFrame(transport, chatbox::kCmdSyncPull);
    QVERIFY(sync_index >= 0);
    const chatbox::Frame request = transport.sentFrame(sync_index);
    chatbox::ByteReader reader(request.body.data(), request.body.size());
    uint16_t count = 0;
    uint16_t start_index = 0;
    QVERIFY(reader.getU16(count));
    QVERIFY(reader.getU16(start_index));
    QCOMPARE(count, static_cast<uint16_t>(1));
    QCOMPARE(start_index, static_cast<uint16_t>(0));
    uint8_t kind = 0;
    uint64_t target_id = 0;
    uint64_t last_message_id = 0;
    QVERIFY(reader.getU8(kind));
    QVERIFY(reader.getU64(target_id));
    QVERIFY(reader.getU64(last_message_id));
    QCOMPARE(kind, static_cast<uint8_t>(2));
    QCOMPARE(target_id, static_cast<uint64_t>(9));
    QCOMPARE(last_message_id, static_cast<uint64_t>(4));

    const chatbox::Frame response = makeSyncResponse(
        request.header.seq, 2, 9,
        {{5, QStringLiteral("m5")}, {6, QStringLiteral("m6")}}, 6);
    transport.deliver(response);

    service.setActiveConversation(2, 9);
    QJsonArray messages = service.snapshot()
                              .value(QStringLiteral("active"))
                              .toObject()
                              .value(QStringLiteral("messages"))
                              .toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value(QStringLiteral("message_id")).toInteger(),
             static_cast<qint64>(5));
    QCOMPARE(messages.at(1).toObject().value(QStringLiteral("message_id")).toInteger(),
             static_cast<qint64>(6));

    transport.deliver(response);
    messages = service.snapshot()
                   .value(QStringLiteral("active"))
                   .toObject()
                   .value(QStringLiteral("messages"))
                   .toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(conversationFor(service.snapshot(), 9)
                 .value(QStringLiteral("last_message_id"))
                 .toInteger(),
             static_cast<qint64>(6));
    QCOMPARE(service.snapshot().value(QStringLiteral("state")).toString(),
             QStringLiteral("online"));
}

