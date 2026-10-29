#include "test_ipc.h"

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUuid>

#include "ipc/ipc_client.h"
#include "ipc/ipc_frame.h"
#include "ipc/ipc_protocol.h"
#include "ipc/ipc_server.h"

using chatbox::clientcore::IpcClient;
using chatbox::clientcore::IpcServer;

namespace {

QString uniqueServerName() {
    return QStringLiteral("clientcore-test-%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

}  // namespace

void TestIpc::frameRoundTrip() {
    QJsonObject object;
    object.insert(QStringLiteral("id"), 7);
    object.insert(QStringLiteral("cmd"), QStringLiteral("ping"));
    QJsonObject args;
    args.insert(QStringLiteral("value"), QStringLiteral("hello"));
    object.insert(QStringLiteral("args"), args);

    QByteArray buffer = chatbox::clientcore::encodeIpcFrame(object);
    QVERIFY(!buffer.isEmpty());
    QJsonObject decoded;
    QString error;
    QVERIFY(chatbox::clientcore::decodeIpcFrame(buffer, decoded, &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(decoded, object);
    QVERIFY(buffer.isEmpty());
}

void TestIpc::partialFrameWaits() {
    QJsonObject object;
    object.insert(QStringLiteral("event"), QStringLiteral("state"));
    const QByteArray frame = chatbox::clientcore::encodeIpcFrame(object);
    QVERIFY(frame.size() > 4);

    QByteArray buffer = frame.left(2);
    QJsonObject decoded;
    QString error;
    QVERIFY(!chatbox::clientcore::decodeIpcFrame(buffer, decoded, &error));
    QVERIFY(error.isEmpty());

    buffer.append(frame.mid(2));
    QVERIFY(chatbox::clientcore::decodeIpcFrame(buffer, decoded, &error));
    QVERIFY(error.isEmpty());
    QCOMPARE(decoded.value(QStringLiteral("event")).toString(),
             QStringLiteral("state"));
}

void TestIpc::oversizeFrameRejected() {
    const quint32 too_large =
        static_cast<quint32>(chatbox::clientcore::kMaxIpcFrameBytes) + 1;
    QByteArray buffer(4, '\0');
    buffer[0] = static_cast<char>((too_large >> 24) & 0xFF);
    buffer[1] = static_cast<char>((too_large >> 16) & 0xFF);
    buffer[2] = static_cast<char>((too_large >> 8) & 0xFF);
    buffer[3] = static_cast<char>(too_large & 0xFF);

    QJsonObject decoded;
    QString error;
    QVERIFY(!chatbox::clientcore::decodeIpcFrame(buffer, decoded, &error));
    QVERIFY(!error.isEmpty());
}

void TestIpc::decodeRequestValidation() {
    using chatbox::clientcore::decodeIpcRequest;

    QString cmd;
    QJsonObject args;
    int code = 0;
    QString error;

    QVERIFY(!decodeIpcRequest(QJsonObject{}, cmd, args, code, error));
    QCOMPARE(code, 400);
    QVERIFY(!error.isEmpty());

    QJsonObject bad_args;
    bad_args.insert(QStringLiteral("cmd"), QStringLiteral("ping"));
    bad_args.insert(QStringLiteral("args"), QStringLiteral("not-an-object"));
    QVERIFY(!decodeIpcRequest(bad_args, cmd, args, code, error));
    QCOMPARE(code, 400);

    QJsonObject valid;
    valid.insert(QStringLiteral("cmd"), QStringLiteral("snapshot"));
    QJsonObject payload;
    payload.insert(QStringLiteral("limit"), 10);
    valid.insert(QStringLiteral("args"), payload);
    QVERIFY(decodeIpcRequest(valid, cmd, args, code, error));
    QCOMPARE(cmd, QStringLiteral("snapshot"));
    QCOMPARE(args.value(QStringLiteral("limit")).toInt(), 10);
    QCOMPARE(code, 0);
    QVERIFY(error.isEmpty());
}

void TestIpc::requestResponseOverLocalSocket() {
    const QString name = uniqueServerName();
    IpcServer server;
    QString error;
    QVERIFY2(server.listen(name, &error), qPrintable(error));
    server.setRequestHandler([](quint64 id, const QJsonObject& request) {
        QString cmd;
        QJsonObject args;
        int code = 0;
        QString error;
        if (!chatbox::clientcore::decodeIpcRequest(request, cmd, args, code,
                                                   error)) {
            return chatbox::clientcore::ipcError(id, code, error);
        }
        if (cmd == QStringLiteral("echo")) {
            QJsonObject data;
            data.insert(QStringLiteral("value"),
                        args.value(QStringLiteral("value")));
            return chatbox::clientcore::ipcOk(id, data);
        }
        return chatbox::clientcore::ipcError(id, 404,
                                             QStringLiteral("unknown cmd"));
    });

    IpcClient client;
    QSignalSpy responses(&client, &IpcClient::responseReceived);
    client.connectToService(name);
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    QJsonObject args;
    args.insert(QStringLiteral("value"), QStringLiteral("hello"));
    const quint64 id = client.request(QStringLiteral("echo"), args);
    QVERIFY(id != 0);

    QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 3000);
    const QJsonObject response = responses.at(0).at(1).toJsonObject();
    QCOMPARE(static_cast<quint64>(responses.at(0).at(0).toULongLong()), id);
    QVERIFY(response.value(QStringLiteral("ok")).toBool());
    QCOMPARE(response.value(QStringLiteral("data"))
                 .toObject()
                 .value(QStringLiteral("value"))
                 .toString(),
             QStringLiteral("hello"));

    const quint64 bad_id = client.request(QStringLiteral("missing"), QJsonObject());
    QVERIFY(bad_id != 0);
    QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 2, 3000);
    const QJsonObject bad = responses.at(1).at(1).toJsonObject();
    QVERIFY(!bad.value(QStringLiteral("ok")).toBool());
    QCOMPARE(bad.value(QStringLiteral("error"))
                 .toObject()
                 .value(QStringLiteral("code"))
                 .toInt(),
             404);
}

void TestIpc::eventBroadcast() {
    const QString name = uniqueServerName();
    IpcServer server;
    QString error;
    QVERIFY2(server.listen(name, &error), qPrintable(error));
    server.setRequestHandler([](quint64 id, const QJsonObject&) {
        return chatbox::clientcore::ipcOk(id, QJsonObject());
    });

    IpcClient client;
    QSignalSpy responses(&client, &IpcClient::responseReceived);
    QSignalSpy events(&client, &IpcClient::eventReceived);
    client.connectToService(name);
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    // A round-trip first so the server has the accepted socket in its buffer
    // map before broadcasting.
    client.request(QStringLiteral("snapshot"), QJsonObject());
    QTRY_COMPARE_WITH_TIMEOUT(responses.count(), 1, 3000);

    QJsonObject data;
    data.insert(QStringLiteral("state"), QStringLiteral("online"));
    server.sendEvent(chatbox::clientcore::ipcEvent(QStringLiteral("state"), data));

    QTRY_COMPARE_WITH_TIMEOUT(events.count(), 1, 3000);
    QCOMPARE(events.at(0).at(0).toString(), QStringLiteral("state"));
    QCOMPARE(events.at(0).at(1)
                 .toJsonObject()
                 .value(QStringLiteral("state"))
                 .toString(),
             QStringLiteral("online"));
}
