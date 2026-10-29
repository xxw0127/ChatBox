#ifndef CHATBOX_CLIENTCORE_TESTS_TEST_IPC_H
#define CHATBOX_CLIENTCORE_TESTS_TEST_IPC_H

#include <QObject>

class TestIpc : public QObject {
    Q_OBJECT

private slots:
    void frameRoundTrip();
    void partialFrameWaits();
    void oversizeFrameRejected();
    void decodeRequestValidation();
    void requestResponseOverLocalSocket();
    void eventBroadcast();
};

#endif  // CHATBOX_CLIENTCORE_TESTS_TEST_IPC_H
