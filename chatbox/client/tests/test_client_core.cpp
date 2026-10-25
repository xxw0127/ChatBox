#include <QtTest>
#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QToolButton>
#include <QVector>

#include "core/app_settings.h"
#include "core/autostart.h"
#include "core/client_policy.h"
#include "core/account_store.h"
#include "core/credential_store.h"
#include "core/focus_ring.h"
#include "core/avatar_cache.h"
#include "core/avatar_utils.h"
#include "core/contact_store.h"
#include "core/history_store.h"
#include "core/identity.h"
#include "core/message_renderer.h"
#include "core/message_store.h"
#include "core/message_time.h"
#include "core/outbox.h"
#include "core/ui_theme.h"
#include "core/user_profile.h"
#include "ui/chat_view.h"
#include "ui/empty_state.h"
#include "ui/login_page.h"
#include "ui/message_input.h"
#include "ui/message_action_bar.h"
#include "ui/profile_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/account_switcher_dialog.h"

namespace {

QString readClientSource(const QString& relative) {
    QString path = QCoreApplication::applicationDirPath() +
                   QStringLiteral("/../../") + relative;
    if (!QFile::exists(path)) {
        path = QStringLiteral("../") + relative;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

QString readMainWindowSource() {
    const QStringList files = {
        QStringLiteral("ui/main_window.cpp"),
        QStringLiteral("ui/main_window_chat.cpp"),
        QStringLiteral("ui/main_window_contacts.cpp"),
        QStringLiteral("ui/main_window_profile.cpp"),
        QStringLiteral("ui/main_window_tray.cpp"),
        QStringLiteral("ui/main_window_net.cpp"),
        QStringLiteral("ui/main_window_support.h"),
    };
    QString source;
    for (const QString& file : files) {
        source += readClientSource(file);
        source += QLatin1Char('\n');
    }
    return source;
}

QString sourceSection(const QString& source, const QString& marker,
                      const QString& next_marker) {
    const int start = source.indexOf(marker);
    if (start < 0) {
        return QString();
    }
    const int end = source.indexOf(next_marker, start + marker.size());
    return end < 0 ? source.mid(start) : source.mid(start, end - start);
}

QString caseSection(const QString& source, const QString& label) {
    return sourceSection(source, label, QStringLiteral("\n        case chatbox::"));
}

QString functionSection(const QString& source, const QString& signature) {
    return sourceSection(source, signature, QStringLiteral("\nvoid MainWindow::"));
}

// 构造一条服务端已确认的消息（message_id > 0）。
chatbox::client::ChatMessage confirmedMessage(quint64 id, quint64 target,
                                              const QString& content,
                                              const QString& utc = QStringLiteral("2026-10-20 10:00:00")) {
    chatbox::client::ChatMessage message;
    message.message_id = id;
    message.context = chatbox::client::ChatContext::kRoom;
    message.target_id = target;
    message.sender_id = 9001;
    message.sender_name = QStringLiteral("alice");
    message.content = content;
    message.created_at_utc = chatbox::client::parseServerUtc(utc);
    message.status = chatbox::client::MessageStatus::kSent;
    return message;
}

}  // namespace

class TestClientCore : public QObject {
    Q_OBJECT

private slots:
    void parseServerUtc();
    void formatLocalTimeAndDate();
    void invalidTimestamp();
    void publicUserIdFormat();
    void messageStorePendingAndAck();
    void messageStoreHistoryPreservesPending();
    void messageStorePrependHistory();
    void messageStoreReconcilesLapsedAck();
    void messageStoreMarkRecalled();
    void messageStoreMergesLocalAndServerPagesInOrder();
    void messageStoreWritesThroughWhenHistoryAttached();
    void outboxExpiresAndMarksFailed();
    void outboxMarksSentAndAllFailed();
    void rendererEscapesHtml();
    void rendererShowsRetryOnlyWhenFailed();
    void rendererAddsDateSeparator();
    void rendererBubbleSpecUsesBorderAndExternalTimestamp();
    void rendererSuppressesRepeatedSenderName();
    void rendererQuoteBlockUsesQuoteSurface();
    void rendererRecalledBubbleHasNoSenderName();
    void rendererFragmentMatchesFullRender();
    void rendererAlignsOwnMessages();
    void rendererQuoteBlockWithoutInlineActions();
    void rendererCapsWideBubbleWidth();
    void rendererQuoteBubbleStaysNarrow();
    void messageBubblesHaveRoundedCorners();
    void messageBubblesDrawRightEdge();
    void messageActionsAvailability();
    void messageActionsRecallWindow();
    void chatViewMapsMessagesToIds();
    void chatViewAppendsFragmentsWithoutResettingDocument();
    void chatViewAppendStaysUnderBudget();
    void chatViewOpensMessageMenuWithKeyboard();
    void chatViewStylesAppendedRetryLink();
    void chatViewStylesRetryLink();
    void chatViewHoverReentersSameMessage();
    void contactStorePersistsAndUpdates();
    void avatarCenterCrop();
    void avatarCompressionLimit();
    void avatarFallback();
    void avatarCacheVersion();
    void avatarCacheIgnoresOlderVersions();
    void profileDisplayNameFallsBackToNickname();
    void profileDialogSelfMode();
    void profileDialogViewModeActions();
    void messageBubbleColoursComeFromTheme();
    void contrastTokensMeetAa();
    void darkSurfaceContrastMeetsAa();
    void primaryButtonContrastMeetsAa();
    void bubbleBoundaryMeetsNonTextContrast();
    void darkPaletteMatchesSpec();
    void themeTokensMatchContrastSpec();
    void styleSheetHasVisibleKeyboardFocus();
    void keyboardFocusReasonIsKeyboardOnly();
    void focusRingFilterHonoursKeyboardAndMouse();
    void mainWindowExposesKeyboardShortcuts();
    void mainWindowAccessibilityLabels();
    void friendItemsExposeAccessibleText();
    void mainWindowOmitsJoinRoomStatusNoise();
    void friendListAvoidsDuplicateGroupLabels();
    void themeTokensMatchStyleSheet();
    void lineEditsAvoidVerticalClipping();
    void messageActionBarHasSingleSoftShadow();
    void messageActionBarIsKeyboardReachable();
    void brandAppIconExposesSizes();
    void roomAvatarIsBrandBadge();
    void trayIconUsesBrandIcon();
    void memberListShowsAvatarFromCache();
    void fallbackAvatarPaletteIsSingleSource();
    void appTitleUsesBrandName();
    void settingsMigrationCopiesLegacyStore();
    void settingsMigrationMovesOldServerEndpoint();
    void settingsMigrationNeverOverwritesCurrent();
    void brandIdentifiersAreUnified();
    void installerTemplateTargetsChatboxExe();
    void privateChatHasNoMentionEntry();
    void privatePushSkipsMentionNotification();
    void headerUsesAvatarAndMoreMenu();
    void systemPopupNotificationIsOptional();
    void autostartCommandCarriesMinimizedFlag();
    void autostartSettingWritesAndRemovesRunValue();
    void startupHonoursMinimizedAndSingleInstance();
    void startupSupportsIsolatedProfile();
    void startupPolicyRequiresUsableTray();
    void unreadBadgesHonourAlertsSwitch();
    void historyStoreCreatesSchema();
    void historyStoreReportsOpenFailure();
    void historyStoreRejectsSecondInstance();
    void historyStoreSaveAndLoadRecent();
    void historyStoreDedupesByMessageId();
    void historyStoreSkipsUnconfirmedMessages();
    void historyStoreMarksRecalled();
    void historyStoreRecallIsConversationScoped();
    void historyStoreIsolatesUsers();
    void historyStoreIsolatesConversations();
    void historyStoreSearchFindsInConversation();
    void historyStoreConversationSummaries();
    void historyStoreClearAllAndOlderThan();
    void historyStorePathMoveCopiesData();
    void historyStoreRejectsExistingTarget();
    void buildScriptShipsSqlDriver();
    void conversationOpensFromLocalHistoryFirst();
    void incomingPushesArePersistedForOtherConversations();
    void recallPushUpdatesBackgroundHistory();
    void loginPageHasNoLocalHistoryEntry();
    void loginPageHidesServerFieldsBehindMoreDots();
    void layoutWeightsFollowAudit();
    void adaptiveLayoutUsesSplitter();
    void textNeverSmallerThanElevenPixels();
    void emptyStateShowsConfiguredCopy();
    void emptyStatesAreWired();
    void messageInputHandlesKeyboardAndPlainPaste();
    void settingsDialogOffersSignOut();
    void accountSwitcherBuildsList();
    void messageInputHidesEmptyScrollbar();
    void messageComposerUsesByteCounter();
    void dangerousActionsUseChineseDialogs();
    void loginPageShowsNicknameOnlyInRegisterMode();
    void loginPageHasAutoLoginAndRememberPasswordOptions();
    void loginPageAccessibleNames();
    void loginPageClearsFieldFocusOnBlankClick();
    void loginPageReloadsRememberedPasswordAfterLogout();
    void credentialStoreEncryptsSavedPassword();
    void credentialStoreForgetsAccountSecret();
    void mainWindowUsesIncrementalAppendForIncomingMessages();
    void settingsDialogDefaultsToPublicServer();
    void offlineModeIsReadOnlyAndLocal();
    void settingsDialogShowsHistoryInfo();
    void settingsDialogExposesHistoryActions();
};

void TestClientCore::parseServerUtc() {
    const QDateTime parsed = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.timeSpec(), Qt::UTC);
    QCOMPARE(parsed.date(), QDate(2026, 10, 18));
    QCOMPARE(parsed.time(), QTime(12, 0, 0));
}

void TestClientCore::formatLocalTimeAndDate() {
    const QDateTime local(QDate(2026, 10, 18), QTime(10, 30, 0), Qt::LocalTime);
    const QDateTime utc = local.toUTC();
    QCOMPARE(chatbox::client::formatLocalTime(utc), QString("10:30"));
    QCOMPARE(chatbox::client::formatLocalDate(utc), QString("2026-10-18"));
}

void TestClientCore::invalidTimestamp() {
    const QDateTime parsed = chatbox::client::parseServerUtc("not-a-time");
    QVERIFY(!parsed.isValid());
    QCOMPARE(chatbox::client::formatLocalTime(parsed), QString("--:--"));
    QCOMPARE(chatbox::client::formatLocalDate(parsed), QString("--"));
}

void TestClientCore::publicUserIdFormat() {
    QCOMPARE(chatbox::client::publicUserId(278), QString("U00000278"));
    QCOMPARE(chatbox::client::publicUserId(123456789), QString("U123456789"));
    QVERIFY(chatbox::client::publicUserId(278) !=
            chatbox::client::publicUserId(279));
}

void TestClientCore::messageStorePendingAndAck() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kPrivate, 7);

    chatbox::client::ChatMessage pending;
    pending.client_msg_id = "p1";
    pending.content = "hello";
    pending.status = chatbox::client::MessageStatus::kSending;
    store.addPending(pending);

    QCOMPARE(store.messages().size(), 1);
    QVERIFY(store.findByClientId("p1") != nullptr);
    QCOMPARE(store.findByClientId("p1")->content, QString("hello"));
    QVERIFY(store.findByClientId("missing") == nullptr);
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    QVERIFY(store.markSent("p1", 42, created));
    QCOMPARE(store.messages().size(), 1);
    QCOMPARE(store.messages().at(0).message_id, quint64(42));
    QCOMPARE(store.messages().at(0).status, chatbox::client::MessageStatus::kSent);

    QVERIFY(store.markSent("p1", 42, created));
    QCOMPARE(store.messages().size(), 1);

    QVERIFY(store.markSending("p1"));
    QCOMPARE(store.messages().at(0).status, chatbox::client::MessageStatus::kSending);

    store.addPending(pending);
    QCOMPARE(store.messages().size(), 1);
}

void TestClientCore::messageStoreHistoryPreservesPending() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kRoom, 1);

    chatbox::client::ChatMessage pending;
    pending.client_msg_id = "p1";
    pending.status = chatbox::client::MessageStatus::kSending;
    pending.content = "pending";
    store.addPending(pending);

    chatbox::client::ChatMessage history;
    history.message_id = 1;
    history.status = chatbox::client::MessageStatus::kSent;
    history.content = "history";
    store.replaceHistory({history});

    QCOMPARE(store.messages().size(), 2);
    QCOMPARE(store.messages().at(0).message_id, quint64(1));
    QCOMPARE(store.messages().at(1).client_msg_id, QString("p1"));
    QCOMPARE(store.messages().at(1).status, chatbox::client::MessageStatus::kSending);
}

void TestClientCore::messageStorePrependHistory() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kRoom, 1);

    chatbox::client::ChatMessage current;
    current.message_id = 10;
    current.status = chatbox::client::MessageStatus::kSent;
    store.appendIncoming(current);

    chatbox::client::ChatMessage older8;
    older8.message_id = 8;
    older8.status = chatbox::client::MessageStatus::kSent;
    chatbox::client::ChatMessage older9;
    older9.message_id = 9;
    older9.status = chatbox::client::MessageStatus::kSent;
    store.prependHistory({older8, older9});
    QCOMPARE(store.messages().size(), 3);
    QCOMPARE(store.messages().at(0).message_id, quint64(8));
    QCOMPARE(store.messages().at(1).message_id, quint64(9));
    QCOMPARE(store.messages().at(2).message_id, quint64(10));

    store.prependHistory({older8, older9});
    QCOMPARE(store.messages().size(), 3);
}

void TestClientCore::messageStoreReconcilesLapsedAck() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kPrivate, 7);

    const QDateTime sent_at = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage pending;
    pending.client_msg_id = "p1";
    pending.context = chatbox::client::ChatContext::kPrivate;
    pending.target_id = 7;
    pending.sender_id = 1;
    pending.content = "hello";
    pending.created_at_utc = sent_at;
    pending.outgoing = true;
    pending.status = chatbox::client::MessageStatus::kFailed;
    store.addPending(pending);

    chatbox::client::ChatMessage history;
    history.message_id = 42;
    history.context = chatbox::client::ChatContext::kPrivate;
    history.target_id = 7;
    history.sender_id = 1;
    history.content = "hello";
    history.created_at_utc = sent_at.addSecs(1);
    history.outgoing = true;
    history.status = chatbox::client::MessageStatus::kSent;
    store.replaceHistory({history});

    QCOMPARE(store.messages().size(), 1);
    QCOMPARE(store.messages().at(0).message_id, quint64(42));
    QCOMPARE(store.messages().at(0).client_msg_id, QString("p1"));

    QVERIFY(store.markSent("p1", 42, sent_at.addSecs(1)));
    QCOMPARE(store.messages().size(), 1);
}

void TestClientCore::messageStoreMarkRecalled() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kRoom, 1);
    chatbox::client::ChatMessage message;
    message.message_id = 99;
    message.content = "to recall";
    message.status = chatbox::client::MessageStatus::kSent;
    store.appendIncoming(message);

    QVERIFY(store.findMessage(99) != nullptr);
    QVERIFY(store.markRecalled(99));
    QCOMPARE(store.messages().at(0).recalled, true);
    QVERIFY(store.messages().at(0).content.isEmpty());
    QVERIFY(!store.markRecalled(100));
}

// 本地首屏与服务端返回的那一页必须合并成一条按时间递增的序列：
// 服务端页里有更新的消息时，本地更旧的行不能被留在最底下。
void TestClientCore::messageStoreMergesLocalAndServerPagesInOrder() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kRoom, 7);
    QVector<chatbox::client::ChatMessage> local;
    for (quint64 id = 1; id <= 10; ++id) {
        local.push_back(confirmedMessage(id, 7, "local"));
    }
    store.replaceHistory(local);
    QVector<chatbox::client::ChatMessage> server;
    for (quint64 id = 6; id <= 12; ++id) {
        server.push_back(confirmedMessage(id, 7, "server"));
    }
    store.replaceHistory(server);
    const auto& merged = store.messages();
    QCOMPARE(merged.size(), 12);
    for (int i = 0; i < merged.size(); ++i) {
        QCOMPARE(merged.at(i).message_id, quint64(i + 1));
    }
    chatbox::client::ChatMessage pending;
    pending.client_msg_id = QStringLiteral("pending-order");
    pending.status = chatbox::client::MessageStatus::kSending;
    store.addPending(pending);
    QCOMPARE(store.messages().last().message_id, quint64(0));
}

void TestClientCore::messageStoreWritesThroughWhenHistoryAttached() {
    QTemporaryDir dir; chatbox::client::HistoryStore history; QString error;
    QVERIFY(history.open(dir.path(), 1401, &error));

    chatbox::client::MessageStore store;
    store.setHistoryStore(&history);
    // 撤回是按会话定位的：store 必须先知道自己管的是哪个会话。
    store.reset(chatbox::client::ChatContext::kRoom, 7);
    QVector<chatbox::client::ChatMessage> page;
    page << confirmedMessage(1, 7, "first") << confirmedMessage(2, 7, "second");
    store.replaceHistory(page);
    store.appendIncoming(confirmedMessage(3, 7, "push"));
    QCOMPARE(history.messageCount(), qint64(3));

    chatbox::client::ChatMessage pending = confirmedMessage(0, 7, "local");
    pending.client_msg_id = QStringLiteral("client-1");
    pending.status = chatbox::client::MessageStatus::kSending;
    store.addPending(pending);
    store.markFailed(pending.client_msg_id);
    QCOMPARE(history.messageCount(), qint64(3));  // 未确认消息不落盘

    chatbox::client::ChatMessage outgoing = confirmedMessage(0, 7, "sent");
    outgoing.client_msg_id = QStringLiteral("client-2");
    outgoing.status = chatbox::client::MessageStatus::kSending;
    store.addPending(outgoing);
    QVERIFY(store.markSent(outgoing.client_msg_id, 4, QDateTime::currentDateTimeUtc()));
    QCOMPARE(history.messageCount(), qint64(4));

    QVERIFY(store.markRecalled(4));
    const auto rows = history.recent(chatbox::client::ChatContext::kRoom, 7, 10);
    QVERIFY(rows.last().recalled);
    QVERIFY(rows.last().content.isEmpty());

    store.setHistoryStore(nullptr);
    store.appendIncoming(confirmedMessage(5, 7, "detached"));
    QCOMPARE(history.messageCount(), qint64(4));
}

void TestClientCore::outboxExpiresAndMarksFailed() {
    chatbox::client::Outbox outbox;
    const QDateTime t0 = chatbox::client::parseServerUtc("2026-10-18 12:00:00");

    chatbox::client::ChatMessage message;
    message.client_msg_id = "p1";
    message.status = chatbox::client::MessageStatus::kSending;
    outbox.add(message, t0);
    QVERIFY(outbox.contains("p1"));

    QVERIFY(outbox.expireSending(t0.addSecs(5), 10).isEmpty());
    const QVector<QString> expired = outbox.expireSending(t0.addSecs(11), 10);
    QCOMPARE(expired.size(), 1);
    QCOMPARE(expired.at(0), QString("p1"));
    QCOMPARE(outbox.pending().at(0).status, chatbox::client::MessageStatus::kFailed);

    QVERIFY(outbox.markSent("p1"));
    QVERIFY(!outbox.contains("p1"));
}

void TestClientCore::outboxMarksSentAndAllFailed() {
    chatbox::client::Outbox outbox;
    const QDateTime now = chatbox::client::parseServerUtc("2026-10-18 12:00:00");

    chatbox::client::ChatMessage first;
    first.client_msg_id = "p1";
    first.status = chatbox::client::MessageStatus::kSending;
    chatbox::client::ChatMessage second;
    second.client_msg_id = "p2";
    second.status = chatbox::client::MessageStatus::kSending;
    outbox.add(first, now);
    outbox.add(second, now);

    QVERIFY(outbox.markSent("p1"));
    QVERIFY(!outbox.contains("p1"));
    QCOMPARE(outbox.markAllSendingFailed(), 1);
    QCOMPARE(outbox.pending().at(0).status, chatbox::client::MessageStatus::kFailed);
}

void TestClientCore::rendererEscapesHtml() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 1;
    message.sender_name = "<b>Alice</b>";
    message.content = "<script>alert(\"x\")</script>";
    message.created_at_utc = created;
    message.status = chatbox::client::MessageStatus::kSent;

    const QString html = chatbox::client::renderConversationHtml({message}, 99);
    QVERIFY(html.contains("&lt;script&gt;"));
    QVERIFY(html.contains("&lt;b&gt;Alice&lt;/b&gt;"));
    QVERIFY(!html.contains("<script>"));
    QVERIFY(!html.contains("<b>Alice</b>"));
}

void TestClientCore::rendererShowsRetryOnlyWhenFailed() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "p1";
    failed.content = "hello";
    failed.created_at_utc = created;
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;

    QString html = chatbox::client::renderConversationHtml({failed}, 1);
    QVERIFY(html.contains("retry:p1"));
    QVERIFY(html.contains("发送失败"));

    chatbox::client::ChatMessage sent = failed;
    sent.status = chatbox::client::MessageStatus::kSent;
    html = chatbox::client::renderConversationHtml({sent}, 1);
    QVERIFY(!html.contains("retry:p1"));
    QVERIFY(!html.contains("发送失败"));
}

void TestClientCore::rendererAddsDateSeparator() {
    const QDateTime first_local(QDate(2026, 10, 18), QTime(10, 0), Qt::LocalTime);
    const QDateTime second_local(QDate(2026, 10, 19), QTime(10, 0), Qt::LocalTime);

    chatbox::client::ChatMessage first;
    first.message_id = 1;
    first.content = "day one";
    first.created_at_utc = first_local.toUTC();
    chatbox::client::ChatMessage second;
    second.message_id = 2;
    second.content = "day two";
    second.created_at_utc = second_local.toUTC();

    const QString html = chatbox::client::renderConversationHtml({first, second}, 1);
    QVERIFY(html.contains("2026-10-18"));
    QVERIFY(html.contains("2026-10-19"));
}

void TestClientCore::rendererBubbleSpecUsesBorderAndExternalTimestamp() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 1;
    message.sender_name = "Bob";
    message.content = "bubble body";
    message.created_at_utc = created;

    const QString html = chatbox::client::renderConversationHtml({message}, 1);
    QVERIFY(html.contains(QStringLiteral("cellpadding=\"1\"")));
    QVERIFY(html.contains(QStringLiteral("cellpadding=\"12\"")));
    // The outer cell is a canvas-coloured gap; ChatView owns the visible
    // rounded border, so the HTML must not paint a square border underneath it.
    QVERIFY(html.contains(QStringLiteral("bgcolor=\"%1\"")
                              .arg(QString::fromLatin1(
                                  chatbox::client::theme::kSurfaceSoft))));
    QVERIFY(!html.contains(QStringLiteral("bgcolor=\"%1\"")
                               .arg(QString::fromLatin1(
                                   chatbox::client::theme::kBubbleBorder))));
    QVERIFY(html.contains(QStringLiteral("line-height: 150%")));
    const QString local_time = chatbox::client::formatLocalTime(created);
    const int timestamp = html.indexOf(local_time);
    const int bubble_close = html.indexOf(QStringLiteral("</table></td></tr></table>"));
    QVERIFY(timestamp > 0);
    QVERIFY(bubble_close > 0);
    QVERIFY(timestamp > bubble_close);
}

void TestClientCore::rendererSuppressesRepeatedSenderName() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage first;
    first.message_id = 1;
    first.sender_id = 2;
    first.sender_name = "RepeatOnly";
    first.content = "first";
    first.created_at_utc = created;
    chatbox::client::ChatMessage second = first;
    second.message_id = 2;
    second.content = "second";
    second.created_at_utc = created.addSecs(30);
    chatbox::client::ChatMessage third = first;
    third.message_id = 3;
    third.content = "third";
    third.created_at_utc = created.addSecs(60);

    const QString html =
        chatbox::client::renderConversationHtml({first, second, third}, 1);
    QCOMPARE(html.count(QStringLiteral("RepeatOnly")), 1);
    QVERIFY(!chatbox::client::shouldShowSenderName(&first, second));
    QVERIFY(!chatbox::client::shouldShowSenderName(&second, third));
    const QString fragment = chatbox::client::renderMessageFragment(
        third, 1, false, QString(), false);
    QVERIFY(!fragment.contains(QStringLiteral("RepeatOnly")));
}

void TestClientCore::rendererQuoteBlockUsesQuoteSurface() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage quoted;
    quoted.message_id = 7;
    quoted.sender_name = "Bob";
    quoted.content = "quoted body";
    quoted.created_at_utc = created;
    quoted.reply_to_id = 5;
    quoted.reply_sender_name = "Alice";
    quoted.reply_content = "original text";

    const QString html = chatbox::client::renderConversationHtml({quoted}, 1);
    QVERIFY(html.contains(QString::fromLatin1(chatbox::client::theme::kQuoteBg)));
    QVERIFY(!html.contains(QStringLiteral("border-left")));
    QVERIFY(html.contains(QStringLiteral("回复 Alice: original text")));
}

void TestClientCore::rendererRecalledBubbleHasNoSenderName() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage recalled;
    recalled.message_id = 8;
    recalled.sender_name = "SenderOnly";
    recalled.content = "secret";
    recalled.created_at_utc = created;
    recalled.recalled = true;

    const QString html = chatbox::client::renderConversationHtml({recalled}, 1);
    QVERIFY(html.contains(QStringLiteral("消息已撤回")));
    QVERIFY(!html.contains(QStringLiteral("SenderOnly")));
    QVERIFY(html.contains(QString::fromLatin1(chatbox::client::theme::kSurfaceStrong)));
}

void TestClientCore::rendererFragmentMatchesFullRender() {
    const QDateTime first_local(QDate(2026, 10, 18), QTime(10, 0), Qt::LocalTime);
    const QDateTime second_local(QDate(2026, 10, 19), QTime(10, 0), Qt::LocalTime);

    chatbox::client::ChatMessage first;
    first.message_id = 1;
    first.sender_name = "Bob";
    first.content = "day one";
    first.created_at_utc = first_local.toUTC();
    chatbox::client::ChatMessage second;
    second.message_id = 2;
    second.sender_name = "Bob";
    second.content = "day two";
    second.created_at_utc = second_local.toUTC();

    const QString expected =
        QStringLiteral("<html><body style=\"font-family: 'Microsoft YaHei', sans-serif;\">") +
        chatbox::client::renderMessageFragment(
            first, 1, true, chatbox::client::formatLocalDate(first.created_at_utc)) +
        chatbox::client::renderMessageFragment(
            second, 1, true, chatbox::client::formatLocalDate(second.created_at_utc)) +
        QStringLiteral("</body></html>");
    QCOMPARE(chatbox::client::renderConversationHtml({first, second}, 1), expected);
}

void TestClientCore::rendererAlignsOwnMessages() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage own;
    own.message_id = 1;
    own.content = "own";
    own.created_at_utc = created;
    own.outgoing = true;

    QString html = chatbox::client::renderConversationHtml({own}, 1);
    QVERIFY(html.contains("align=\"right\""));

    chatbox::client::ChatMessage other;
    other.message_id = 2;
    other.sender_id = 2;
    other.sender_name = "Bob";
    other.content = "other";
    other.created_at_utc = created;
    other.outgoing = false;

    html = chatbox::client::renderConversationHtml({other}, 1);
    QVERIFY(html.contains("align=\"left\""));
}

void TestClientCore::rendererQuoteBlockWithoutInlineActions() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage quoted;
    quoted.message_id = 7;
    quoted.sender_name = "Bob";
    quoted.content = "quoted body";
    quoted.created_at_utc = created;
    quoted.outgoing = false;
    quoted.reply_to_id = 5;
    quoted.reply_sender_name = "Alice";
    quoted.reply_content = "original text";

    QString html = chatbox::client::renderConversationHtml({quoted}, 1);
    QVERIFY(html.contains("回复 Alice: original text"));
    QVERIFY(!html.contains("quote:7"));

    chatbox::client::ChatMessage recalled = quoted;
    recalled.message_id = 8;
    recalled.outgoing = true;
    recalled.content = "secret";
    recalled.recalled = true;
    html = chatbox::client::renderConversationHtml({recalled}, 1);
    QVERIFY(html.contains("消息已撤回"));
    QVERIFY(!html.contains("secret"));
    QVERIFY(!html.contains("recall:8"));

    chatbox::client::ChatMessage own;
    own.message_id = 9;
    own.content = "own";
    own.created_at_utc = created;
    own.outgoing = true;
    html = chatbox::client::renderConversationHtml({own}, 1);
    QVERIFY(!html.contains("recall:9"));
}

void TestClientCore::rendererCapsWideBubbleWidth() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 41;
    message.sender_name = "Bob";
    message.content = QString(300, QChar('x'));
    message.created_at_utc = created;

    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({message}, 1, &keys);
    ChatView view;
    view.resize(560, 600);
    view.show();
    QTest::qWait(30);
    view.setConversationHtml(html, keys);

    const QRect bubble = view.messageBubbleRect(41);
    QVERIFY(!bubble.isNull());
    const int limit = static_cast<int>(view.viewport()->width() * 0.8);
    QVERIFY2(bubble.width() <= limit,
             qPrintable(QStringLiteral("bubble %1 > limit %2")
                            .arg(bubble.width())
                            .arg(limit)));
}

void TestClientCore::rendererQuoteBubbleStaysNarrow() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage quoted;
    quoted.message_id = 42;
    quoted.sender_name = "Bob";
    quoted.content = "reply";
    quoted.created_at_utc = created;
    quoted.reply_to_id = 5;
    quoted.reply_sender_name = "Alice";
    quoted.reply_content =
        QStringLiteral("room original 32-Chinese-character "
                       "859d8d66-5453-4675-aa97-3d418f31f17e");

    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({quoted}, 1, &keys);
    ChatView view;
    view.resize(560, 600);
    view.show();
    QTest::qWait(30);
    view.setConversationHtml(html, keys);

    const QRect bubble = view.messageBubbleRect(42);
    QVERIFY(!bubble.isNull());
    const int limit = static_cast<int>(view.viewport()->width() * 0.8);
    QVERIFY2(bubble.width() <= limit,
             qPrintable(QStringLiteral("quote bubble %1 > limit %2")
                            .arg(bubble.width())
                            .arg(limit)));
}

void TestClientCore::messageBubblesHaveRoundedCorners() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 43;
    message.sender_id = 9;
    message.sender_name = "Bob";
    message.content = "rounded corner";
    message.created_at_utc = created;

    QFile style(QStringLiteral(":/style.qss"));
    QVERIFY(style.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString previous_style = qApp->styleSheet();
    qApp->setStyleSheet(QString::fromUtf8(style.readAll()));

    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({message}, 1, &keys);
    ChatView view;
    view.resize(560, 400);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);
    QTest::qWait(50);
    const QRect bubble = view.messageOuterBubbleRect(43);
    QVERIFY(!bubble.isNull());
    QWidget* overlay =
        view.findChild<QWidget*>(QStringLiteral("BubbleOverlay"));
    QVERIFY(overlay != nullptr);
    const QImage image = overlay->grab().toImage();
    const QColor canvas(chatbox::client::theme::kSurfaceSoft);
    const QColor border(chatbox::client::theme::kBubbleBorder);
    const QColor corner = image.pixelColor(bubble.left() + 2, bubble.top() + 2);
    bool border_on_top_edge = false;
    for (int y = bubble.top(); y <= bubble.top() + 2; ++y) {
        if (image.pixelColor(bubble.center().x(), y) == border) {
            border_on_top_edge = true;
        }
    }
    qApp->setStyleSheet(previous_style);

    QVERIFY2(qAbs(corner.red() - canvas.red()) <= 4 &&
                 qAbs(corner.green() - canvas.green()) <= 4 &&
                 qAbs(corner.blue() - canvas.blue()) <= 4,
             qPrintable(QStringLiteral("corner colour %1,%2,%3 should be canvas %4,%5,%6")
                            .arg(corner.red())
                            .arg(corner.green())
                            .arg(corner.blue())
                            .arg(canvas.red())
                            .arg(canvas.green())
                            .arg(canvas.blue())));
    QVERIFY2(border_on_top_edge,
             "the overlay must draw the rounded bubble border, not just mask corners");
}

void TestClientCore::messageBubblesDrawRightEdge() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 44;
    message.sender_id = 1;
    message.content = "?";
    message.created_at_utc = created;
    message.outgoing = true;

    QFile style(QStringLiteral(":/style.qss"));
    QVERIFY(style.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString previous_style = qApp->styleSheet();
    qApp->setStyleSheet(QString::fromUtf8(style.readAll()));

    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({message}, 1, &keys);
    ChatView view;
    view.resize(560, 400);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);
    QTest::qWait(50);

    const QRect bubble = view.messageOuterBubbleRect(44);
    QVERIFY(!bubble.isNull());
    QWidget* overlay =
        view.findChild<QWidget*>(QStringLiteral("BubbleOverlay"));
    QVERIFY(overlay != nullptr);
    const QImage image = overlay->grab().toImage();
    const QColor border(chatbox::client::theme::kBubbleBorder);
    bool found = false;
    for (int x = bubble.right() - 2; x <= bubble.right() + 2; ++x) {
        for (int y = bubble.top() + 2; y <= bubble.bottom() - 2; ++y) {
            if (x >= 0 && x < image.width() && y >= 0 && y < image.height() &&
                image.pixelColor(x, y) == border) {
                found = true;
            }
        }
    }
    const QColor edge = image.pixelColor(image.width() - 1, bubble.center().y());
    qApp->setStyleSheet(previous_style);
    QVERIFY2(found, "outgoing bubbles must paint the right border");
    QVERIFY2(edge == QColor(chatbox::client::theme::kSurfaceSoft),
             "the clipped table sliver at the viewport edge must be canvas");
}

void TestClientCore::messageActionsAvailability() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    chatbox::client::ChatMessage incoming;
    incoming.message_id = 5;
    incoming.sender_id = 2;
    chatbox::client::MessageActions actions =
        chatbox::client::messageActions(incoming, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(!actions.can_recall);
    QVERIFY(!actions.can_retry);

    chatbox::client::ChatMessage own;
    own.message_id = 6;
    own.outgoing = true;
    own.created_at_utc = now;
    actions = chatbox::client::messageActions(own, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(actions.can_recall);

    // Room history marks the user's own messages by sender id.
    chatbox::client::ChatMessage own_by_sender;
    own_by_sender.message_id = 7;
    own_by_sender.sender_id = 1;
    own_by_sender.created_at_utc = now.addSecs(-5);
    actions = chatbox::client::messageActions(own_by_sender, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(actions.can_recall);

    chatbox::client::ChatMessage recalled;
    recalled.message_id = 8;
    recalled.outgoing = true;
    recalled.recalled = true;
    recalled.created_at_utc = now;
    actions = chatbox::client::messageActions(recalled, 1, now);
    QVERIFY(!actions.can_reply);
    QVERIFY(!actions.can_recall);
    QVERIFY(!actions.can_retry);

    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "p1";
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;
    actions = chatbox::client::messageActions(failed, 1, now);
    QVERIFY(actions.can_retry);
    QVERIFY(!actions.can_reply);
    QVERIFY(!actions.can_recall);

    chatbox::client::ChatMessage sending;
    sending.client_msg_id = "p2";
    sending.status = chatbox::client::MessageStatus::kSending;
    actions = chatbox::client::messageActions(sending, 1, now);
    QVERIFY(!actions.can_retry);
    QVERIFY(!actions.can_reply);
}

void TestClientCore::messageActionsRecallWindow() {
    // The client mirrors the server's 2-minute recall window so a stale
    // message never offers 撤回 that the server would reject.
    const QDateTime now = QDateTime::currentDateTimeUtc();

    chatbox::client::ChatMessage own;
    own.message_id = 9;
    own.outgoing = true;
    own.created_at_utc = now.addSecs(-121);
    chatbox::client::MessageActions actions =
        chatbox::client::messageActions(own, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(!actions.can_recall);

    own.created_at_utc = now.addSecs(-119);
    actions = chatbox::client::messageActions(own, 1, now);
    QVERIFY(actions.can_recall);

    // Exactly at the limit the server still accepts the recall.
    own.created_at_utc = now.addSecs(-120);
    actions = chatbox::client::messageActions(own, 1, now);
    QVERIFY(actions.can_recall);

    // Other people's messages never offer 撤回, however fresh they are.
    chatbox::client::ChatMessage incoming;
    incoming.message_id = 10;
    incoming.sender_id = 2;
    incoming.created_at_utc = now;
    actions = chatbox::client::messageActions(incoming, 1, now);
    QVERIFY(actions.can_reply);
    QVERIFY(!actions.can_recall);

    // A message without a server timestamp cannot be recalled either.
    chatbox::client::ChatMessage no_time;
    no_time.message_id = 11;
    no_time.outgoing = true;
    actions = chatbox::client::messageActions(no_time, 1, now);
    QVERIFY(!actions.can_recall);

    // The default reference time is the current wall clock.
    own.created_at_utc = now.addSecs(-300);
    actions = chatbox::client::messageActions(own, 1);
    QVERIFY(actions.can_reply);
    QVERIFY(!actions.can_recall);
}

void TestClientCore::chatViewMapsMessagesToIds() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage first;
    first.message_id = 11;
    first.sender_id = 2;
    first.sender_name = "Bob";
    first.content = "first body";
    first.created_at_utc = created;
    chatbox::client::ChatMessage second;
    second.message_id = 22;
    second.sender_id = 1;
    second.sender_name = "Me";
    second.content = "second body";
    second.created_at_utc = created;
    second.outgoing = true;

    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "p9";
    failed.content = "failed body";
    failed.created_at_utc = created;
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;

    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({first, second, failed}, 1, &keys);
    QCOMPARE(keys.size(), 3);
    QCOMPARE(keys.at(0).message_id, quint64(11));
    QCOMPARE(keys.at(1).message_id, quint64(22));
    QCOMPARE(keys.at(2).message_id, quint64(0));
    QCOMPARE(keys.at(2).client_msg_id, QString("p9"));

    ChatView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);

    const QTextCursor first_cursor = view.document()->find("first body");
    QVERIFY(!first_cursor.isNull());
    QCOMPARE(view.messageIdAt(view.cursorRect(first_cursor).center()), quint64(11));

    const QTextCursor second_cursor = view.document()->find("second body");
    QVERIFY(!second_cursor.isNull());
    QCOMPARE(view.messageIdAt(view.cursorRect(second_cursor).center()), quint64(22));

    // The date separator is not part of any message.
    const QTextCursor date_cursor = view.document()->find("2026-10-18");
    QVERIFY(!date_cursor.isNull());
    QCOMPARE(view.messageIdAt(view.cursorRect(date_cursor).center()), quint64(0));

    // A failed message has no server id but keeps its client id, so the
    // context menu can still offer 重发 for it.
    const QTextCursor failed_cursor = view.document()->find("failed body");
    QVERIFY(!failed_cursor.isNull());
    const QPoint failed_pos = view.cursorRect(failed_cursor).center();
    QCOMPARE(view.messageIdAt(failed_pos), quint64(0));
    chatbox::client::MessageKey failed_key;
    QVERIFY(view.messageKeyAt(failed_pos, &failed_key));
    QCOMPARE(failed_key.client_msg_id, QString("p9"));

    chatbox::client::MessageKey none;
    QVERIFY(!view.messageKeyAt(QPoint(1, 1), &none));

    const QRect first_anchor = view.messageAnchorRect(11);
    const QRect second_anchor = view.messageAnchorRect(22);
    QVERIFY(!first_anchor.isNull());
    QVERIFY(!second_anchor.isNull());
    QVERIFY(second_anchor.top() > first_anchor.top());
    QVERIFY(view.messageAnchorRect(9999).isNull());

    // Bubble rects are in viewport coordinates and contain their content.
    const QRect first_bubble = view.messageBubbleRect(11);
    const QRect second_bubble = view.messageBubbleRect(22);
    QVERIFY(!first_bubble.isNull());
    QVERIFY(!second_bubble.isNull());
    QVERIFY(first_bubble.contains(view.cursorRect(first_cursor).center()));
    QVERIFY(second_bubble.contains(view.cursorRect(second_cursor).center()));
    QVERIFY(view.messageBubbleRect(9999).isNull());

    // After scrolling, bubble rects must follow the viewport.
    view.resize(640, 160);
    QTest::qWait(50);
    QScrollBar* scroll = view.verticalScrollBar();
    scroll->setValue(scroll->maximum());
    QTest::qWait(50);
    const QPoint second_content = view.cursorRect(second_cursor).center();
    QVERIFY(second_content.y() >= 0);
    QVERIFY(second_content.y() < view.viewport()->height());
    QVERIFY(view.messageBubbleRect(22).contains(second_content));
}

void TestClientCore::chatViewAppendsFragmentsWithoutResettingDocument() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage first;
    first.message_id = 11;
    first.sender_id = 2;
    first.sender_name = "Bob";
    first.content = "first body";
    first.created_at_utc = created;
    chatbox::client::ChatMessage second;
    second.message_id = 22;
    second.sender_id = 1;
    second.sender_name = "Me";
    second.content = "second body";
    second.created_at_utc = created;
    second.outgoing = true;

    QVector<chatbox::client::MessageKey> keys;
    const QString html = chatbox::client::renderConversationHtml({first, second}, 1, &keys);
    ChatView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);
    QTextDocument* document = view.document();
    int resets = 0;
    QObject::connect(document, &QTextDocument::contentsChange,
                     [&resets](int position, int removed, int) {
                         if (position == 0 && removed > 0) {
                             ++resets;
                         }
                     });

    for (int i = 0; i < 50; ++i) {
        chatbox::client::ChatMessage appended;
        appended.message_id = 100 + static_cast<quint64>(i);
        appended.sender_id = 2;
        appended.sender_name = "Bob";
        appended.content = QStringLiteral("append %1").arg(i);
        appended.created_at_utc = created.addSecs(i);
        chatbox::client::MessageKey key;
        key.message_id = appended.message_id;
        view.appendFragment(chatbox::client::renderMessageFragment(appended, 1, false,
                                                                    QString()),
                            key);
    }

    QCOMPARE(view.document(), document);
    QCOMPARE(resets, 0);
    QVERIFY(!document->find("first body").isNull());
    QVERIFY(!view.messageAnchorRect(11).isNull());
    QVERIFY(!view.messageAnchorRect(149).isNull());

    QScrollBar* scroll = view.verticalScrollBar();
    scroll->setValue(scroll->maximum());
    QTest::qWait(50);
    const QTextCursor last = document->find("append 49");
    QVERIFY(!last.isNull());
    const QPoint last_pos = view.cursorRect(last).center();
    QCOMPARE(view.messageIdAt(last_pos), quint64(149));
    QVERIFY(view.messageBubbleRect(149).contains(last_pos));

    view.clearConversation();
    QVERIFY(view.messageAnchorRect(11).isNull());
    QVERIFY(view.document()->find("first body").isNull());
}

void TestClientCore::chatViewAppendStaysUnderBudget() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    for (int count : {100, 500, 1000}) {
        QVector<chatbox::client::ChatMessage> messages;
        messages.reserve(count);
        for (int i = 0; i < count; ++i) {
            chatbox::client::ChatMessage message;
            message.message_id = static_cast<quint64>(i + 1);
            message.sender_id = 2;
            message.sender_name = "Bob";
            message.content = QStringLiteral("message %1").arg(i);
            message.created_at_utc = created.addSecs(i);
            messages.append(message);
        }

        QVector<chatbox::client::MessageKey> keys;
        const QString html = chatbox::client::renderConversationHtml(messages, 1, &keys);
        ChatView view;
        view.resize(640, 480);
        view.show();
        QTest::qWait(50);
        QElapsedTimer full_timer;
        full_timer.start();
        view.setConversationHtml(html, keys);
        QTest::qWait(200);
        // Force the initial document layout before measuring the incremental
        // append; otherwise the timer would include the first full layout.
        view.document()->size();
        view.verticalScrollBar()->maximum();
        const qint64 full_ms = full_timer.elapsed();

        chatbox::client::ChatMessage extra;
        extra.message_id = 2000;
        extra.sender_id = 2;
        extra.sender_name = "Bob";
        extra.content = "one more";
        extra.created_at_utc = created.addSecs(count);
        chatbox::client::MessageKey key;
        key.message_id = extra.message_id;
        const QString fragment =
            chatbox::client::renderMessageFragment(extra, 1, false, QString());

        QElapsedTimer timer;
        timer.start();
        view.appendFragment(fragment, key);
        const qint64 elapsed_us = timer.nsecsElapsed() / 1000;
        qInfo("messages=%d full=%lldms append=%.2fms", count,
              static_cast<long long>(full_ms), elapsed_us / 1000.0);
        QVERIFY(!view.document()->find("one more").isNull());
        QVERIFY(!view.messageAnchorRect(2000).isNull());
        QVERIFY2(elapsed_us < 5000,
                 qPrintable(QStringLiteral("append after %1 messages took %2 us")
                                .arg(count)
                                .arg(elapsed_us)));
    }
}

void TestClientCore::chatViewOpensMessageMenuWithKeyboard() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 77;
    message.sender_id = 2;
    message.sender_name = "Bob";
    message.content = "keyboard body";
    message.created_at_utc = created;

    QVector<chatbox::client::MessageKey> keys;
    const QString html = chatbox::client::renderConversationHtml({message}, 1, &keys);
    ChatView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);
    QCOMPARE(view.focusPolicy(), Qt::StrongFocus);

    QTextCursor cursor = view.document()->find("keyboard body");
    QVERIFY(!cursor.isNull());
    cursor.select(QTextCursor::WordUnderCursor);
    view.setTextCursor(cursor);
    view.setFocus();
    QTest::qWait(20);

    QSignalSpy spy(&view, &ChatView::messageMenuRequested);
    QTest::keyClick(&view, Qt::Key_F10);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toULongLong(), quint64(77));
}

void TestClientCore::chatViewStylesAppendedRetryLink() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage base;
    base.message_id = 1;
    base.content = "base";
    base.created_at_utc = created;

    QVector<chatbox::client::MessageKey> keys;
    const QString html = chatbox::client::renderConversationHtml({base}, 1, &keys);
    ChatView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);

    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "p2";
    failed.content = "append failed";
    failed.created_at_utc = created;
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;
    chatbox::client::MessageKey key;
    key.client_msg_id = failed.client_msg_id;
    view.appendFragment(chatbox::client::renderMessageFragment(failed, 1, false, QString()),
                        key);

    bool inspected = false;
    for (QTextBlock block = view.document()->begin(); block.isValid();
         block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.text().contains(QString::fromUtf8("重发"))) {
                continue;
            }
            const QTextCharFormat format = fragment.charFormat();
            QVERIFY(format.isAnchor());
            QVERIFY(!format.fontUnderline());
            QCOMPARE(format.foreground().color(),
                     QColor(chatbox::client::theme::kAccent));
            inspected = true;
        }
    }
    QVERIFY(inspected);
}

void TestClientCore::chatViewStylesRetryLink() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "p1";
    failed.content = "oops";
    failed.created_at_utc = created;
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;

    QVector<chatbox::client::MessageKey> keys;
    const QString html = chatbox::client::renderConversationHtml({failed}, 1, &keys);
    ChatView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);

    bool inspected = false;
    for (QTextBlock block = view.document()->begin(); block.isValid();
         block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.text().contains(QString::fromUtf8("重发"))) {
                continue;
            }
            const QTextCharFormat format = fragment.charFormat();
            QVERIFY(format.isAnchor());
            QVERIFY(!format.fontUnderline());
            QCOMPARE(format.foreground().color(),
                     QColor(chatbox::client::theme::kAccent));
            inspected = true;
        }
    }
    QVERIFY(inspected);
}

void TestClientCore::chatViewHoverReentersSameMessage() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage own;
    own.message_id = 22;
    own.sender_id = 1;
    own.sender_name = "Me";
    own.content = "hover body";
    own.created_at_utc = created;
    own.outgoing = true;

    QVector<chatbox::client::MessageKey> keys;
    const QString html = chatbox::client::renderConversationHtml({own}, 1, &keys);

    ChatView view;
    view.resize(640, 480);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);

    const QTextCursor cursor = view.document()->find("hover body");
    QVERIFY(!cursor.isNull());
    const QPoint pos = view.cursorRect(cursor).center();
    QCOMPARE(view.messageIdAt(pos), quint64(22));

    int hovered = 0;
    int cleared = 0;
    QObject::connect(&view, &ChatView::hoveredMessage,
                     [&hovered](quint64, const QRect&) { ++hovered; });
    QObject::connect(&view, &ChatView::hoverCleared, [&cleared]() { ++cleared; });

    const auto send_move = [&view](const QPoint& point) {
        QMouseEvent event(QEvent::MouseMove, QPointF(point),
                          QPointF(view.viewport()->mapToGlobal(point)),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &event);
    };

    send_move(pos);
    QCOMPARE(hovered, 1);

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(view.viewport(), &leave);
    QCOMPARE(cleared, 1);

    // Returning to the same bubble after leaving the view must show the
    // action bar again; a stale hovered id used to suppress the signal.
    send_move(pos);
    QCOMPARE(hovered, 2);
}

void TestClientCore::contactStorePersistsAndUpdates() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QCoreApplication::setOrganizationName("ChatBoxTest");
    QCoreApplication::setApplicationName("ClientCoreTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QSettings settings;
    settings.remove("contacts");
    settings.sync();

    chatbox::client::Contact first;
    first.user_id = 42;
    first.username = "alice";
    first.nickname = "Alice";
    chatbox::client::ContactStore store;
    QVERIFY(store.add(first));
    QCOMPARE(store.contacts().size(), 1);

    first.nickname = "Alice 2";
    QVERIFY(!store.add(first));
    QCOMPARE(store.contacts().size(), 1);
    QCOMPARE(store.contacts().at(0).nickname, QString("Alice 2"));

    chatbox::client::Contact second;
    second.user_id = 43;
    second.username = "bob";
    second.nickname = "Bob";
    QVERIFY(store.add(second));

    chatbox::client::ContactStore reloaded;
    QCOMPARE(reloaded.contacts().size(), 2);
    QVERIFY(reloaded.remove(42));
    QCOMPARE(reloaded.contacts().size(), 1);
    QCOMPARE(reloaded.contacts().at(0).user_id, quint64(43));

    chatbox::client::ContactStore other("other-scope");
    other.add(first);
    QCOMPARE(other.contacts().size(), 1);
    QCOMPARE(reloaded.contacts().size(), 1);

    settings.remove("contacts");
    settings.sync();
}

void TestClientCore::avatarCenterCrop() {
    QImage wide(2, 1, QImage::Format_RGB32);
    wide.fill(Qt::red);
    const QImage cropped = chatbox::client::centerCropSquare(wide);
    QVERIFY(!cropped.isNull());
    QCOMPARE(cropped.width(), 1);
    QCOMPARE(cropped.height(), 1);

    QImage square(5, 5, QImage::Format_RGB32);
    square.fill(Qt::blue);
    const QImage unchanged = chatbox::client::centerCropSquare(square);
    QCOMPARE(unchanged.size(), QSize(5, 5));

    QVERIFY(chatbox::client::centerCropSquare(QImage()).isNull());
}

void TestClientCore::avatarCompressionLimit() {
    QImage noisy(256, 256, QImage::Format_RGB32);
    quint32 state = 0x12345678u;
    for (int y = 0; y < noisy.height(); ++y) {
        for (int x = 0; x < noisy.width(); ++x) {
            state = state * 1664525u + 1013904223u;
            const int red = static_cast<int>((state >> 16) & 0xFF);
            state = state * 1664525u + 1013904223u;
            const int green = static_cast<int>((state >> 16) & 0xFF);
            state = state * 1664525u + 1013904223u;
            const int blue = static_cast<int>((state >> 16) & 0xFF);
            noisy.setPixel(x, y, qRgb(red, green, blue));
        }
    }

    const QByteArray jpeg = chatbox::client::encodeAvatarJpeg(noisy);
    QVERIFY(!jpeg.isEmpty());
    QVERIFY(jpeg.size() <= 48 * 1024);
    QVERIFY(jpeg.startsWith(QByteArray::fromHex("ffd8ff")));

    QVERIFY(chatbox::client::encodeAvatarJpeg(QImage()).isEmpty());
    QVERIFY(chatbox::client::encodeAvatarJpeg(noisy, 1).isEmpty());
}

void TestClientCore::avatarFallback() {
    const QImage fallback = chatbox::client::fallbackAvatar("Alice", 42, 64);
    QVERIFY(!fallback.isNull());
    QCOMPARE(fallback.size(), QSize(64, 64));
    QVERIFY(fallback.hasAlphaChannel());
    QVERIFY(fallback.pixelColor(32, 32).alpha() > 0);
    QCOMPARE(fallback.pixelColor(0, 0).alpha(), 0);

    const QImage same = chatbox::client::fallbackAvatar("Alice", 42, 64);
    QVERIFY(fallback == same);

    QVERIFY(!chatbox::client::fallbackAvatar(QString(), 42, 64).isNull());
    QVERIFY(chatbox::client::fallbackAvatar("Alice", 42, 0).isNull());
}

void TestClientCore::avatarCacheVersion() {
    chatbox::client::AvatarCache cache;
    QImage image;
    QVERIFY(!cache.needsFetch(7, 0));
    QVERIFY(cache.needsFetch(7, 3));
    QVERIFY(!cache.lookup(7, image));
    QVERIFY(!cache.contains(7));

    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(Qt::red);
    cache.store(7, 3, source);
    QVERIFY(cache.contains(7));
    QVERIFY(!cache.needsFetch(7, 3));
    QVERIFY(cache.lookup(7, image));
    QVERIFY(image == source);
    QVERIFY(cache.needsFetch(7, 4));

    cache.store(8, 1, QImage());
    QVERIFY(cache.contains(8));
    QVERIFY(!cache.needsFetch(8, 1));
    QVERIFY(!cache.lookup(8, image));

    cache.remove(7);
    QVERIFY(!cache.contains(7));
    QVERIFY(cache.needsFetch(7, 3));
    QVERIFY(cache.contains(8));

    cache.clear();
    QVERIFY(!cache.contains(8));
    QVERIFY(cache.needsFetch(7, 3));
    QVERIFY(!cache.lookup(7, image));
}

void TestClientCore::avatarCacheIgnoresOlderVersions() {
    chatbox::client::AvatarCache cache;
    QImage image;

    QImage version2(8, 8, QImage::Format_RGB32);
    version2.fill(Qt::green);
    QImage version1(8, 8, QImage::Format_RGB32);
    version1.fill(Qt::red);
    QImage version2_update(8, 8, QImage::Format_RGB32);
    version2_update.fill(Qt::blue);

    cache.store(11, 2, version2);
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version2);
    QVERIFY(!cache.needsFetch(11, 2));

    // A late response for version 1 must not overwrite version 2.
    cache.store(11, 1, version1);
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version2);
    QVERIFY(!cache.needsFetch(11, 2));

    // An update for the same version is accepted.
    cache.store(11, 2, version2_update);
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version2_update);

    // Version 0 is older than any loaded avatar version and is ignored.
    cache.store(11, 0, QImage());
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version2_update);

    // A newer version replaces the cached entry.
    cache.store(11, 3, version1);
    QVERIFY(cache.lookup(11, image));
    QVERIFY(image == version1);

    // A newer explicit "no avatar" entry blocks a late older image.
    cache.store(12, 2, QImage());
    QVERIFY(!cache.lookup(12, image));
    cache.store(12, 1, version1);
    QVERIFY(!cache.lookup(12, image));
    QVERIFY(!cache.needsFetch(12, 2));
}

void TestClientCore::profileDisplayNameFallsBackToNickname() {
    chatbox::client::UserProfile profile;
    profile.nickname = QString::fromUtf8("昵称");
    QCOMPARE(chatbox::client::profileDisplayName(profile), QString::fromUtf8("昵称"));

    profile.remark = QString::fromUtf8("备注");
    QCOMPARE(chatbox::client::profileDisplayName(profile), QString::fromUtf8("备注"));

    profile.remark = QString::fromUtf8("  备注  ");
    QCOMPARE(chatbox::client::profileDisplayName(profile), QString::fromUtf8("备注"));

    profile.remark = "   ";
    QCOMPARE(chatbox::client::profileDisplayName(profile), QString::fromUtf8("昵称"));
}

void TestClientCore::profileDialogSelfMode() {
    ProfileDialog dialog;
    chatbox::client::UserProfile profile;
    profile.user_id = 278;
    profile.username = "alice";
    profile.nickname = QString::fromUtf8("爱丽丝");
    profile.signature.clear();
    profile.relationship = 0;

    dialog.setProfile(profile, true);
    dialog.setAttribute(Qt::WA_DontShowOnScreen, true);
    dialog.show();
    QVERIFY(dialog.isVisible());
    QVERIFY(dialog.width() >= 420);
    dialog.hide();

    auto* avatar = dialog.findChild<QLabel*>("ProfileAvatar");
    auto* name = dialog.findChild<QLabel*>("ProfileNameLabel");
    auto* username = dialog.findChild<QLabel*>("ProfileUsernameLabel");
    auto* uid = dialog.findChild<QLabel*>("ProfileUidLabel");
    auto* signature = dialog.findChild<QLabel*>("ProfileSignatureLabel");
    auto* nickname_edit = dialog.findChild<QLineEdit*>("ProfileNicknameEdit");
    auto* signature_edit = dialog.findChild<QPlainTextEdit*>("ProfileSignatureEdit");
    auto* save = dialog.findChild<QPushButton*>("ProfileSaveButton");
    auto* remove_avatar = dialog.findChild<QPushButton*>("ProfileRemoveAvatarButton");
    auto* message = dialog.findChild<QPushButton*>("ProfileMessageButton");
    auto* status = dialog.findChild<QLabel*>("ProfileStatusLabel");
    auto* nickname_count = dialog.findChild<QLabel*>("ProfileNicknameCountLabel");
    auto* signature_count = dialog.findChild<QLabel*>("ProfileSignatureCountLabel");
    QVERIFY(avatar != nullptr);
    QVERIFY(name != nullptr);
    QVERIFY(username != nullptr);
    QVERIFY(uid != nullptr);
    QVERIFY(signature != nullptr);
    QVERIFY(nickname_edit != nullptr);
    QVERIFY(signature_edit != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(remove_avatar != nullptr);
    QVERIFY(message != nullptr);
    QVERIFY(status != nullptr);
    QVERIFY(nickname_count != nullptr);
    QVERIFY(signature_count != nullptr);

    QCOMPARE(name->text(), QString::fromUtf8("爱丽丝"));
    QCOMPARE(username->text(), QString("@alice"));
    QCOMPARE(uid->text(), QString("U00000278"));
    QCOMPARE(signature->text(), QString::fromUtf8("这个人很懒，什么都没写"));
    const QPixmap avatar_pixmap = avatar->pixmap(Qt::ReturnByValue);
    QVERIFY(!avatar_pixmap.isNull());
    QCOMPARE(avatar_pixmap.size(), QSize(96, 96));
    QVERIFY(nickname_edit->isVisibleTo(&dialog));
    QVERIFY(signature_edit->isVisibleTo(&dialog));
    QVERIFY(save->isVisibleTo(&dialog));
    QVERIFY(!message->isVisibleTo(&dialog));
    QVERIFY(!remove_avatar->isEnabled());

    QSignalSpy save_spy(&dialog, &ProfileDialog::saveRequested);
    nickname_edit->setText(QString::fromUtf8("新昵称"));
    signature_edit->setPlainText(QString::fromUtf8("新的签名"));
    save->click();
    QCOMPARE(save_spy.count(), 1);
    QList<QVariant> args = save_spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString::fromUtf8("新昵称"));
    QCOMPARE(args.at(1).toString(), QString::fromUtf8("新的签名"));
    QCOMPARE(args.at(2).toInt(), 0);
    QVERIFY(args.at(3).toByteArray().isEmpty());

    QImage image(12, 12, QImage::Format_RGB32);
    image.fill(Qt::red);
    dialog.setAvatar(image);
    QVERIFY(remove_avatar->isEnabled());
    remove_avatar->click();
    save->click();
    QCOMPARE(save_spy.count(), 1);
    args = save_spy.takeFirst();
    QCOMPARE(args.at(2).toInt(), 2);
    QVERIFY(args.at(3).toByteArray().isEmpty());

    nickname_edit->setText("   ");
    save->click();
    QCOMPARE(save_spy.count(), 0);
    QVERIFY(status->text().contains(QString::fromUtf8("昵称不能为空")));

    nickname_edit->setText(QString(33, QChar(0x4E00)));
    save->click();
    QCOMPARE(save_spy.count(), 0);
    QVERIFY(status->text().contains(QString::fromUtf8("32")));

    nickname_edit->setText(QString::fromUtf8("昵称"));
    signature_edit->setPlainText(QString(129, QChar('b')));
    save->click();
    QCOMPARE(save_spy.count(), 0);
    QVERIFY(status->text().contains(QString::fromUtf8("128")));

    // Counters and save both use trimmed text, so trailing whitespace does
    // not turn an otherwise valid boundary value into an error.
    dialog.setProfile(profile, true);
    const QString padded_nickname = QString(32, QChar('a')) + "   ";
    const QString padded_signature = QString(128, QChar('b')) + "   ";
    nickname_edit->setText(padded_nickname);
    signature_edit->setPlainText(padded_signature);
    QCOMPARE(nickname_count->text(), QString("32/32"));
    QCOMPARE(signature_count->text(), QString("128/128"));
    save->click();
    QCOMPARE(save_spy.count(), 1);
    args = save_spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString(32, QChar('a')));
    QCOMPARE(args.at(1).toString(), QString(128, QChar('b')));
    QCOMPARE(args.at(2).toInt(), 0);

    // 32 emoji are 32 code points but 64 UTF-16 units: they must be accepted
    // when paired with a 128-code-point signature.
    QVector<uint> emoji_points;
    for (int index = 0; index < 32; ++index) {
        emoji_points.append(0x1F600);
    }
    const QString emoji_nickname =
        QString::fromUcs4(emoji_points.constData(), emoji_points.size());
    dialog.setProfile(profile, true);
    nickname_edit->setText(emoji_nickname);
    signature_edit->setPlainText(QString(128, QChar('b')));
    save->click();
    QCOMPARE(save_spy.count(), 1);
    args = save_spy.takeFirst();
    QCOMPARE(args.at(0).toString(), emoji_nickname);
    QCOMPARE(args.at(1).toString(), QString(128, QChar('b')));
    QCOMPARE(args.at(2).toInt(), 0);

    emoji_points.append(0x1F600);
    nickname_edit->setText(
        QString::fromUcs4(emoji_points.constData(), emoji_points.size()));
    save->click();
    QCOMPARE(save_spy.count(), 0);

    dialog.setBusy(true);
    QVERIFY(!save->isEnabled());
    QVERIFY(!remove_avatar->isEnabled());
    QVERIFY(!nickname_edit->isEnabled());
    QVERIFY(!signature_edit->isEnabled());
    dialog.setBusy(false);
    QVERIFY(save->isEnabled());
    QVERIFY(nickname_edit->isEnabled());
    QVERIFY(signature_edit->isEnabled());
}

void TestClientCore::profileDialogViewModeActions() {
    ProfileDialog dialog;
    chatbox::client::UserProfile profile;
    profile.user_id = 42;
    profile.username = "bob";
    profile.nickname = QString::fromUtf8("鲍勃");
    profile.remark = QString::fromUtf8("老鲍");
    profile.online = true;
    profile.relationship = 1;

    dialog.setProfile(profile, false);

    auto* name = dialog.findChild<QLabel*>("ProfileNameLabel");
    auto* secondary = dialog.findChild<QLabel*>("ProfileNicknameLabel");
    auto* online = dialog.findChild<QLabel*>("ProfileOnlineLabel");
    auto* nickname_edit = dialog.findChild<QLineEdit*>("ProfileNicknameEdit");
    auto* save = dialog.findChild<QPushButton*>("ProfileSaveButton");
    auto* message = dialog.findChild<QPushButton*>("ProfileMessageButton");
    auto* add = dialog.findChild<QPushButton*>("ProfileAddFriendButton");
    auto* remark = dialog.findChild<QPushButton*>("ProfileRemarkButton");
    auto* remove = dialog.findChild<QPushButton*>("ProfileRemoveFriendButton");
    QVERIFY(name != nullptr);
    QVERIFY(secondary != nullptr);
    QVERIFY(online != nullptr);
    QVERIFY(nickname_edit != nullptr);
    QVERIFY(save != nullptr);
    QVERIFY(message != nullptr);
    QVERIFY(add != nullptr);
    QVERIFY(remark != nullptr);
    QVERIFY(remove != nullptr);

    QCOMPARE(name->text(), QString::fromUtf8("老鲍"));
    QVERIFY(secondary->isVisibleTo(&dialog));
    QVERIFY(secondary->text().contains(QString::fromUtf8("鲍勃")));
    QVERIFY(online->text().contains(QString::fromUtf8("在线")));
    QVERIFY(!nickname_edit->isVisibleTo(&dialog));
    QVERIFY(!save->isVisibleTo(&dialog));
    QVERIFY(message->isVisibleTo(&dialog));
    QVERIFY(remark->isVisibleTo(&dialog));
    QVERIFY(remove->isVisibleTo(&dialog));
    QVERIFY(!add->isVisibleTo(&dialog));

    QSignalSpy message_spy(&dialog, &ProfileDialog::messageRequested);
    QSignalSpy remark_spy(&dialog, &ProfileDialog::remarkRequested);
    QSignalSpy remove_spy(&dialog, &ProfileDialog::removeFriendRequested);
    message->click();
    remark->click();
    remove->click();
    QCOMPARE(message_spy.count(), 1);
    QCOMPARE(remark_spy.count(), 1);
    QCOMPARE(remove_spy.count(), 1);
    QCOMPARE(message_spy.at(0).at(0).toULongLong(), qulonglong(42));
    QCOMPARE(remark_spy.at(0).at(0).toULongLong(), qulonglong(42));
    QCOMPARE(remove_spy.at(0).at(0).toULongLong(), qulonglong(42));

    profile.relationship = 2;
    profile.remark.clear();
    profile.nickname = "Carol";
    profile.online = false;
    dialog.setProfile(profile, false);
    QCOMPARE(name->text(), QString("Carol"));
    QVERIFY(!secondary->isVisibleTo(&dialog));
    QVERIFY(online->text().contains(QString::fromUtf8("离线")));
    QVERIFY(add->isVisibleTo(&dialog));
    QVERIFY(!message->isVisibleTo(&dialog));

    QSignalSpy add_spy(&dialog, &ProfileDialog::addFriendRequested);
    add->click();
    QCOMPARE(add_spy.count(), 1);
    QCOMPARE(add_spy.at(0).at(0).toULongLong(), qulonglong(42));

    auto* refresh = dialog.findChild<QPushButton*>("ProfileRefreshButton");
    QVERIFY(refresh != nullptr);
    QVERIFY(refresh->isVisibleTo(&dialog));
    QSignalSpy refresh_spy(&dialog, &ProfileDialog::refreshRequested);
    refresh->click();
    QCOMPARE(refresh_spy.count(), 1);
    QCOMPARE(refresh_spy.at(0).at(0).toULongLong(), qulonglong(42));

    profile.relationship = 3;
    dialog.setProfile(profile, false);
    auto* pending = dialog.findChild<QPushButton*>("ProfilePendingButton");
    QVERIFY(pending != nullptr);
    QVERIFY(pending->isVisibleTo(&dialog));
    QCOMPARE(pending->text(), QString::fromUtf8("申请已发送"));
    QVERIFY(!pending->isEnabled());

    pending->setToolTip("stale");
    dialog.setProfile(profile, false);
    QVERIFY(pending->toolTip().isEmpty());

    profile.relationship = 4;
    dialog.setProfile(profile, false);
    QVERIFY(pending->isVisibleTo(&dialog));
    QCOMPARE(pending->text(), QString::fromUtf8("查看好友申请"));
    QVERIFY(pending->isEnabled());
    QVERIFY(pending->toolTip().isEmpty());
    QSignalSpy friend_requests_spy(&dialog,
                                   &ProfileDialog::friendRequestsRequested);
    pending->click();
    QCOMPARE(friend_requests_spy.count(), 1);

    dialog.setBusy(true);
    QVERIFY(!add->isEnabled());
    QVERIFY(!pending->isEnabled());
    QVERIFY(!dialog.findChild<QPushButton*>("ProfileRefreshButton")->isEnabled());
    dialog.setBusy(false);
    QVERIFY(pending->isEnabled());
}

// the conversation HTML must use the theme bubble palette (own messages keep
// the green semantic tint), not the retired WhatsApp-era greys.
void TestClientCore::messageBubbleColoursComeFromTheme() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-19 12:00:00");
    chatbox::client::ChatMessage own;
    own.client_msg_id = "c1";
    own.message_id = 1;
    own.sender_id = 1;
    own.content = "hi";
    own.created_at_utc = created;
    own.outgoing = true;

    chatbox::client::ChatMessage other;
    other.client_msg_id = "c2";
    other.message_id = 2;
    other.sender_id = 9;
    other.sender_name = "peer";
    other.content = "yo";
    other.created_at_utc = created;

    chatbox::client::ChatMessage failed;
    failed.client_msg_id = "c3";
    failed.sender_id = 1;
    failed.content = "nope";
    failed.created_at_utc = created;
    failed.outgoing = true;
    failed.status = chatbox::client::MessageStatus::kFailed;

    const QString html = chatbox::client::renderConversationHtml({own, other, failed}, 1);
    QVERIFY(html.contains(QString::fromLatin1(chatbox::client::theme::kBubbleOwn)));
    QVERIFY(html.contains(QString::fromLatin1(chatbox::client::theme::kBubbleOther)));
    QVERIFY(!html.contains(QStringLiteral("#DCF8C6")));
    QVERIFY(!html.contains(QStringLiteral("#F2F2F2")));
    // a failed send is an error state, so it uses the semantic-down colour
    QVERIFY(html.contains(QString::fromLatin1(chatbox::client::theme::kSemanticDown)));
    QVERIFY(html.contains(QStringLiteral("retry:c3")));
}

// guards the palette in resources/style.qss (embedded as :/style.qss at build
// time) against drifting from core/ui_theme.h; bubble, banner and semantic
// colours live in C++/HTML only and are not asserted here.
double contrastRatio(const QColor& foreground, const QColor& background) {
    const auto luminance = [](const QColor& color) {
        const auto channel = [](double value) {
            return value <= 0.03928 ? value / 12.92
                                    : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
               0.0722 * channel(color.blueF());
    };
    const double a = luminance(foreground);
    const double b = luminance(background);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

void TestClientCore::contrastTokensMeetAa() {
    const auto& palette = chatbox::client::theme::lightPalette();
    const QColor white(palette.canvas);
    const QColor soft(palette.surfaceSoft);
    const QColor own(palette.bubbleOwn);
    const QColor muted(palette.muted);
    const QColor muted_soft(palette.mutedSoft);

    const QVector<QPair<QString, double>> checks = {
        {QStringLiteral("kMuted on canvas"), contrastRatio(muted, white)},
        {QStringLiteral("kMuted on soft"), contrastRatio(muted, soft)},
        {QStringLiteral("kMutedSoft on canvas"), contrastRatio(muted_soft, white)},
        {QStringLiteral("kMutedSoft on bubble"), contrastRatio(muted_soft, own)},
    };
    for (const auto& check : checks) {
        QVERIFY2(check.second >= 4.5,
                 qPrintable(QStringLiteral("%1 = %2").arg(check.first).arg(check.second)));
    }
}

void TestClientCore::darkSurfaceContrastMeetsAa() {
    const auto& dark = chatbox::client::theme::darkPalette();
    const QColor muted_soft(dark.mutedSoft);
    // Bubble interiors use their own token now: a colour tuned for the canvas
    // cannot also clear 4.5:1 on the own-bubble fill.
    const QColor bubble_meta(dark.bubbleMeta);
    const QColor own(dark.bubbleOwn);
    const QColor other(dark.bubbleOther);
    const QColor canvas(dark.canvas);
    const QColor strong(dark.surfaceStrong);

    const QVector<QPair<QString, double>> checks = {
        {QStringLiteral("kBubbleMeta on dark own bubble"),
         contrastRatio(bubble_meta, own)},
        {QStringLiteral("kBubbleMeta on dark other bubble"),
         contrastRatio(bubble_meta, other)},
        {QStringLiteral("kMutedSoft on dark canvas"),
         contrastRatio(muted_soft, canvas)},
        {QStringLiteral("kMutedSoft on dark recalled surface"),
         contrastRatio(muted_soft, strong)},
    };
    for (const auto& check : checks) {
        QVERIFY2(check.second >= 4.5,
                 qPrintable(QStringLiteral("%1 = %2")
                                .arg(check.first)
                                .arg(check.second, 0, 'f', 2)));
    }
}

void TestClientCore::primaryButtonContrastMeetsAa() {
    const auto check = [](const chatbox::client::theme::ThemePalette& palette,
                          const QString& label) {
        const QColor text(palette.onPrimary);
        const QVector<QPair<QString, QColor>> surfaces = {
            {QStringLiteral("primary"), QColor(palette.primary)},
            {QStringLiteral("primary hover"), QColor(palette.primaryHover)},
            {QStringLiteral("primary active"), QColor(palette.primaryActive)},
        };
        for (const auto& surface : surfaces) {
            const double ratio = contrastRatio(text, surface.second);
            QVERIFY2(ratio >= 4.5,
                     qPrintable(QStringLiteral("%1 %2 = %3")
                                    .arg(label, surface.first)
                                    .arg(ratio, 0, 'f', 2)));
        }
    };
    check(chatbox::client::theme::lightPalette(), QStringLiteral("light"));
    check(chatbox::client::theme::darkPalette(), QStringLiteral("dark"));
}

// Telegram separates bubbles with fill + a hairline, not a 3:1 outline; a
// strong border reads as a boxed table and kills the whole look. The floor is
// therefore "the edge is perceptible" (1.3:1) plus "the fill differs from the
// canvas", which together are what actually keeps a bubble from disappearing.
void TestClientCore::bubbleBoundaryMeetsNonTextContrast() {
    const auto check = [](const chatbox::client::theme::ThemePalette& palette,
                          const QString& label) {
        const double ratio =
            contrastRatio(QColor(palette.bubbleBorder), QColor(palette.canvas));
        QVERIFY2(ratio >= 1.3,
                 qPrintable(QStringLiteral("%1 bubble border = %2")
                                .arg(label)
                                .arg(ratio, 0, 'f', 2)));
        const double other_ratio =
            contrastRatio(QColor(palette.bubbleBorder), QColor(palette.bubbleOther));
        QVERIFY2(other_ratio >= 1.3,
                 qPrintable(QStringLiteral("%1 border on other bubble = %2")
                                .arg(label)
                                .arg(other_ratio, 0, 'f', 2)));
        QVERIFY2(QColor(palette.bubbleOther) != QColor(palette.surfaceSoft),
                 qPrintable(QStringLiteral("%1 other bubble equals the chat canvas")
                                .arg(label)));
        QVERIFY2(QColor(palette.surfaceStrong) != QColor(palette.canvas),
                 qPrintable(QStringLiteral("%1 recalled surface equals canvas")
                                .arg(label)));
    };
    check(chatbox::client::theme::lightPalette(), QStringLiteral("light"));
    check(chatbox::client::theme::darkPalette(), QStringLiteral("dark"));
}

void TestClientCore::darkPaletteMatchesSpec() {
    const auto& dark = chatbox::client::theme::darkPalette();
    QCOMPARE(QString::fromLatin1(dark.canvas), QStringLiteral("#17212B"));
    QCOMPARE(QString::fromLatin1(dark.surfaceSoft), QStringLiteral("#0E1621"));
    QCOMPARE(QString::fromLatin1(dark.ink), QStringLiteral("#FFFFFF"));
    QCOMPARE(QString::fromLatin1(dark.accent), QStringLiteral("#5EB5F7"));
    QCOMPARE(QString::fromLatin1(dark.bubbleOther), QStringLiteral("#182533"));

    QFile style(QStringLiteral(":/style-dark.qss"));
    QVERIFY2(style.open(QIODevice::ReadOnly | QIODevice::Text),
             "style-dark.qss must be embedded in the resource file");
    const QString css = QString::fromUtf8(style.readAll());
    QVERIFY(css.contains(QStringLiteral("#17212B")));
    QVERIFY(css.contains(QStringLiteral("#5EB5F7")));
}

void TestClientCore::themeTokensMatchContrastSpec() {
    const QString source = readClientSource(QStringLiteral("core/ui_theme.cpp"));
    QVERIFY2(!source.isEmpty(), "core/ui_theme.h must be readable");
    QVERIFY(source.contains(QStringLiteral("#6A6E74")));
    QVERIFY(source.contains(QStringLiteral("#6B7076")));
    QVERIFY(source.contains(QStringLiteral("kSemanticUpText")));
    QVERIFY(source.contains(QStringLiteral("#3BA55D")));
    QVERIFY(source.contains(QStringLiteral("kOnlineDot")));
    QVERIFY(source.contains(QStringLiteral("#4DCD5E")));
    QVERIFY(source.contains(QStringLiteral("kQuoteBg")));
    QVERIFY(source.contains(QStringLiteral("#F1F1F1")));
    QVERIFY(source.contains(QStringLiteral("kDangerText")));
    QVERIFY(source.contains(QStringLiteral("#E53935")));
    QVERIFY(source.contains(QStringLiteral("kDisabledText")));
    QVERIFY(source.contains(QStringLiteral("#6B7076")));
}

void TestClientCore::styleSheetHasVisibleKeyboardFocus() {
    const QString qss = readClientSource(QStringLiteral("resources/style.qss"));
    QVERIFY2(!qss.isEmpty(), "resources/style.qss must be readable");
    QVERIFY(!qss.contains(QStringLiteral("outline: 0")));
    const QStringList selectors = {
        QStringLiteral("QListWidget[keyboardFocus=\"true\"]:focus"),
        QStringLiteral("QPushButton[keyboardFocus=\"true\"]:focus"),
        QStringLiteral("QPushButton#PrimaryButton[keyboardFocus=\"true\"]:focus"),
        QStringLiteral("QPushButton#SendButton[keyboardFocus=\"true\"]:focus"),
        QStringLiteral("QCheckBox[keyboardFocus=\"true\"]::indicator:focus"),
        QStringLiteral("QToolButton[keyboardFocus=\"true\"]:focus"),
    };
    for (const QString& selector : selectors) {
        QVERIFY2(qss.contains(selector), qPrintable(selector));
    }
}

void TestClientCore::keyboardFocusReasonIsKeyboardOnly() {
    QVERIFY(chatbox::client::isKeyboardFocusReason(Qt::TabFocusReason));
    QVERIFY(chatbox::client::isKeyboardFocusReason(Qt::BacktabFocusReason));
    QVERIFY(chatbox::client::isKeyboardFocusReason(Qt::ShortcutFocusReason));
    QVERIFY(!chatbox::client::isKeyboardFocusReason(Qt::MouseFocusReason));
    QVERIFY(!chatbox::client::isKeyboardFocusReason(Qt::ActiveWindowFocusReason));
    QVERIFY(!chatbox::client::isKeyboardFocusReason(Qt::OtherFocusReason));
}

void TestClientCore::focusRingFilterHonoursKeyboardAndMouse() {
    QWidget widget;
    chatbox::client::FocusRingFilter filter;
    widget.installEventFilter(&filter);

    QFocusEvent keyboard_focus(QEvent::FocusIn, Qt::TabFocusReason);
    QApplication::sendEvent(&widget, &keyboard_focus);
    QVERIFY(widget.property("keyboardFocus").toBool());

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1),
                      QPointF(1, 1), Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QVERIFY(!widget.property("keyboardFocus").toBool());

    QFocusEvent mouse_focus(QEvent::FocusIn, Qt::MouseFocusReason);
    QApplication::sendEvent(&widget, &mouse_focus);
    QVERIFY(!widget.property("keyboardFocus").toBool());
}

void TestClientCore::mainWindowExposesKeyboardShortcuts() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY(source.contains(QStringLiteral("Ctrl+F")));
    QVERIFY(source.contains(QStringLiteral("Ctrl+Shift+F")));
    QVERIFY(source.contains(QStringLiteral("Ctrl+,")));
    QVERIFY(source.contains(QStringLiteral("Alt+Up")));
    QVERIFY(source.contains(QStringLiteral("Alt+Down")));
    QVERIFY(source.contains(QStringLiteral("Qt::Key_Escape")));
    QVERIFY(source.contains(QStringLiteral("clearReply()")));
}

void TestClientCore::mainWindowAccessibilityLabels() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY(source.contains(QStringLiteral("more_button_->setAccessibleName")));
    QVERIFY(source.contains(QStringLiteral("send_button_->setAccessibleName")));
    QVERIFY(source.contains(QStringLiteral("mention_button_->setAccessibleName")));
    QVERIFY(source.contains(QStringLiteral("context_avatar_->setAccessibleName")));
    QVERIFY(source.contains(QStringLiteral("my_profile_avatar_->setAccessibleName")));
    QVERIFY(source.contains(QStringLiteral("context_avatar_->setAccessibleDescription")));
    QVERIFY(source.contains(QStringLiteral("my_profile_avatar_->setAccessibleDescription")));
    QVERIFY(source.contains(QStringLiteral("room_list_->setAccessibleName")));
    QVERIFY(source.contains(QStringLiteral("conversation_list_->setAccessibleName")));
    QVERIFY(source.contains(QStringLiteral("member_list_->setAccessibleName")));
}

void TestClientCore::friendItemsExposeAccessibleText() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY(source.contains(QStringLiteral("Qt::AccessibleTextRole")));
}

void TestClientCore::mainWindowOmitsJoinRoomStatusNoise() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_net.cpp"));
    QVERIFY2(!source.isEmpty(), "ui/main_window_net.cpp must be readable");
    QVERIFY(!source.contains(QStringLiteral("已加入房间")));
    const QString chat_source =
        readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    QVERIFY2(!chat_source.isEmpty(), "ui/main_window_chat.cpp must be readable");
    QVERIFY(!chat_source.contains(QStringLiteral("正在进入房间")));
}

void TestClientCore::friendListAvoidsDuplicateGroupLabels() {
    const QString source =
        readClientSource(QStringLiteral("ui/main_window_contacts.cpp"));
    QVERIFY2(!source.isEmpty(), "ui/main_window_contacts.cpp must be readable");
    const QString section =
        functionSection(source, QStringLiteral("void MainWindow::refreshFriendListView()"));
    QVERIFY(!section.isEmpty());
    QVERIFY2(section.contains(QStringLiteral("show_group_headers")),
             "only render 在线/离线 headers when both groups are non-empty");
}

void TestClientCore::themeTokensMatchStyleSheet() {
    // resolve relative to the test binary first so the guard also works when
    // the binary is launched outside tests/ (Qt Creator, CI, ...)
    QString style_path = QCoreApplication::applicationDirPath() +
                         QStringLiteral("/../../resources/style.qss");
    if (!QFile::exists(style_path)) {
        style_path = QStringLiteral("../resources/style.qss");
    }
    QFile style_file(style_path);
    QVERIFY2(style_file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(style_path + ": " + style_file.errorString()));
    const QString css = QString::fromUtf8(style_file.readAll()).toLower();

    // QSS 负责的颜色（App 从 :/style.qss 加载同一文件，构建时嵌入）。
    // 注意：kBody/kBubbleOwn/kBubbleOther/kSemanticUp/kSemanticDown/kBanner*
    // 只出现在 C++/HTML 里，不在 QSS，故不在此列表。
    const auto& light = chatbox::client::theme::lightPalette();
    const QStringList required = {
        light.accent,
        light.accentHover,
        light.accentActive,
        light.canvas,
        light.surfaceSoft,
        light.surfaceStrong,
        light.surfaceStrongHover,
        light.hairline,
        light.ink,
        light.body,
        light.muted,
        light.mutedSoft,
        light.disabledText,
        light.dangerText,
        light.selectionBg,
        light.selectionBorder,
    };
    QStringList allowed;
    for (const QString& token : required) {
        QVERIFY2(css.contains(token.toLower()), qPrintable(token));
        allowed.append(token.toLower());
    }

    // every colour in the sheet must be one of the palette tokens above, so a
    // one-off hard-coded grey cannot sneak in unnoticed
    QRegularExpression colour(QStringLiteral("#[0-9a-f]{6}"));
    QRegularExpressionMatchIterator it = colour.globalMatch(css);
    while (it.hasNext()) {
        const QString found = it.next().captured(0);
        QVERIFY2(allowed.contains(found), qPrintable(found));
    }

    // The palette deliberately follows Telegram Desktop now, so most of the
    // old "never go back" greys are legal again. Only colours that belong to
    // neither palette stay banned.
    const QStringList retired = {
        "#2474c4", "#e9ebee", "#a5a9ae", "#c4c9cf", "#a9afb6", "#e6e8eb",
    };
    for (const QString& token : retired) {
        QVERIFY2(!css.contains(token), qPrintable(token));
    }
}

// the Coinbase elevation is a single soft drop (0 4px 12px rgba(0,0,0,0.04));
// QSS cannot express box-shadow, so the action bar carries the effect instead.
void TestClientCore::messageActionBarIsKeyboardReachable() {
    MessageActionBar bar;
    auto* reply = bar.findChild<QPushButton*>(QStringLiteral("MessageActionReplyButton"));
    auto* recall = bar.findChild<QPushButton*>(QStringLiteral("MessageActionRecallButton"));
    QVERIFY(reply != nullptr);
    QVERIFY(recall != nullptr);
    QCOMPARE(reply->focusPolicy(), Qt::TabFocus);
    QCOMPARE(recall->focusPolicy(), Qt::TabFocus);
    QCOMPARE(reply->accessibleName(), QStringLiteral("回复此消息"));
    QCOMPARE(recall->accessibleName(), QStringLiteral("撤回此消息"));

    const QString qss = readClientSource(QStringLiteral("resources/style.qss"));
    QVERIFY(qss.contains(QStringLiteral(
        "QFrame#MessageActionBar QPushButton[keyboardFocus=\"true\"]:focus")));
}

void TestClientCore::lineEditsAvoidVerticalClipping() {
    for (const QString& file : {QStringLiteral("resources/style.qss"),
                                QStringLiteral("resources/style-dark.qss")}) {
        const QString qss = readClientSource(file);
        QVERIFY2(!qss.isEmpty(), qPrintable(file));
        QVERIFY2(qss.contains(QStringLiteral("min-height: 36px")), qPrintable(file));
        QVERIFY2(qss.contains(QStringLiteral("padding: 0 14px")), qPrintable(file));
    }
}

void TestClientCore::messageActionBarHasSingleSoftShadow() {
    MessageActionBar bar;
    auto* effect = qobject_cast<QGraphicsDropShadowEffect*>(bar.graphicsEffect());
    QVERIFY(effect != nullptr);
    QCOMPARE(effect->blurRadius(), 12.0);
    QCOMPARE(effect->offset(), QPointF(0, 4));
    QCOMPARE(effect->color(), QColor(0, 0, 0, 10));
}

// The shell picks a frame per context (16px tray, 24px taskbar @150%, 256px
// Explorer); a single-frame .ico is what made the old icon look blurry.
void TestClientCore::brandAppIconExposesSizes() {
    QIcon icon(QStringLiteral(":/app.ico"));
    QList<QSize> sizes = icon.availableSizes();
    std::sort(sizes.begin(), sizes.end(),
              [](const QSize& a, const QSize& b) { return a.width() < b.width(); });
    QList<QSize> expected;
    const int wanted[] = {16, 24, 32, 48, 64, 128, 256};
    for (int side : wanted) {
        expected.append(QSize(side, side));
    }
    QCOMPARE(sizes, expected);
}

// Room badge (R2): accent disc + white speech bubble, no letter. Replaces the
// old green "1" circle in the sidebar and chat header.
void TestClientCore::roomAvatarIsBrandBadge() {
    const QImage img = chatbox::client::roomAvatar(36);
    QCOMPARE(img.size(), QSize(36, 36));
    QCOMPARE(img.pixelColor(18, 18), QColor(Qt::white));  // bubble body centre
    QCOMPARE(img.pixelColor(4, 18), QColor(chatbox::client::theme::kAccent));
    QCOMPARE(img.pixelColor(0, 0).alpha(), 0);  // round badge: corners empty
}

// The tray must show the brand mark, not Qt's stock information icon. The
// wiring itself needs a live system tray, so this is a source guard.
void TestClientCore::trayIconUsesBrandIcon() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "main window sources must be readable");
    QVERIFY2(!source.contains(QStringLiteral("SP_MessageBoxInformation")),
             "tray must not fall back to the stock Qt information icon");
    QVERIFY2(source.contains(QStringLiteral(":/app.ico")),
             "tray must use the brand icon resource");
}

// The online member list used to always draw the letter fallback, so members
// with an uploaded photo stayed anonymous. It must resolve icons through the
// avatar cache, request missing avatars through the same "ask once with
// version 0" path used for other peers, and repaint when an avatar arrives.
// The list needs a live connection, so this is a source guard.
void TestClientCore::memberListShowsAvatarFromCache() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "main window sources must be readable");
    QVERIFY2(source.contains(QStringLiteral("QIcon MainWindow::memberAvatarIcon(")),
             "member list must resolve icons through the cache-aware helper");
    QVERIFY2(source.contains(QStringLiteral("item->setIcon(memberAvatarIcon(")),
             "member list items must use the cache-aware avatar helper");
    QVERIFY2(!source.contains(
                 QStringLiteral("item->setIcon(avatarIcon(nickname, member_id))")),
             "member list must not hard-code the letter fallback");
    QVERIFY2(source.contains(QStringLiteral("requestPeerAvatar(member_id)")),
             "member list must request missing member avatars");
    QVERIFY2(source.contains(QStringLiteral("refreshMemberListAvatar(")),
             "arriving avatars must repaint the member list");
}

// Every fallback letter avatar must come from one palette. main_window.cpp
// used to carry its own 6-colour table (giving 483 -> #9B59B6 purple) while
// core/avatar_utils.cpp owns the 8-colour table the profile dialog uses
// (483 -> #E8684A), so the same user showed two different colours.
// The rendering needs widgets, so this is a source guard.
void TestClientCore::fallbackAvatarPaletteIsSingleSource() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "main window sources must be readable");
    QVERIFY2(!source.contains(QStringLiteral("#9B59B6")) &&
                 !source.contains(QStringLiteral("#4CAF50")) &&
                 !source.contains(QStringLiteral("#F5A623")),
             "main_window must not carry its own fallback palette");
    QVERIFY2(source.contains(QStringLiteral("chatbox::client::fallbackAvatar(")),
             "fallback avatars must come from core/avatar_utils");
}

// The product is called ChatBox: window title, tray tooltip, unread-count
// title and the login card must all use it, and the old "ChatBox Client"
// string must not come back. Rendering needs widgets, so this is a source
// guard; brandIdentifiersAreUnified covers the internal identifiers.
void TestClientCore::appTitleUsesBrandName() {
    const QString window = readMainWindowSource();
    QVERIFY2(!window.isEmpty(), "main window sources must be readable");
    QVERIFY2(!window.contains(QStringLiteral("ChatBox Client")),
             "the old product name must not come back");
    QVERIFY2(window.contains(QStringLiteral("setWindowTitle(\"ChatBox\"")),
             "window title must use the ChatBox brand");
    QVERIFY2(window.contains(QStringLiteral("setToolTip(\"ChatBox\"")),
             "tray tooltip must use the ChatBox brand");
    QVERIFY2(window.contains(QStringLiteral("ChatBox (%1)")),
             "unread-count title must use the ChatBox brand");

    const QString login = readClientSource(QStringLiteral("ui/login_page.cpp"));
    QVERIFY2(!login.isEmpty(), "ui/login_page.cpp must be readable");
    QVERIFY2(login.contains(QStringLiteral("QLabel(\"ChatBox\"")),
             "login card title must use the ChatBox brand");
}

// Sessions and preferences used to live under the Chatroom/ChatroomClient
// identifiers. The ChatBox rename switches the store to ChatBox/ChatBox, so
// the first start has to copy the legacy values over; otherwise every user
// would be logged out by the rebrand.
void TestClientCore::settingsMigrationCopiesLegacyStore() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString legacy_path = dir.filePath(QStringLiteral("legacy.ini"));
    const QString current_path = dir.filePath(QStringLiteral("current.ini"));
    {
        QSettings legacy(legacy_path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("session/token"), QStringLiteral("token-123"));
        legacy.setValue(QStringLiteral("server/host"), QStringLiteral("192.168.2.113"));
        legacy.sync();
    }
    {
        QSettings current(current_path, QSettings::IniFormat);
        QSettings legacy(legacy_path, QSettings::IniFormat);
        QVERIFY(chatbox::client::migrateLegacySettings(current, legacy));
        QCOMPARE(current.value(QStringLiteral("session/token")).toString(),
                 QStringLiteral("token-123"));
        QCOMPARE(current.value(QStringLiteral("server/host")).toString(),
                 QStringLiteral("192.168.2.113"));
    }
    // A fresh install has nothing to copy and must not report a migration.
    {
        QSettings current(dir.filePath(QStringLiteral("fresh.ini")), QSettings::IniFormat);
        QSettings legacy(dir.filePath(QStringLiteral("empty-legacy.ini")), QSettings::IniFormat);
        QVERIFY(!chatbox::client::migrateLegacySettings(current, legacy));
    }
}

void TestClientCore::settingsMigrationMovesOldServerEndpoint() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings settings(dir.filePath(QStringLiteral("migrate.ini")),
                       QSettings::IniFormat);
    settings.setValue(QStringLiteral("server/host"), QStringLiteral("frp-oil.com"));
    settings.setValue(QStringLiteral("server/port"), 15318);
    settings.setValue(QStringLiteral("session/token"), QStringLiteral("old-token"));
    QVERIFY(chatbox::client::migrateServerEndpoint(settings));
    QCOMPARE(settings.value(QStringLiteral("server/host")).toString(),
             QStringLiteral("121.5.168.189"));
    QCOMPARE(settings.value(QStringLiteral("server/port")).toInt(), 9443);
    QVERIFY(!settings.contains(QStringLiteral("session/token")));

    // A custom endpoint must not be overwritten.
    QSettings custom(dir.filePath(QStringLiteral("custom.ini")),
                     QSettings::IniFormat);
    custom.setValue(QStringLiteral("server/host"), QStringLiteral("example.test"));
    custom.setValue(QStringLiteral("server/port"), 12345);
    QVERIFY(!chatbox::client::migrateServerEndpoint(custom));
}

// The migration only ever fills an empty store: values written after the
// rename (for example a token refreshed by the new build) must win.
void TestClientCore::settingsMigrationNeverOverwritesCurrent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString legacy_path = dir.filePath(QStringLiteral("legacy.ini"));
    const QString current_path = dir.filePath(QStringLiteral("current.ini"));
    {
        QSettings legacy(legacy_path, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("session/token"), QStringLiteral("stale-token"));
        legacy.sync();
    }
    {
        QSettings current(current_path, QSettings::IniFormat);
        current.setValue(QStringLiteral("session/token"), QStringLiteral("fresh-token"));
        current.sync();
    }
    {
        QSettings current(current_path, QSettings::IniFormat);
        QSettings legacy(legacy_path, QSettings::IniFormat);
        QVERIFY(!chatbox::client::migrateLegacySettings(current, legacy));
        QCOMPARE(current.value(QStringLiteral("session/token")).toString(),
                 QStringLiteral("fresh-token"));
    }
}

// The rename unified the internal identifiers too: executable (chatbox.exe),
// project file, QSettings store and the server-side database/log defaults all
// read chatbox/ChatBox now. This is a source guard, the values live in build
// and deployment files that the test binary cannot execute.
void TestClientCore::brandIdentifiersAreUnified() {
    const auto read_source = [](const QString& relative) -> QString {
        QString path = QCoreApplication::applicationDirPath() +
                       QStringLiteral("/../../") + relative;
        if (!QFile::exists(path)) {
            path = QStringLiteral("../") + relative;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    };
    const QString main_source = read_source(QStringLiteral("main.cpp"));
    QVERIFY2(!main_source.isEmpty(), "main.cpp must be readable");
    QVERIFY2(main_source.contains(QStringLiteral("setOrganizationName(\"ChatBox\")")),
             "QSettings organisation must be ChatBox");
    QVERIFY2(main_source.contains(QStringLiteral("setApplicationName(\"ChatBox\")")),
             "QSettings application must be ChatBox");
    QVERIFY2(main_source.contains(QStringLiteral("migrateLegacySettings(")),
             "startup must migrate the pre-ChatBox settings store");
    QVERIFY2(main_source.contains(QStringLiteral("QStringLiteral(\"Chatroom\")")) &&
                 main_source.contains(QStringLiteral("QStringLiteral(\"ChatroomClient\")")),
             "the migration must read the legacy Chatroom/ChatroomClient store");

    const QString project = read_source(QStringLiteral("chatbox.pro"));
    QVERIFY2(!project.isEmpty(), "chatbox.pro must exist next to main.cpp (project file renamed)");
    QVERIFY2(project.contains(QStringLiteral("TARGET = chatbox")),
             "the client executable must be chatbox.exe");

    const QString server_config = read_source(QStringLiteral("../server/config.h"));
    QVERIFY2(!server_config.isEmpty(), "../server/config.h must be readable");
    QVERIFY2(server_config.contains(QStringLiteral("db_name = \"chatbox\"")),
             "the server database default must be chatbox");
    QVERIFY2(server_config.contains(QStringLiteral("logs/chatbox.log")),
             "the server log default must be chatbox.log");
}

// The Inno Setup template must reference the executable the build actually
// produces (chatbox.exe). ISCC is not installed on this machine, so the
// template is never executed during the build; this source guard is what
// catches a stale name (dead shortcuts, broken post-install launch).
void TestClientCore::installerTemplateTargetsChatboxExe() {
    QString path = QCoreApplication::applicationDirPath() +
                   QStringLiteral("/../../installer/chatbox.iss");
    if (!QFile::exists(path)) {
        path = QStringLiteral("../installer/chatbox.iss");
    }
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(path + ": " + file.errorString()));
    const QString source = QString::fromUtf8(file.readAll());
    QVERIFY2(source.contains(QStringLiteral("#define MyAppExeName \"chatbox.exe\"")),
             "the installer must target the executable the build produces");
    QVERIFY2(!source.contains(QStringLiteral("_client.exe")),
             "the installer must not reference a never-built executable name");
}

// 私聊不提供 @ 功能：输入区的 @ 按钮只在房间出现，私聊消息不再触发“有人@你”。
void TestClientCore::privateChatHasNoMentionEntry() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    const QString private_section = functionSection(
        source, QStringLiteral("void MainWindow::selectPrivate("));
    QVERIFY2(private_section.contains(QStringLiteral("mention_button_->hide()")),
             "private chats must hide the mention button");
    const QString room_section = functionSection(
        source, QStringLiteral("void MainWindow::selectRoom("));
    QVERIFY2(room_section.contains(QStringLiteral("mention_button_->show()")),
             "rooms keep the mention button");
}

void TestClientCore::privatePushSkipsMentionNotification() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    const QString private_push = caseSection(
        source, QStringLiteral("case chatbox::kCmdPrivatePush:"));
    QVERIFY2(!private_push.isEmpty(), "kCmdPrivatePush handler must exist");
    QVERIFY2(!private_push.contains(QStringLiteral("showMentionNotification")),
             "private messages must not raise a mention notification");
    const QString room_push =
        caseSection(source, QStringLiteral("case chatbox::kCmdChatPush:"));
    QVERIFY2(room_push.contains(QStringLiteral("showMentionNotification")),
             "rooms keep mention notifications");
}

// 头部只留 头像 + 标题 + ⋯；资料入口是头像，消息搜索在 ⋯ 菜单里。
void TestClientCore::headerUsesAvatarAndMoreMenu() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY2(!source.contains(QStringLiteral("context_profile_button_")),
             "the header must not keep a separate profile button");
    QVERIFY2(!source.contains(QStringLiteral("load_more_button_")),
             "load-more moved into the more menu");
    QVERIFY2(!source.contains(QStringLiteral("search_messages_button")),
             "message search moved out of the composer");
    QVERIFY2(source.contains(QStringLiteral("more_button_->setPopupMode(QToolButton::InstantPopup)")),
             "the header owns an instant-popup menu button");
    QVERIFY2(source.contains(QStringLiteral("more_button_->setIcon(chatbox::client::moreDotsIcon(")),
             "the header more control uses three filled dots");
    QVERIFY2(!source.contains(QStringLiteral("more_button_->setText")),
             "the header more control must not render thin text dots");
    // The indicator rule moved from an inline setStyleSheet into style.qss so
    // it follows the theme; assert against the stylesheet that actually
    // carries it now.
    const QString qss = readClientSource(QStringLiteral("resources/style.qss"));
    QVERIFY2(qss.contains(QStringLiteral("QToolButton#MoreButton::menu-indicator")),
             "the header more control must hide the menu indicator arrow");
    QVERIFY2(source.contains(QStringLiteral("消息搜索")) &&
                 source.contains(QStringLiteral("加载更早消息")),
             "the more menu carries search and load-more");
    const QString avatar_click =
        functionSection(source, QStringLiteral("bool MainWindow::eventFilter("));
    QVERIFY2(avatar_click.contains(QStringLiteral("context_avatar_")) &&
                 avatar_click.contains(QStringLiteral("ContextType::Private")) &&
                 avatar_click.contains(QStringLiteral("openProfile(current_peer_id_")),
             "only a private-chat avatar opens the peer profile");
    const QString room_section =
        functionSection(source, QStringLiteral("void MainWindow::selectRoom("));
    const QString private_section =
        functionSection(source, QStringLiteral("void MainWindow::selectPrivate("));
    const QString reset_section =
        functionSection(source, QStringLiteral("void MainWindow::resetContext("));
    QVERIFY2(room_section.contains(QStringLiteral("load_more_action_->setEnabled(false)")) &&
                 private_section.contains(
                     QStringLiteral("load_more_action_->setEnabled(false)")) &&
                 reset_section.contains(
                     QStringLiteral("load_more_action_->setEnabled(false)")),
             "every context switch must disable load-more until history says otherwise");
    const QString search_section =
        functionSection(source, QStringLiteral("void MainWindow::openMessageSearch("));
    QVERIFY2(search_section.contains(QStringLiteral("ContextType::None")),
             "search must refuse to open without a selected conversation");
}

// 系统弹窗是独立开关：关掉后仍保留任务栏闪烁，但不再调用托盘气泡。
void TestClientCore::systemPopupNotificationIsOptional() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY2(source.contains(QStringLiteral("settings/system_popup")),
             "the system-popup preference must be persisted");
    const QString private_notify = functionSection(
        source, QStringLiteral("void MainWindow::showPrivateNotification("));
    QVERIFY2(private_notify.contains(QStringLiteral("system_popup_enabled_")),
             "private popups must honour the system-popup switch");
    const QString mention_notify = functionSection(
        source, QStringLiteral("void MainWindow::showMentionNotification("));
    QVERIFY2(mention_notify.contains(QStringLiteral("system_popup_enabled_")),
             "mention popups must honour the system-popup switch");
    QVERIFY2(source.contains(
                 QStringLiteral("notifications_enabled_ && system_popup_enabled_ && tray_ != nullptr")),
             "friend-request popups must honour both switches");
    const QString dialog = readClientSource(QStringLiteral("ui/settings_dialog.cpp"));
    QVERIFY2(dialog.contains(QStringLiteral("系统弹窗通知")),
             "the settings dialog exposes the system-popup switch");
}

// 路径可能含空格，命令行必须整体加引号；--minimized 让开机启动静默驻留托盘。
void TestClientCore::autostartCommandCarriesMinimizedFlag() {
    const QString exe = QStringLiteral("C:/Program Files/ChatBox/chatbox.exe");
    const QString expected =
        QStringLiteral("\"%1\" --minimized").arg(QDir::toNativeSeparators(exe));
    QCOMPARE(chatbox::client::autostartCommand(exe), expected);
}

void TestClientCore::autostartSettingWritesAndRemovesRunValue() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QSettings run_key(dir.filePath(QStringLiteral("run.ini")), QSettings::IniFormat);
    const QString exe = QStringLiteral("C:/Apps/ChatBox/chatbox.exe");
    chatbox::client::applyAutostartSetting(run_key, true, exe);
    QCOMPARE(run_key.value(QStringLiteral("ChatBox")).toString(),
             chatbox::client::autostartCommand(exe));
    chatbox::client::applyAutostartSetting(run_key, false, exe);
    QVERIFY(!run_key.contains(QStringLiteral("ChatBox")));
}

// 开机启动静默进托盘；没有托盘时必须正常显示窗口，不能变成不可见进程。
// 第二实例直接退出；崩溃遗留的锁允许过期。
void TestClientCore::startupHonoursMinimizedAndSingleInstance() {
    const QString source = readClientSource(QStringLiteral("main.cpp"));
    QVERIFY2(!source.isEmpty(), "main.cpp must be readable");
    QVERIFY2(source.contains(QStringLiteral("--minimized")),
             "startup must understand the autostart flag");
    QVERIFY2(source.contains(QStringLiteral("shouldStartHidden(")),
             "startup must use the testable visibility policy");
    QVERIFY2(!source.contains(QStringLiteral("QSystemTrayIcon::isSystemTrayAvailable()")),
             "Qt's Windows tray probe is a constant; the shell must be checked instead");
    QVERIFY2(source.contains(QStringLiteral("QLockFile")),
             "a second instance must not run concurrently");
    QVERIFY2(source.contains(QStringLiteral("setStaleLockTime")),
             "a crashed instance must not block startup forever");
    QVERIFY2(source.contains(QStringLiteral("setHighDpiScaleFactorRoundingPolicy")),
             "fractional Windows scaling (125%/150%) must not round up to 200%");
    QVERIFY2(source.contains(QStringLiteral("PassThrough")),
             "the high-DPI policy must preserve the fractional scale factor");
    const QString window = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(!window.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY2(window.contains(QStringLiteral("availableGeometry")),
             "the initial window size must be capped to the usable screen");
}

// Acceptance testing needs two clients for two accounts. A separate profile
// gets its own lock and INI settings without changing the production default.
void TestClientCore::startupSupportsIsolatedProfile() {
    const QString source = readClientSource(QStringLiteral("main.cpp"));
    QVERIFY2(!source.isEmpty(), "main.cpp must be readable");
    QVERIFY(source.contains(QStringLiteral("--profile")));
    QVERIFY(source.contains(QStringLiteral("QSettings::setDefaultFormat(QSettings::IniFormat)")));
    QVERIFY(source.contains(QStringLiteral("QSettings::setPath(")));
    QVERIFY(source.contains(QStringLiteral("profile_lock")));
}

// Qt's Windows platform plugin reports a system tray unconditionally, so the
// shell check is separate and the visibility decision is a pure policy.
void TestClientCore::startupPolicyRequiresUsableTray() {
    QVERIFY(chatbox::client::shouldStartHidden(true, true));
    QVERIFY(!chatbox::client::shouldStartHidden(true, false));
    QVERIFY(!chatbox::client::shouldStartHidden(false, true));
    QVERIFY(!chatbox::client::shouldStartHidden(false, false));
}

// Turning the alert master switch off must also hide unread badges and the
// window-title counter, not only the taskbar flash and popups.
void TestClientCore::unreadBadgesHonourAlertsSwitch() {
    QCOMPARE(chatbox::client::displayedUnread(true, 5), 5);
    QCOMPARE(chatbox::client::displayedUnread(false, 5), 0);
    QCOMPARE(chatbox::client::displayedUnread(true, -1), 0);
    const QString source = readMainWindowSource();
    const QString refresh = functionSection(
        source, QStringLiteral("void MainWindow::refreshFriendListView("));
    QVERIFY2(refresh.contains(QStringLiteral("displayedUnread(notifications_enabled_")),
             "the conversation list must route unread counts through the policy");
}

// Opening a fresh directory must create the schema described in the design
// spec, and repeating the call for the same account must be idempotent.
void TestClientCore::historyStoreCreatesSchema() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY2(store.open(dir.path(), 1101, &error), qPrintable(error));
    QVERIFY(store.isOpen());
    QCOMPARE(store.userId(), quint64(1101));
    QCOMPARE(store.directory(), QDir(dir.path()).absolutePath());
    QVERIFY(store.sizeBytes() > 0);

    {
        QSqlDatabase db = QSqlDatabase::database(QStringLiteral("history_1101"), false);
        QVERIFY(db.isOpen());
        QVERIFY(db.tables().contains(QStringLiteral("messages")));
        QVERIFY(db.tables().contains(QStringLiteral("conversations")));
        QSqlQuery version(db);
        QVERIFY(version.exec(QStringLiteral("PRAGMA user_version")) && version.next());
        QCOMPARE(version.value(0).toInt(), 1);
        QSqlQuery indexes(db);
        QVERIFY(indexes.exec(QStringLiteral("PRAGMA index_list(messages)")));
        QStringList index_names;
        while (indexes.next()) index_names << indexes.value(1).toString();
        QVERIFY(index_names.contains(QStringLiteral("uk_msg_server")));
        QVERIFY(index_names.contains(QStringLiteral("idx_msg_page")));
    }

    QVERIFY(store.open(dir.path(), 1101, &error));  // 幂等
    store.close();
    QVERIFY(!store.isOpen());
}

// A path that is a regular file can never host the database, so opening must
// fail with a Chinese message instead of silently succeeding.
void TestClientCore::historyStoreReportsOpenFailure() {
    QTemporaryDir dir;
    QFile blocker(dir.filePath(QStringLiteral("not-a-dir")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY(!store.open(blocker.fileName(), 1102, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.isOpen());
}

// Two live stores must not share one connection name: the second open has to
// fail instead of stealing the first store's connection.
void TestClientCore::historyStoreRejectsSecondInstance() {
    QTemporaryDir dir;
    QString error;
    chatbox::client::HistoryStore first;
    QVERIFY(first.open(dir.path(), 1103, &error));
    chatbox::client::HistoryStore second;
    QVERIFY(!second.open(dir.path(), 1103, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(first.isOpen());
    QCOMPARE(first.directory(), QDir(dir.path()).absolutePath());
    first.close();
    QVERIFY2(second.open(dir.path(), 1103, &error), qPrintable(error));
    second.close();
}

// Confirmed messages must round-trip through the local store: the newest
// page comes back in ascending order and the older page is reachable with
// the pagination cursor.
void TestClientCore::historyStoreSaveAndLoadRecent() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1201, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "one", "2026-10-20 10:00:00")));
    QVERIFY(store.saveMessage(confirmedMessage(2, 7, "two", "2026-10-20 10:01:00")));
    QVERIFY(store.saveMessage(confirmedMessage(3, 7, "three", "2026-10-20 10:02:00")));
    const auto last = store.recent(chatbox::client::ChatContext::kRoom, 7, 2);
    QCOMPARE(last.size(), 2);
    QCOMPARE(last.at(0).message_id, quint64(2));
    QCOMPARE(last.at(1).message_id, quint64(3));
    const auto older = store.before(chatbox::client::ChatContext::kRoom, 7, 2, 5);
    QCOMPARE(older.size(), 1);
    QCOMPARE(older.at(0).message_id, quint64(1));
    QVERIFY(store.before(chatbox::client::ChatContext::kRoom, 7, 1, 5).isEmpty());
}

// The same server message must never be stored twice; a repeated write
// updates the existing row in place.
void TestClientCore::historyStoreDedupesByMessageId() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1202, &error));
    QVERIFY(store.saveMessage(confirmedMessage(5, 7, "hello")));
    QVERIFY(store.saveMessage(confirmedMessage(5, 7, "edited")));
    QCOMPARE(store.messageCount(), qint64(1));
    QCOMPARE(store.recent(chatbox::client::ChatContext::kRoom, 7, 10).at(0).content,
             QString("edited"));
}

// Unconfirmed (still pending) messages stay in memory only: they must not
// reach the local database.
void TestClientCore::historyStoreSkipsUnconfirmedMessages() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1203, &error));
    auto pending = confirmedMessage(0, 7, "not confirmed");
    pending.status = chatbox::client::MessageStatus::kSending;
    QVERIFY(!store.saveMessage(pending));
    QCOMPARE(store.messageCount(), qint64(0));
    QVERIFY(!store.hasMessages());
}

// A recall wipes the body from the local copy as well.
void TestClientCore::historyStoreMarksRecalled() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1204, &error));
    QVERIFY(store.saveMessage(confirmedMessage(7, 7, "to be recalled")));
    QVERIFY(store.markRecalled(chatbox::client::ChatContext::kRoom, 7, 7));
    const auto rows = store.recent(chatbox::client::ChatContext::kRoom, 7, 10);
    QCOMPARE(rows.size(), 1);
    QVERIFY(rows.at(0).recalled);
    QVERIFY(rows.at(0).content.isEmpty());
    QVERIFY(!store.markRecalled(chatbox::client::ChatContext::kRoom, 7, 999));

    // 撤回后会话摘要必须跟着清空：侧栏不能再显示已撤回的正文。
    const auto summaries = store.conversations();
    QCOMPARE(summaries.size(), 1);
    QVERIFY(summaries.at(0).last_content.isEmpty());
}

// 撤回必须按会话定位：房间与私聊各自独立编号，
// 私聊里撤回 message_id=7 不能把房间里同号的消息一起清空。
void TestClientCore::historyStoreRecallIsConversationScoped() {
    QTemporaryDir dir;
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY(store.open(dir.path(), 1208, &error));
    QVERIFY(store.saveMessage(confirmedMessage(7, 7, "room text")));
    chatbox::client::ChatMessage private_message = confirmedMessage(7, 8, "private text");
    private_message.context = chatbox::client::ChatContext::kPrivate;
    QVERIFY(store.saveMessage(private_message));
    QVERIFY(store.markRecalled(chatbox::client::ChatContext::kPrivate, 8, 7));
    const auto room = store.recent(chatbox::client::ChatContext::kRoom, 7, 10);
    const auto priv = store.recent(chatbox::client::ChatContext::kPrivate, 8, 10);
    QCOMPARE(room.size(), 1);
    QCOMPARE(priv.size(), 1);
    QVERIFY(!room.at(0).recalled);
    QCOMPARE(room.at(0).content, QStringLiteral("room text"));
    QVERIFY(priv.at(0).recalled);
    QVERIFY(priv.at(0).content.isEmpty());
}

// Two accounts share one database file but must never see each other's rows.
void TestClientCore::historyStoreIsolatesUsers() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1205, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "mine")));
    QVERIFY(store.open(dir.path(), 1206, &error));
    QCOMPARE(store.messageCount(), qint64(0));
    QVERIFY(store.recent(chatbox::client::ChatContext::kRoom, 7, 10).isEmpty());
    QVERIFY(store.open(dir.path(), 1205, &error));
    QCOMPARE(store.messageCount(), qint64(1));
}

// A room and a private chat are separate conversations even when they use
// the same numeric target id.
void TestClientCore::historyStoreIsolatesConversations() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1207, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "room message")));
    auto priv = confirmedMessage(2, 8, "private message");
    priv.context = chatbox::client::ChatContext::kPrivate;
    QVERIFY(store.saveMessage(priv));
    QCOMPARE(store.recent(chatbox::client::ChatContext::kRoom, 7, 10).size(), 1);
    QCOMPARE(store.recent(chatbox::client::ChatContext::kPrivate, 8, 10).size(), 1);
    QVERIFY(store.recent(chatbox::client::ChatContext::kPrivate, 7, 10).isEmpty());
}

// Local search is scoped to the current conversation and must treat the
// keyword literally: % and _ are not wildcards.
void TestClientCore::historyStoreSearchFindsInConversation() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1208, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "hello 100% done")));
    QVERIFY(store.saveMessage(confirmedMessage(2, 7, "hello world")));
    auto priv = confirmedMessage(3, 8, "hello private");
    priv.context = chatbox::client::ChatContext::kPrivate;
    QVERIFY(store.saveMessage(priv));

    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "hello", 50).size(), 2);
    const auto priv_hits = store.search(chatbox::client::ChatContext::kPrivate, 8, "hello", 50);
    QCOMPARE(priv_hits.size(), 1);
    QCOMPARE(priv_hits.at(0).message_id, quint64(3));
    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "100%", 50).size(), 1);
    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "%", 50).size(), 1);  // 只中 100%
    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "_", 50).size(), 0);  // 下划线被转义
    QCOMPARE(store.search(chatbox::client::ChatContext::kRoom, 7, "hello", 1).size(), 1);
}

// 会话摘要：saveConversation 只保证会话行存在并写标题，last_* 由消息写入推进；
// 更旧的一页历史不得把 last_* 往回改。
void TestClientCore::historyStoreConversationSummaries() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1301, &error));
    chatbox::client::ConversationRow row;
    row.context = chatbox::client::ChatContext::kRoom;
    row.target_id = 7;
    row.title = QStringLiteral("大厅");
    QVERIFY(store.saveConversation(row));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "one", "2026-10-20 10:00:00")));
    QVERIFY(store.saveMessage(confirmedMessage(2, 7, "two", "2026-10-20 10:01:00")));
    // 更旧的消息不得把 last_* 往回改
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "one edited", "2026-10-20 09:00:00")));
    auto rows = store.conversations();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).title, QStringLiteral("大厅"));
    QCOMPARE(rows.at(0).last_message_id, quint64(2));
    QCOMPARE(rows.at(0).last_content, QString("two"));
}

// 清理：clearOlderThan 只删当前账号的过期消息并重算会话摘要（空会话删除），
// clearAll 清空当前账号的全部本地记录。
void TestClientCore::historyStoreClearAllAndOlderThan() {
    QTemporaryDir dir; chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(dir.path(), 1302, &error));
    const QString old_time = QDateTime::currentDateTimeUtc().addDays(-40)
                                 .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "old room", old_time)));
    QVERIFY(store.saveMessage(confirmedMessage(2, 7, "new room",
                                              QDateTime::currentDateTimeUtc()
                                                  .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))));
    auto priv = confirmedMessage(3, 8, "old private", old_time);
    priv.context = chatbox::client::ChatContext::kPrivate;
    QVERIFY(store.saveMessage(priv));

    QVERIFY(store.clearOlderThan(30));
    auto rows = store.conversations();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.at(0).context, chatbox::client::ChatContext::kRoom);
    QCOMPARE(rows.at(0).last_message_id, quint64(2));

    QVERIFY(store.clearAll());
    QCOMPARE(store.messageCount(), qint64(0));
    QVERIFY(store.conversations().isEmpty());
}

// 迁移目录：migrate=true 把现有记录复制到新目录，migrate=false 在新目录建空库。
void TestClientCore::historyStorePathMoveCopiesData() {
    QTemporaryDir first; QTemporaryDir second; QTemporaryDir third;
    chatbox::client::HistoryStore store; QString error;
    QVERIFY(store.open(first.path(), 1303, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "keep me")));
    QCOMPARE(store.messageCount(), qint64(1));

    QVERIFY2(store.moveTo(second.path(), true, &error), qPrintable(error));
    QVERIFY(store.isOpen());
    QCOMPARE(store.directory(), QDir(second.path()).absolutePath());
    QCOMPARE(store.messageCount(), qint64(1));
    QVERIFY(QFile::exists(QDir(first.path()).filePath(QStringLiteral("history.sqlite"))));

    QVERIFY2(store.moveTo(third.path(), false, &error), qPrintable(error));
    QCOMPARE(store.messageCount(), qint64(0));
    QVERIFY(store.recent(chatbox::client::ChatContext::kRoom, 7, 10).isEmpty());
}

// 目标目录已有本地库时必须拒绝迁移，并保持当前连接在旧目录上打开。
void TestClientCore::historyStoreRejectsExistingTarget() {
    QTemporaryDir source; QTemporaryDir target;
    QString error;
    {
        chatbox::client::HistoryStore other;
        QVERIFY(other.open(target.path(), 1304, &error));
    }
    chatbox::client::HistoryStore store;
    QVERIFY(store.open(source.path(), 1305, &error));
    QVERIFY(store.saveMessage(confirmedMessage(1, 7, "keep")));
    QVERIFY(!store.moveTo(target.path(), true, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(store.isOpen());
    QCOMPARE(store.directory(), QDir(source.path()).absolutePath());
    QCOMPARE(store.messageCount(), qint64(1));

    QFile blocker(source.filePath(QStringLiteral("blocker")));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QVERIFY(!chatbox::client::HistoryStore::ensureWritableDirectory(
        blocker.fileName() + QStringLiteral("/sub"), &error));
    QVERIFY(chatbox::client::HistoryStore::ensureWritableDirectory(
        source.filePath(QStringLiteral("writable")), &error));
}

// The portable package must ship the Qt SQL module and the SQLite driver
// plugin, otherwise the local history store cannot open its database on a
// machine without a Qt installation.
void TestClientCore::buildScriptShipsSqlDriver() {
    const QString build = readClientSource(QStringLiteral("build_client.ps1"));
    QVERIFY2(!build.isEmpty(), "build_client.ps1 must be readable");
    QVERIFY(build.contains(QStringLiteral("Qt5Sql.dll")));
    QVERIFY(build.contains(QStringLiteral("sqldrivers")));
    QVERIFY(build.contains(QStringLiteral("qsqlite.dll")));

    const QString package = readClientSource(QStringLiteral("package_client.ps1"));
    QVERIFY2(!package.isEmpty(), "package_client.ps1 must be readable");
    QVERIFY(package.contains(QStringLiteral("\"imageformats\"")));
    QVERIFY(package.contains(QStringLiteral("\"sqldrivers\"")));
}

// 打开会话时先用本地历史首屏渲染，再由在线历史覆盖：
// 两个选择入口都必须先 loadInitialHistory()，
// 预渲染本身只读本地库并把结果交给 replaceHistory()。
void TestClientCore::conversationOpensFromLocalHistoryFirst() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    const QString room = functionSection(source, QStringLiteral("void MainWindow::selectRoom("));
    const QString priv = functionSection(source, QStringLiteral("void MainWindow::selectPrivate("));
    QVERIFY(room.contains(QStringLiteral("loadInitialHistory(")));
    QVERIFY(priv.contains(QStringLiteral("loadInitialHistory(")));
    const QString load = functionSection(source, QStringLiteral("void MainWindow::loadInitialHistory("));
    QVERIFY(load.contains(QStringLiteral("history_store_.recent(")));
    QVERIFY(load.contains(QStringLiteral("replaceHistory(")));
}

// 后台会话收到的推送同样要落本地库（不论是不是当前会话），
// 私聊推送还要补一条会话摘要，便于离线侧栏显示。
void TestClientCore::incomingPushesArePersistedForOtherConversations() {
    const QString source = readMainWindowSource();
    const QString room_push = caseSection(source, QStringLiteral("case chatbox::kCmdChatPush:"));
    const QString private_push = caseSection(source, QStringLiteral("case chatbox::kCmdPrivatePush:"));
    QVERIFY(room_push.contains(QStringLiteral("history_store_.saveMessage(")));
    QVERIFY(private_push.contains(QStringLiteral("history_store_.saveMessage(")));
    QVERIFY(private_push.contains(QStringLiteral("saveConversation(")));
}

// 后台会话的撤回推送同样要落到本地历史库：
// 否则本地副本里还留着用户已经撤回的正文。
void TestClientCore::recallPushUpdatesBackgroundHistory() {
    const QString source = readMainWindowSource();
    const QString recall = caseSection(source, QStringLiteral("case chatbox::kCmdMessageRecall:"));
    QVERIFY(!recall.isEmpty());
    QVERIFY(recall.contains(QStringLiteral("history_store_.markRecalled(")));
    QVERIFY(recall.contains(QStringLiteral("ChatContext::kRoom")));
    QVERIFY(recall.contains(QStringLiteral("ChatContext::kPrivate")));
}

// 登录页不再保留「查看本地记录」行：无网络时由主窗口自动进入只读离线模式。
void TestClientCore::loginPageHasNoLocalHistoryEntry() {
    LoginPage page;
    QVERIFY(page.findChild<QPushButton*>(QStringLiteral("LocalHistoryButton")) == nullptr);
}

// 服务器/端口/TLS 不是每次登录都要填：默认收在登录卡片的横向三点入口里，
// 新安装默认走公网，展开后才显示可编辑字段。
void TestClientCore::loginPageHidesServerFieldsBehindMoreDots() {
    QSettings settings;
    settings.remove(QStringLiteral("server"));
    settings.sync();

    LoginPage page;
    auto* toggle = page.findChild<QToolButton*>(QStringLiteral("AdvancedOptionsButton"));
    auto* panel = page.findChild<QWidget*>(QStringLiteral("AdvancedOptionsPanel"));
    QVERIFY(toggle != nullptr);
    QVERIFY(panel != nullptr);
    QVERIFY(toggle->text().isEmpty());
    QVERIFY(!toggle->icon().isNull());
    QCOMPARE(toggle->iconSize(), QSize(20, 20));
    QCOMPARE(toggle->width(), 32);
    QCOMPARE(toggle->height(), 32);
    QVERIFY(panel->isHidden());

    auto* host = page.findChild<QLineEdit*>(QStringLiteral("ServerHostEdit"));
    auto* port = page.findChild<QSpinBox*>(QStringLiteral("ServerPortSpin"));
    auto* tls = page.findChild<QCheckBox*>(QStringLiteral("ServerTlsCheck"));
    QVERIFY(host != nullptr);
    QVERIFY(port != nullptr);
    QVERIFY(tls != nullptr);
    QCOMPARE(host->text(), QStringLiteral("121.5.168.189"));
    QCOMPARE(port->value(), 9443);
    QVERIFY(tls->isChecked());

    toggle->click();
    QVERIFY(!panel->isHidden());
    toggle->click();
    QVERIFY(panel->isHidden());
}

void TestClientCore::layoutWeightsFollowAudit() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY(!source.contains(QStringLiteral("room_list_->setFixedHeight(58)")));
    QVERIFY(source.contains(QStringLiteral("room_list_->setMinimumHeight(56)")));
    QVERIFY(source.contains(QStringLiteral("room_list_->setMaximumHeight(160)")));
    QVERIFY(source.contains(QStringLiteral("QSizePolicy::Maximum")));
    QVERIFY(source.contains(QStringLiteral("friend_requests_badge_")));
    QVERIFY(source.contains(QStringLiteral("FriendRequestBadge")));
    QVERIFY(source.contains(QStringLiteral("member_count_label_")));
    QVERIFY(source.contains(QStringLiteral("confirmDanger")));
    QVERIFY(source.contains(QStringLiteral("more_button_->setFixedSize(32, 32)")));
    // 好友申请从独立一行收进「好友」标题行，用图标按钮 + 角标；
    // 文字信息改由 tooltip / accessibleName 承载，未读数仍然只走角标。
    QVERIFY(source.contains(QStringLiteral("friend_requests_button_->setAccessibleName(\"好友申请\")")));
    QVERIFY(source.contains(QStringLiteral("friend_requests_button_->setToolTip(\"好友申请\")")));
}

void TestClientCore::adaptiveLayoutUsesSplitter() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY(source.contains(QStringLiteral("setMinimumSize(qMin(760, available.width())")));
    QVERIFY(source.contains(QStringLiteral("QSplitter(Qt::Horizontal")));
    QVERIFY(source.contains(QStringLiteral("setChildrenCollapsible(false)")));
    QVERIFY(source.contains(QStringLiteral("setHandleWidth(0)")));
    QVERIFY(source.contains(QStringLiteral("setMinimumWidth(240)")));
    QVERIFY(source.contains(QStringLiteral("setMaximumWidth(360)")));
    QVERIFY(source.contains(QStringLiteral("setMinimumWidth(180)")));
    QVERIFY(source.contains(QStringLiteral("setMaximumWidth(280)")));
    QVERIFY(source.contains(QStringLiteral("setStretchFactor(0, 0)")));
    QVERIFY(source.contains(QStringLiteral("setStretchFactor(1, 1)")));
    QVERIFY(source.contains(QStringLiteral("setStretchFactor(2, 0)")));
}

void TestClientCore::textNeverSmallerThanElevenPixels() {
    const QString qss = readClientSource(QStringLiteral("resources/style.qss"));
    QVERIFY2(!qss.isEmpty(), "resources/style.qss must be readable");
    QVERIFY(!qss.contains(QStringLiteral("font-size: 10px")));
    const QString delegate =
        readClientSource(QStringLiteral("ui/friend_item_delegate.cpp"));
    QVERIFY2(!delegate.isEmpty(), "ui/friend_item_delegate.cpp must be readable");
    QVERIFY(!delegate.contains(QStringLiteral("setPixelSize(10)")));
}

void TestClientCore::emptyStateShowsConfiguredCopy() {
    EmptyState state;
    state.setIcon(QIcon(QStringLiteral(":/icons/message.png")));
    state.setTitle(QStringLiteral("还没有消息"));
    state.setDescription(QStringLiteral("发送第一条消息，打个招呼吧"));
    state.setAction(QStringLiteral("开始"));

    auto* icon = state.findChild<QLabel*>(QStringLiteral("EmptyStateIcon"));
    auto* title = state.findChild<QLabel*>(QStringLiteral("EmptyStateTitle"));
    auto* description =
        state.findChild<QLabel*>(QStringLiteral("EmptyStateDescription"));
    auto* action =
        state.findChild<QPushButton*>(QStringLiteral("EmptyStateAction"));
    QVERIFY(icon != nullptr);
    QVERIFY(title != nullptr);
    QVERIFY(description != nullptr);
    QVERIFY(action != nullptr);
    QVERIFY(icon->pixmap() != nullptr);
    QCOMPARE(title->text(), QStringLiteral("还没有消息"));
    QCOMPARE(description->text(), QStringLiteral("发送第一条消息，打个招呼吧"));
    QCOMPARE(action->text(), QStringLiteral("开始"));
    QVERIFY(!action->isHidden());
}

void TestClientCore::emptyStatesAreWired() {
    const QString window = readMainWindowSource();
    QVERIFY2(!window.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY(window.contains(QStringLiteral("chat_empty_state_")));
    QVERIFY(window.contains(QStringLiteral("friends_empty_state_")));
    QVERIFY(window.contains(QStringLiteral("还没有消息")));
    QVERIFY(window.contains(QStringLiteral("还没有好友")));

    const QString search =
        readClientSource(QStringLiteral("ui/user_search_dialog.cpp"));
    QVERIFY2(!search.isEmpty(), "ui/user_search_dialog.cpp must be readable");
    QVERIFY(search.contains(QStringLiteral("EmptyState")));
    QVERIFY(search.contains(QStringLiteral("没有找到「")));
}

class ExposedMessageInput : public MessageInput {
public:
    using MessageInput::insertFromMimeData;
};

void TestClientCore::messageInputHandlesKeyboardAndPlainPaste() {
    MessageInput input;
    QCOMPARE(input.frameShape(), QFrame::NoFrame);
    input.resize(400, 60);
    input.show();
    QTest::qWait(30);
    QSignalSpy submitted(&input, &MessageInput::submitRequested);

    QTest::keyClicks(&input, "hello");
    QTest::keyClick(&input, Qt::Key_Return);
    QCOMPARE(submitted.count(), 1);

    input.clear();
    QTest::keyClicks(&input, "line");
    QTest::keyClick(&input, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(submitted.count(), 1);
    QVERIFY(input.toPlainText().contains('\n'));

    input.clear();
    QTest::keyClick(&input, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(submitted.count(), 2);

    QMimeData mime;
    mime.setHtml(QStringLiteral("<b>pasted</b>"));
    mime.setText(QStringLiteral("pasted"));
    ExposedMessageInput pasted;
    pasted.insertFromMimeData(&mime);
    QCOMPARE(pasted.toPlainText(), QStringLiteral("pasted"));

    input.clear();
    input.setPlainText(QString(1000, QChar('x')));
    QTest::qWait(20);
    QVERIFY(input.height() >= 40);
    QVERIFY(input.height() <= 120);
}

void TestClientCore::messageInputHidesEmptyScrollbar() {
    QFile style(QStringLiteral(":/style.qss"));
    QVERIFY(style.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString previous_style = qApp->styleSheet();
    qApp->setStyleSheet(QString::fromUtf8(style.readAll()));

    MessageInput input;
    input.setPlaceholderText("输入消息，回车发送，Shift + 回车换行");
    input.resize(446, 40);
    input.show();
    input.setFocus();
    QTest::qWait(100);
    const bool fits =
        input.document()->size().height() <= input.viewport()->height();
    const bool visible = input.verticalScrollBar()->isVisible();
    qApp->setStyleSheet(previous_style);
    QVERIFY2(fits,
             "empty composer document must fit inside the padded viewport");
    QVERIFY2(!visible,
             "an empty composer must not paint a scrollbar handle on its right edge");
}

void TestClientCore::messageComposerUsesByteCounter() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY(source.contains(QStringLiteral("MessageInput")));
    QVERIFY(source.contains(QStringLiteral("MessageByteCounter")));
    QVERIFY(source.contains(QStringLiteral("toPlainText()")));
    QVERIFY(source.contains(QStringLiteral("2000")));
    QVERIFY(source.contains(QStringLiteral("1800")));
    QVERIFY(source.contains(QStringLiteral("已用 %1/2000 字节")));
}

void TestClientCore::dangerousActionsUseChineseDialogs() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY(source.contains(QStringLiteral("confirmDanger")));
    QVERIFY(source.contains(QStringLiteral("撤回消息")));
    QVERIFY(source.contains(QStringLiteral("退出登录")));
    QVERIFY(!source.contains(QStringLiteral("QMessageBox::question")));
}

// 昵称只在注册时出现：登录态点“注册”进入注册态，
// 主按钮变“注册”，次按钮变“返回登录”；返回后昵称隐藏。
void TestClientCore::loginPageShowsNicknameOnlyInRegisterMode() {
    LoginPage page;
    auto* nickname = page.findChild<QLineEdit*>(QStringLiteral("NicknameEdit"));
    auto* login = page.findChild<QPushButton*>(QStringLiteral("PrimaryButton"));
    auto* register_button = page.findChild<QPushButton*>(QStringLiteral("RegisterButton"));
    QVERIFY(nickname != nullptr);
    QVERIFY(login != nullptr);
    QVERIFY(register_button != nullptr);
    QVERIFY(nickname->isHidden());
    QCOMPARE(login->text(), QStringLiteral("登录"));
    QCOMPARE(register_button->text(), QStringLiteral("注册"));

    register_button->click();
    QVERIFY(!nickname->isHidden());
    QCOMPARE(login->text(), QStringLiteral("注册"));
    QCOMPARE(register_button->text(), QStringLiteral("返回登录"));

    register_button->click();
    QVERIFY(nickname->isHidden());
    QCOMPARE(login->text(), QStringLiteral("登录"));
    QCOMPARE(register_button->text(), QStringLiteral("注册"));
}

void TestClientCore::loginPageHasAutoLoginAndRememberPasswordOptions() {
    QSettings settings;
    settings.remove(QStringLiteral("login"));
    settings.sync();

    LoginPage page;
    auto* auto_login = page.findChild<QCheckBox*>(QStringLiteral("AutoLoginCheck"));
    auto* remember = page.findChild<QCheckBox*>(QStringLiteral("RememberPasswordCheck"));
    QVERIFY(auto_login != nullptr);
    QVERIFY(remember != nullptr);
    QVERIFY(auto_login->isChecked());
    QVERIFY(!remember->isChecked());
}

void TestClientCore::loginPageAccessibleNames() {
    LoginPage page;
    auto* host = page.findChild<QLineEdit*>(QStringLiteral("ServerHostEdit"));
    auto* port = page.findChild<QSpinBox*>(QStringLiteral("ServerPortSpin"));
    auto* username = page.findChild<QLineEdit*>(QStringLiteral("UsernameEdit"));
    auto* password = page.findChild<QLineEdit*>(QStringLiteral("PasswordEdit"));
    auto* nickname = page.findChild<QLineEdit*>(QStringLiteral("NicknameEdit"));
    QVERIFY(host != nullptr);
    QVERIFY(port != nullptr);
    QVERIFY(username != nullptr);
    QVERIFY(password != nullptr);
    QVERIFY(nickname != nullptr);
    QCOMPARE(host->accessibleName(), QStringLiteral("服务器"));
    QCOMPARE(port->accessibleName(), QStringLiteral("端口"));
    QCOMPARE(username->accessibleName(), QStringLiteral("用户名"));
    QCOMPARE(password->accessibleName(), QStringLiteral("密码"));
    QCOMPARE(nickname->accessibleName(), QStringLiteral("昵称"));
}

void TestClientCore::loginPageClearsFieldFocusOnBlankClick() {
    LoginPage page;
    page.resize(640, 720);
    page.show();
    QTest::qWait(50);
    auto* username = page.findChild<QLineEdit*>(QStringLiteral("UsernameEdit"));
    QVERIFY(username != nullptr);
    username->setFocus();
    QTest::qWait(20);
    QVERIFY(username->hasFocus());

    QTest::mouseClick(&page, Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    QTest::qWait(20);
    QVERIFY(!username->hasFocus());
}

void TestClientCore::loginPageReloadsRememberedPasswordAfterLogout() {
    QSettings settings;
    settings.remove(QStringLiteral("login"));
    settings.sync();
    settings.setValue(QStringLiteral("login/remember_password"), true);
    settings.sync();
    QVERIFY(chatbox::client::saveSavedCredentials(QStringLiteral("alice"),
                                                   QStringLiteral("Secret123")));

    LoginPage page;
    auto* username = page.findChild<QLineEdit*>(QStringLiteral("UsernameEdit"));
    auto* password = page.findChild<QLineEdit*>(QStringLiteral("PasswordEdit"));
    QVERIFY(username != nullptr);
    QVERIFY(password != nullptr);
    QCOMPARE(username->text(), QStringLiteral("alice"));
    QCOMPARE(password->text(), QStringLiteral("Secret123"));

    page.clearPassword();
    QVERIFY(password->text().isEmpty());
    page.resetToLogin();
    QCOMPARE(username->text(), QStringLiteral("alice"));
    QCOMPARE(password->text(), QStringLiteral("Secret123"));

    chatbox::client::clearSavedCredentials();
    settings.remove(QStringLiteral("login"));
    settings.sync();
}

void TestClientCore::credentialStoreEncryptsSavedPassword() {
    QSettings settings;
    settings.remove(QStringLiteral("login"));
    settings.sync();
    if (!chatbox::client::passwordStorageSupported()) {
        QSKIP("secure password storage is only available on Windows");
    }

    QVERIFY(chatbox::client::saveSavedCredentials(QStringLiteral("alice"),
                                                   QStringLiteral("Secret123")));
    QSettings stored;
    const QString encoded = stored.value(QStringLiteral("login/password")).toString();
    QVERIFY(!encoded.isEmpty());
    QVERIFY(!encoded.contains(QStringLiteral("Secret123")));

    QString username;
    QString password;
    QVERIFY(chatbox::client::loadSavedCredentials(&username, &password));
    QCOMPARE(username, QStringLiteral("alice"));
    QCOMPARE(password, QStringLiteral("Secret123"));

    chatbox::client::clearSavedCredentials();
    QVERIFY(!chatbox::client::loadSavedCredentials(&username, &password));
}

void TestClientCore::credentialStoreForgetsAccountSecret() {
    if (!chatbox::client::passwordStorageSupported()) {
        QSKIP("secure password storage is only available on Windows");
    }
    const QString username =
        QStringLiteral("qa_forget_%1").arg(QCoreApplication::applicationPid());
    chatbox::client::AccountRecord record;
    record.username = username;
    record.nickname = QStringLiteral("QA Forget");
    record.user_id = 999999;
    chatbox::client::upsertAccount(record);
    QVERIFY(chatbox::client::saveAccountCredentials(username,
                                                     QStringLiteral("Secret123")));

    QString password;
    QVERIFY(chatbox::client::loadAccountCredentials(username, &password));
    chatbox::client::forgetAccountCredentials(username);
    QVERIFY(!chatbox::client::loadAccountCredentials(username, &password));

    // Forgetting the password must not erase the switcher's display record.
    bool record_remains = false;
    for (const chatbox::client::AccountRecord& candidate :
         chatbox::client::loadAccounts()) {
        if (candidate.username == username) {
            record_remains = true;
        }
    }
    QVERIFY(record_remains);
    chatbox::client::forgetAccountRecord(username);
}

void TestClientCore::mainWindowUsesIncrementalAppendForIncomingMessages() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    const QString add = functionSection(
        source, QStringLiteral("void MainWindow::addIncomingMessage("));
    QVERIFY(add.contains(QStringLiteral("renderMessageFragment(")));
    QVERIFY(add.contains(QStringLiteral("appendFragment(")));
    QVERIFY(add.contains(QStringLiteral("formatLocalDate(")));
    QVERIFY(add.contains(QStringLiteral("bar->value() >= bar->maximum() - 8")));
}

void TestClientCore::settingsDialogDefaultsToPublicServer() {
    QSettings settings;
    settings.remove(QStringLiteral("server"));
    settings.sync();

    SettingsDialog dialog(QStringLiteral("U00001001"), QString(), 0, false);
    auto* host = dialog.findChild<QLineEdit*>(QStringLiteral("ServerHostEdit"));
    auto* port = dialog.findChild<QSpinBox*>(QStringLiteral("ServerPortSpin"));
    auto* tls = dialog.findChild<QCheckBox*>(QStringLiteral("ServerTlsCheck"));
    auto* appearance =
        dialog.findChild<QComboBox*>(QStringLiteral("AppearanceCombo"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    QVERIFY(host != nullptr);
    QVERIFY(port != nullptr);
    QVERIFY(tls != nullptr);
    QVERIFY(appearance != nullptr);
    QVERIFY(buttons != nullptr);
    QCOMPARE(appearance->count(), 3);
    QCOMPARE(buttons->button(QDialogButtonBox::Save)->text(),
             QStringLiteral("保存"));
    QCOMPARE(buttons->button(QDialogButtonBox::Cancel)->text(),
             QStringLiteral("取消"));
    QCOMPARE(host->text(), QStringLiteral("121.5.168.189"));
    QCOMPARE(port->value(), 9443);
    QVERIFY(tls->isChecked());
}

// 离线模式必须是只读的本地视图：所有写操作都要被挡住，
// 搜索与翻页改走本地历史库；断网时自动进入，不再保留手动入口。
void TestClientCore::offlineModeIsReadOnlyAndLocal() {
    const QString source = readMainWindowSource();
    QVERIFY(source.contains(QStringLiteral("enterOfflineMode(")));
    QVERIFY(source.contains(QStringLiteral("enterOfflineMode(true)")));
    QVERIFY(source.contains(QStringLiteral("离线模式 · 仅显示本地记录")));
    QVERIFY(!source.contains(QStringLiteral("LocalHistoryButton")));
    QVERIFY(!source.contains(QStringLiteral("openLocalHistory")));
    const QString recall = functionSection(source, QStringLiteral("void MainWindow::sendRecall("));
    QVERIFY(recall.contains(QStringLiteral("!online_")));
    const QString search = functionSection(source,
        QStringLiteral("void MainWindow::requestMessageSearch("));
    QVERIFY(search.contains(QStringLiteral("offline_mode_")));
    QVERIFY(search.contains(QStringLiteral("history_store_.search(")));
    const QString room = functionSection(source, QStringLiteral("void MainWindow::selectRoom("));
    QVERIFY(room.contains(QStringLiteral("offline_mode_")));
    QVERIFY(room.contains(QStringLiteral("loadInitialHistory(")));
    const QString leave = functionSection(source, QStringLiteral("void MainWindow::leaveOfflineMode("));
    QVERIFY(leave.contains(QStringLiteral("offline_mode_ = false")));
    const QString save = functionSection(source, QStringLiteral("void MainWindow::saveSession("));
    QVERIFY(save.contains(QStringLiteral("history/last_user_id")));
    const QString clear = functionSection(source, QStringLiteral("void MainWindow::clearSession("));
    QVERIFY(!clear.contains(QStringLiteral("history/last_user_id")));
}

// 设置页要能显示本地聊天记录的存放位置、占用大小，并提供三个操作入口。
// 库不可用（缺少 SQLite 驱动）时按钮必须禁用，避免用户点了才发现没库。
void TestClientCore::settingsDialogShowsHistoryInfo() {
    QTemporaryDir dir;
    SettingsDialog dialog(QStringLiteral("U00001001"), dir.path(), 4096, true);
    auto* path = dialog.findChild<QLabel*>(QStringLiteral("HistoryPathLabel"));
    auto* size = dialog.findChild<QLabel*>(QStringLiteral("HistorySizeLabel"));
    QVERIFY(path != nullptr);
    QVERIFY(size != nullptr);
    QCOMPARE(QDir(path->text()), QDir(dir.path()));
    QCOMPARE(size->text(), QStringLiteral("4.0 KB"));
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("ChangeHistoryDirButton")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("OpenHistoryDirButton")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("ClearHistoryButton")) != nullptr);
    dialog.setHistoryInfo(dir.path(), 3 * 1024 * 1024, false);
    QCOMPARE(size->text(), QStringLiteral("3.0 MB"));
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("ClearHistoryButton"))->isEnabled());
}

// 设置页的三个历史操作都要有中文文案，并落到 MainWindow 的落盘逻辑上。
void TestClientCore::settingsDialogExposesHistoryActions() {
    const QString dialog = readClientSource(QStringLiteral("ui/settings_dialog.cpp"));
    QVERIFY(!dialog.isEmpty());
    QVERIFY(dialog.contains(QStringLiteral("更改目录")));
    QVERIFY(dialog.contains(QStringLiteral("打开目录")));
    QVERIFY(dialog.contains(QStringLiteral("清理")));
    QVERIFY(dialog.contains(QStringLiteral("historyPathChangeRequested")));
    QVERIFY(dialog.contains(QStringLiteral("historyClearAllRequested")));
    QVERIFY(dialog.contains(QStringLiteral("historyClearOlderRequested")));
    QVERIFY(dialog.contains(QStringLiteral("不影响服务器历史")));
    const QString window = readMainWindowSource();
    QVERIFY(window.contains(QStringLiteral("settings/history/path")));
    QVERIFY(window.contains(QStringLiteral("moveTo(")));
}

// 账号级操作搬到设置后，右栏不再承载导航；这里守住「设置里能退出」这一入口。
void TestClientCore::settingsDialogOffersSignOut() {
    SettingsDialog dialog(QStringLiteral("U00000001"),
                          QStringLiteral("C:/tmp/chatbox-history"), 1024, true);
    auto* sign_out =
        dialog.findChild<QPushButton*>(QStringLiteral("DangerOutlineButton"));
    QVERIFY2(sign_out != nullptr, "settings must expose a sign-out action");
    QCOMPARE(sign_out->text(), QStringLiteral("退出当前账号"));
}

// 切换账号列表必须先能构造出来（空表也要给出说明），否则账号菜单点开就崩。
void TestClientCore::accountSwitcherBuildsList() {
    AccountSwitcherDialog dialog(QStringLiteral("alice"));
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("AccountList"));
    QVERIFY2(list != nullptr, "switcher must build an account list");
    QVERIFY(list->count() >= 1);
    QVERIFY(dialog.selectedUsername().isEmpty());

    const QString source = readMainWindowSource();
    QVERIFY2(!source.contains(QStringLiteral("logout_button_")),
             "sign-out must not sit in the room member footer anymore");
    QVERIFY2(source.contains(QStringLiteral("right_panel_->setVisible")),
             "the member column is conditional on the chat context");
}

QTEST_MAIN(TestClientCore)
#include "test_client_core.moc"