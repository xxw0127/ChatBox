#ifndef CHATBOX_CLIENTCORE_TESTS_TEST_MOBILE_STORE_H
#define CHATBOX_CLIENTCORE_TESTS_TEST_MOBILE_STORE_H

#include <QObject>

class TestMobileStore : public QObject {
    Q_OBJECT

private slots:
    void schemaCreated();
    void conversationUpsert();
    void messageDedupeByMessageId();
    void recentMessagesAscending();
    void outboxLifecycle();
    void metaRoundTrip();
    void recalledMessageContentCleared();
};

#endif  // CHATBOX_CLIENTCORE_TESTS_TEST_MOBILE_STORE_H
