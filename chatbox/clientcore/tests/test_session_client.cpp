#include <QtTest>

#include <string>
#include <vector>

#include "common/session_protocol.h"
#include "fake_transport.h"
#include "session/session_client.h"

using chatbox::clientcore::LoginResult;
using chatbox::clientcore::SessionClient;
using chatbox::clientcore::SessionConfig;

namespace {

constexpr uint32_t kAllCaps =
    chatbox::kCapMultiDevice | chatbox::kCapSyncPull |
    chatbox::kCapReadStatePush | chatbox::kCapMessageEcho |
    chatbox::kCapSessionManage | chatbox::kCapDeviceMetadata;

QString makeToken() {
    return QString(64, QLatin1Char('a'));
}

chatbox::Frame makeAuthResponse(uint16_t command, uint32_t seq,
                                quint64 user_id, const QString& nickname,
                                const QString& token, bool with_extras,
                                quint32 caps, quint32 ttl) {
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
    if (with_extras) {
        chatbox::appendAuthExtras(writer, caps, ttl);
    }
    frame.body = writer.data();
    return frame;
}

chatbox::Frame makeErrorResponse(uint32_t seq, uint16_t code,
                                 const QString& message) {
    chatbox::Frame frame;
    frame.header.flags = chatbox::kFlagResponse | chatbox::kFlagError;
    frame.header.cmd = chatbox::kCmdError;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    writer.putU16(code);
    writer.putString(message.toStdString());
    frame.body = writer.data();
    return frame;
}

chatbox::Frame makeSessionListResponse(
    uint32_t seq, const std::vector<chatbox::SessionInfo>& sessions) {
    chatbox::Frame frame;
    frame.header.flags = chatbox::kFlagResponse;
    frame.header.cmd = chatbox::kCmdSessionList;
    frame.header.seq = seq;
    chatbox::ByteWriter writer;
    chatbox::appendSessionList(writer, sessions);
    frame.body = writer.data();
    return frame;
}

}  // namespace

class TestSessionClient : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void loginSendsDeviceInfoAndCaps();
    void resumeSendsTokenAndDeviceInfo();
    void parsesAuthExtras();
    void legacyAuthResponseLeavesCapsZero();
    void loginErrorEmitsLoginFailed();
    void sessionListRoundTrip();
    void revokeSendsSessionId();
    void revokeFailureEmitsRequestFailed();
    void unhandledFrameIsForwarded();
};

void TestSessionClient::initTestCase() {
    qRegisterMetaType<LoginResult>("chatbox::clientcore::LoginResult");
    qRegisterMetaType<chatbox::SessionInfo>("chatbox::SessionInfo");
    qRegisterMetaType<QVector<chatbox::SessionInfo>>(
        "QVector<chatbox::SessionInfo>");
}

void TestSessionClient::loginSendsDeviceInfoAndCaps() {
    FakeTransport transport;
    SessionClient client(&transport);
    SessionConfig config;
    config.device_id = QStringLiteral("device-1");
    config.device_name = QStringLiteral("Pixel");
    config.platform = QStringLiteral("android");
    config.app_version = QStringLiteral("0.1.0");
    client.setConfig(config);

    client.login(QStringLiteral("alice"), QStringLiteral("secret"));

    QCOMPARE(transport.sent.size(), 1);
    const chatbox::Frame frame = transport.sentFrame(0);
    QCOMPARE(frame.header.cmd, static_cast<uint16_t>(chatbox::kCmdLogin));
    QCOMPARE(frame.header.seq, 1u);

    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    std::string username;
    std::string password;
    QVERIFY(reader.getString(username));
    QVERIFY(reader.getString(password));
    QCOMPARE(QString::fromStdString(username), QStringLiteral("alice"));
    QCOMPARE(QString::fromStdString(password), QStringLiteral("secret"));

    chatbox::DeviceInfo device;
    bool present = false;
    QVERIFY(chatbox::readDeviceInfo(reader, device, present));
    QVERIFY(present);
    QCOMPARE(QString::fromStdString(device.device_id), QStringLiteral("device-1"));
    QCOMPARE(QString::fromStdString(device.device_name), QStringLiteral("Pixel"));
    QCOMPARE(QString::fromStdString(device.platform), QStringLiteral("android"));
    QCOMPARE(QString::fromStdString(device.app_version), QStringLiteral("0.1.0"));
    QCOMPARE(device.caps, kAllCaps);
}

void TestSessionClient::resumeSendsTokenAndDeviceInfo() {
    FakeTransport transport;
    SessionClient client(&transport);
    SessionConfig config;
    config.device_id = QStringLiteral("device-2");
    config.device_name = QStringLiteral("Pixel");
    config.app_version = QStringLiteral("0.1.0");
    client.setConfig(config);

    const QString token = makeToken();
    client.resume(token);

    QCOMPARE(transport.sent.size(), 1);
    const chatbox::Frame frame = transport.sentFrame(0);
    QCOMPARE(frame.header.cmd, static_cast<uint16_t>(chatbox::kCmdResume));
    chatbox::ByteReader reader(frame.body.data(), frame.body.size());
    std::string wire_token;
    QVERIFY(reader.getString(wire_token));
    QCOMPARE(QString::fromStdString(wire_token), token);
    chatbox::DeviceInfo device;
    bool present = false;
    QVERIFY(chatbox::readDeviceInfo(reader, device, present));
    QVERIFY(present);
    QCOMPARE(QString::fromStdString(device.device_id), QStringLiteral("device-2"));
    QCOMPARE(device.caps, kAllCaps);
}

void TestSessionClient::parsesAuthExtras() {
    FakeTransport transport;
    SessionClient client(&transport);
    QSignalSpy spy(&client, &SessionClient::loginSucceeded);
    client.login(QStringLiteral("alice"), QStringLiteral("secret"));
    const chatbox::Frame request = transport.sentFrame(0);
    const QString token = makeToken();

    transport.deliver(makeAuthResponse(chatbox::kCmdLogin, request.header.seq, 42,
                                       QStringLiteral("Alice"), token, true,
                                       chatbox::kCapSyncPull, 3600));
    QCOMPARE(spy.count(), 1);
    const LoginResult result = spy.takeFirst().at(0).value<LoginResult>();
    QCOMPARE(result.user_id, static_cast<quint64>(42));
    QCOMPARE(result.nickname, QStringLiteral("Alice"));
    QCOMPARE(result.token, token);
    QCOMPARE(result.server_caps, static_cast<quint32>(chatbox::kCapSyncPull));
    QCOMPARE(result.ttl_seconds, static_cast<quint32>(3600));
}

void TestSessionClient::legacyAuthResponseLeavesCapsZero() {
    FakeTransport transport;
    SessionClient client(&transport);
    QSignalSpy spy(&client, &SessionClient::loginSucceeded);
    client.login(QStringLiteral("alice"), QStringLiteral("secret"));
    const chatbox::Frame request = transport.sentFrame(0);
    const QString token = makeToken();

    transport.deliver(makeAuthResponse(chatbox::kCmdLogin, request.header.seq, 7,
                                       QStringLiteral("Alice"), token, false, 0, 0));
    QCOMPARE(spy.count(), 1);
    const LoginResult result = spy.takeFirst().at(0).value<LoginResult>();
    QCOMPARE(result.server_caps, static_cast<quint32>(0));
    QCOMPARE(result.ttl_seconds, static_cast<quint32>(0));
    QCOMPARE(result.token, token);
}

void TestSessionClient::loginErrorEmitsLoginFailed() {
    FakeTransport transport;
    SessionClient client(&transport);
    QSignalSpy spy(&client, &SessionClient::loginFailed);
    client.login(QStringLiteral("alice"), QStringLiteral("wrong"));
    const chatbox::Frame request = transport.sentFrame(0);

    transport.deliver(makeErrorResponse(request.header.seq, 1004,
                                        QStringLiteral("invalid username or password")));
    QCOMPARE(spy.count(), 1);
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 1004);
    QCOMPARE(args.at(1).toString(), QStringLiteral("invalid username or password"));
}

void TestSessionClient::sessionListRoundTrip() {
    FakeTransport transport;
    SessionClient client(&transport);
    QSignalSpy spy(&client, &SessionClient::sessionListReceived);
    client.requestSessionList();
    const chatbox::Frame request = transport.sentFrame(0);
    QCOMPARE(request.header.cmd, static_cast<uint16_t>(chatbox::kCmdSessionList));

    chatbox::SessionInfo first;
    first.session_id = "sid-1";
    first.device_id = "device-1";
    first.device_name = "Pixel";
    first.platform = "android";
    first.app_version = "0.1.0";
    first.last_seen_at = "2026-10-29 10:00:00";
    first.current = true;
    chatbox::SessionInfo second;
    second.session_id = "sid-2";
    second.device_id = "device-2";
    second.device_name = "Desktop";
    second.platform = "windows";

    transport.deliver(makeSessionListResponse(request.header.seq, {first, second}));
    QCOMPARE(spy.count(), 1);
    const QVector<chatbox::SessionInfo> sessions =
        spy.takeFirst().at(0).value<QVector<chatbox::SessionInfo>>();
    QCOMPARE(sessions.size(), 2);
    QCOMPARE(QString::fromStdString(sessions[0].session_id), QStringLiteral("sid-1"));
    QVERIFY(sessions[0].current);
    QCOMPARE(QString::fromStdString(sessions[1].device_name), QStringLiteral("Desktop"));
    QVERIFY(!sessions[1].current);
}

void TestSessionClient::revokeSendsSessionId() {
    FakeTransport transport;
    SessionClient client(&transport);
    QSignalSpy spy(&client, &SessionClient::sessionRevoked);
    client.revokeSession(QStringLiteral("sid-1"));
    const chatbox::Frame request = transport.sentFrame(0);
    QCOMPARE(request.header.cmd, static_cast<uint16_t>(chatbox::kCmdSessionRevoke));
    chatbox::ByteReader reader(request.body.data(), request.body.size());
    std::string session_id;
    QVERIFY(reader.getString(session_id));
    QCOMPARE(QString::fromStdString(session_id), QStringLiteral("sid-1"));

    chatbox::Frame response;
    response.header.flags = chatbox::kFlagResponse;
    response.header.cmd = chatbox::kCmdSessionRevoke;
    response.header.seq = request.header.seq;
    transport.deliver(response);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("sid-1"));
}

void TestSessionClient::revokeFailureEmitsRequestFailed() {
    FakeTransport transport;
    SessionClient client(&transport);
    QSignalSpy spy(&client, &SessionClient::requestFailed);
    client.revokeSession(QStringLiteral("sid-404"));
    const chatbox::Frame request = transport.sentFrame(0);
    transport.deliver(makeErrorResponse(request.header.seq, 1001,
                                        QStringLiteral("session not found")));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 1001);
}

void TestSessionClient::unhandledFrameIsForwarded() {
    FakeTransport transport;
    SessionClient client(&transport);
    QSignalSpy spy(&client, &SessionClient::frameReceived);

    chatbox::Frame push;
    push.header.cmd = chatbox::kCmdChatPush;
    push.header.seq = 0;
    transport.deliver(push);

    QCOMPARE(spy.count(), 1);
    const QByteArray raw = spy.takeFirst().at(0).toByteArray();
    chatbox::FrameHeader header;
    QVERIFY(chatbox::decodeHeader(raw.constData(),
                                  static_cast<std::size_t>(raw.size()), header));
    QCOMPARE(header.cmd, static_cast<uint16_t>(chatbox::kCmdChatPush));
}

QTEST_MAIN(TestSessionClient)
#include "test_session_client.moc"
