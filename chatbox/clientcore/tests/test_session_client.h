#ifndef CHATBOX_CLIENTCORE_TESTS_TEST_SESSION_CLIENT_H
#define CHATBOX_CLIENTCORE_TESTS_TEST_SESSION_CLIENT_H

#include <QObject>

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

#endif  // CHATBOX_CLIENTCORE_TESTS_TEST_SESSION_CLIENT_H
