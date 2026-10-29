#ifndef CHATBOX_CLIENTCORE_TESTS_TEST_CHAT_SERVICE_H
#define CHATBOX_CLIENTCORE_TESTS_TEST_CHAT_SERVICE_H

#include <QObject>

class TestChatService : public QObject {
    Q_OBJECT

private slots:
    void loginStoresProfileAndRequestsLists();
    void loginSavesTokenAndResumeUsesIt();
    void terminalErrorClearsToken();
    void incomingPrivatePushPersistsAndEmits();
    void sendTextCreatesOutboxAndFrame();
    void sendAckMarksOutboxSent();
    void readStatePushUpdatesUnread();
    void syncPullAppliesAfterCursor();
    void syncErrorFallsBackToOnline();
};

#endif  // CHATBOX_CLIENTCORE_TESTS_TEST_CHAT_SERVICE_H
