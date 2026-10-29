#include <QCoreApplication>
#include <QtTest>

#include "test_mobile_store.h"
#include "test_session_client.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int status = 0;
    TestSessionClient session_client;
    status |= QTest::qExec(&session_client, argc, argv);
    TestMobileStore mobile_store;
    status |= QTest::qExec(&mobile_store, argc, argv);
    return status;
}
