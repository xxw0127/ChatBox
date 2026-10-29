#include "test_secure_store.h"

#include <QtTest>

#include "secure/secure_store.h"

void TestSecureStore::memoryStoreRoundTrip() {
    chatbox::clientcore::MemorySecureStore store;
    QVERIFY(store.loadToken().isEmpty());
    QVERIFY(store.loadDeviceId().isEmpty());

    store.saveToken(QStringLiteral("token-1"));
    store.saveDeviceId(QStringLiteral("device-1"));
    QCOMPARE(store.loadToken(), QStringLiteral("token-1"));
    QCOMPARE(store.loadDeviceId(), QStringLiteral("device-1"));

    store.clearToken();
    QVERIFY(store.loadToken().isEmpty());
    QCOMPARE(store.loadDeviceId(), QStringLiteral("device-1"));
}
