#ifndef CHATBOX_CLIENTCORE_TESTS_TEST_SECURE_STORE_H
#define CHATBOX_CLIENTCORE_TESTS_TEST_SECURE_STORE_H

#include <QObject>

class TestSecureStore : public QObject {
    Q_OBJECT

private slots:
    void memoryStoreRoundTrip();
};

#endif  // CHATBOX_CLIENTCORE_TESTS_TEST_SECURE_STORE_H
