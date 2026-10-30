#include <QtTest>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <QApplication>
#include <QBuffer>
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
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPixmap>
#include <QPainter>
#include <QPushButton>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTimer>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVector>

#include "core/app_settings.h"
#include "core/autostart.h"
#include "core/client_config.h"
#include "core/client_policy.h"
#include "core/account_store.h"
#include "core/credential_store.h"
#include "core/file_thumbnail_cache.h"
#include "core/focus_ring.h"
#include "core/avatar_cache.h"
#include "core/avatar_utils.h"
#include "core/contact_store.h"
#include "core/history_store.h"
#include "core/group_store.h"
#include "core/group_types.h"
#include "core/identity.h"
#include "core/message_renderer.h"
#include "core/message_store.h"
#include "core/message_time.h"
#include "core/mention_query.h"
#include "core/outbox.h"
#include "core/presence_book.h"
#include "core/ui_theme.h"
#include "core/update_installer.h"
#include "core/update_manifest.h"
#include "core/update_service.h"
#include "core/user_profile.h"
#include "ui/chat_view.h"
#include "ui/empty_state.h"
#include "ui/login_page.h"
#include "ui/message_input.h"
#include "ui/mention_popup.h"
#include "ui/message_search_dialog.h"
#include "ui/more_dots.h"
#include "ui/profile_dialog.h"
#include "ui/file_manager_dialog.h"
#include "ui/settings_dialog.h"
#include "ui/group_create_dialog.h"
#include "ui/group_join_dialog.h"
#include "ui/group_notify_dialog.h"
#include "ui/group_settings_dialog.h"
#include "ui/update_dialog.h"
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

namespace {

QString testCaPath() {
    return QCoreApplication::applicationDirPath() +
           QStringLiteral("/../../certs/ca.crt");
}

// Emits finished() synchronously from abort(), which is the Qt behavior that
// used to null-deref in UpdateService::cancel().
class AbortSignalsFinishedReply : public QNetworkReply {
public:
    explicit AbortSignalsFinishedReply(QObject* parent = nullptr)
        : QNetworkReply(parent) {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setUrl(QUrl(QStringLiteral(
            "https://121.5.168.189/updates/chatbox-0.2.5-win64.zip")));
    }
    void abort() override { emit finished(); }
    qint64 readData(char*, qint64) override { return -1; }
    qint64 bytesAvailable() const override { return 0; }
};

class AbortSignalsFinishedManager : public QNetworkAccessManager {
public:
    QNetworkReply* createRequest(Operation, const QNetworkRequest&,
                                 QIODevice*) override {
        return new AbortSignalsFinishedReply(this);
    }
};

class FixedBodyReply : public QNetworkReply {
public:
    FixedBodyReply(const QByteArray& body, QObject* parent)
        : QNetworkReply(parent), body_(body) {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setUrl(QUrl(QStringLiteral(
            "https://121.5.168.189/updates/chatbox-0.2.5-win64.zip")));
        setHeader(QNetworkRequest::ContentLengthHeader, body_.size());
        QTimer::singleShot(0, this, [this] {
            emit readyRead();
            emit finished();
        });
    }
    void abort() override { emit finished(); }
    qint64 readData(char* data, qint64 max_size) override {
        const qint64 left = body_.size() - offset_;
        const qint64 count = qMin(left, max_size);
        if (count <= 0) {
            return -1;
        }
        std::memcpy(data, body_.constData() + offset_,
                    static_cast<size_t>(count));
        offset_ += count;
        return count;
    }
    qint64 bytesAvailable() const override { return body_.size() - offset_; }
    bool isSequential() const override { return true; }

private:
    QByteArray body_;
    qint64 offset_ = 0;
};

class FixedBodyManager : public QNetworkAccessManager {
public:
    explicit FixedBodyManager(const QByteArray& body, QObject* parent = nullptr)
        : QNetworkAccessManager(parent), body_(body) {}
    QNetworkReply* createRequest(Operation, const QNetworkRequest&,
                                 QIODevice*) override {
        return new FixedBodyReply(body_, this);
    }

private:
    QByteArray body_;
};

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
    void rendererAddsCenteredTimeSeparator();
    void rendererBubbleSpecUsesBorderWithoutPerMessageTimestamp();
    void rendererSuppressesRepeatedSenderName();
    void rendererQuoteBlockUsesQuoteSurface();
    void rendererRecalledBubbleHasNoSenderName();
    void rendererFragmentMatchesFullRender();
    void rendererAlignsOwnMessages();
    void rendererQuoteBlockWithoutInlineActions();
    void rendererCapsWideBubbleWidth();
    void rendererQuoteBubbleStaysNarrow();
    void rendererShowsAvatarAndNameOnly();
    void rendererTightensConsecutiveBubbleGap();
    void rendererOwnMessageHidesNameAndId();
    void rendererHidesAvatarForConsecutiveMessages();
    void messageGroupingUsesPreviousMessage();
    void bubbleTextIsVerticallyCentered();
    void messageBubblesHaveRoundedCorners();
    void messageBubblesDrawRightEdge();
    void messageActionsAvailability();
    void messageActionsRecallWindow();
    void chatViewMapsMessagesToIds();
    void chatViewRegistersAvatarResources();
    void chatViewBubbleRectExcludesAvatar();
    void chatViewAppendsFragmentsWithoutResettingDocument();
    void chatViewAppendStaysUnderBudget();
    void chatViewOpensMessageMenuWithKeyboard();
    void chatViewStylesAppendedRetryLink();
    void chatViewStylesRetryLink();
    void messageMenuKeepsReplyAndRecall();
    void mentionTokenDetectsAtStart();
    void mentionTokenStopsAtWhitespace();
    void mentionTokenRejectsEmail();
    void mentionTokenNeedsBoundary();
    void mentionTokenAllowsAfterCjkComma();
    void mentionTokenIgnoresCursorBeforeAt();
    void mentionPopupFiltersByNameAndId();
    void mentionPopupKeyboardSelectionEmitsChosen();
    void mentionPopupAllEntryStaysFirst();
    void mentionPopupShowsEmptyState();
    void mentionButtonOpensMentionPopup();
    void typingAtOpensMentionPopup();
    void privateChatClosesMentionPopup();
    void atAllNotificationMatchesEveryone();
    void messageSearchEmitsJumpOnClick();
    void chatViewScrollsToMessageAndFlashes();
    void jumpModeSkipsLiveAppend();
    void jumpModeShowsReturnButton();
    void jumpLatestRestoresBottom();
    void switchConversationExitsJumpMode();
    void jumpRequestUsesTargetPlusOne();
    void jumpTimeoutResetsState();
    void switchConversationClearsSearchState();
    void insertMentionChoiceRecomputesToken();
    void presenceBookAppliesNewerSequence();
    void presenceBookIgnoresStaleSequence();
    void presenceBookClearsOnDisconnect();
    void presenceOverridesFriendListResponse();
    void contactStorePersistsAndUpdates();
    void avatarCenterCrop();
    void avatarCompressionLimit();
    void avatarFallback();
    void avatarCacheVersion();
    void avatarSrcFallsBackToVersionZero();
    void mainWindowRequestsAvatarForMessageSender();
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
    void brandAppIconExposesSizes();
    void roomAvatarIsBrandBadge();
    void trayIconUsesBrandIcon();
    void memberListShowsAvatarFromCache();
    void fallbackAvatarPaletteIsSingleSource();
    void appTitleUsesBrandName();
    void settingsMigrationCopiesLegacyStore();
    void settingsMigrationMovesOldServerEndpoint();
    void groupStoreFiltersAndSorts();
    void groupDialogsValidateInput();
    void sidebarHasGroupTabs();
    void groupListTitleShowsOnlyName();
    void sidebarAndMenuIconsAreDistinct();
    void rightSidebarHasRequiredPanels();
    void scrollLoadsOlderAndJumpsToSearchHit();
    void groupSettingsRespectsRoles();
    void updatePromptRestoresHiddenWindow();
    void updateManifestParsesValid();
    void updateManifestRejectsInvalid();
    void updateVersionComparison();
    void updateServiceRejectsUntrustedManifestUrl();
    void updateServiceUsesPinnedCa();
    void updateServiceCancelIsSafe();
    void updateServiceRejectsShaMismatch();
    void updateDialogHasRequiredActions();
    void updateDialogShowsCancelDuringDownload();
    void startupSchedulesOneUpdateCheck();
    void updateInstallerValidatesPackage();
    void updateInstallerExtractsZip();
    void updateMarkersRoundTrip();
    void historyDirectoryInsideInstallRejected();
    void fileSaveDirectoryValidatesAndAvoidsCollisions();
    void updateHelperHasRollbackSafety();
    void updateServerSiteUsesHttps();
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
    void closeToTrayPolicy();
    void unreadBadgesHonourAlertsSwitch();
    void historyStoreCreatesSchema();
    void historyStoreMigratesV1();
    void historyStorePendingUploadRoundTrip();
    void historyStoreFileTaskRoundTrip();
    void historyStoreFileDeleteIsConversationScoped();
    void historyStorePreservesFileMediaDimensions();
    void rendererGroupedBubblesAlign();
    void messageStoreConfirmsFileUpload();
    void rendererFileCardShowsProgress();
    void rendererShowsLargeImagePreview();
    void rendererShowsVideoPreviewWithPlayMarker();
    void rendererFallsBackToFileCardForVideoWithoutThumbnail();
    void thumbnailCacheUsesVersionedPath();
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
    void buildScriptShipsQtOpenSslForFileTransfer();
    void conversationOpensFromLocalHistoryFirst();
    void incomingPushesArePersistedForOtherConversations();
    void recallPushUpdatesBackgroundHistory();
    void loginPageHasNoLocalHistoryEntry();
    void loginPageHasNoServerSettings();
    void layoutWeightsFollowAudit();
    void adaptiveLayoutUsesSplitter();
    void textNeverSmallerThanElevenPixels();
    void emptyStateShowsConfiguredCopy();
    void emptyStatesAreWired();
    void messageInputHandlesKeyboardAndPlainPaste();
    void settingsDialogOffersSignOut();
    void settingsDialogOffersUpdateCheck();
    void settingsDialogOffersCloseToTray();
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
    void settingsDialogHasFixedServerAddress();
    void offlineModeIsReadOnlyAndLocal();
    void settingsDialogShowsHistoryInfo();
    void settingsDialogExposesHistoryActions();
    void fileManagerDialogBuilds();
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

void TestClientCore::mentionTokenDetectsAtStart() {
    const chatbox::client::MentionToken token =
        chatbox::client::mentionTokenAt(QStringLiteral("@"), 1);
    QVERIFY(token.active);
    QCOMPARE(token.start, 0);
    QVERIFY(token.query.isEmpty());
}

void TestClientCore::mentionTokenStopsAtWhitespace() {
    const QString text = QString::fromUtf8("@\u674e\u786c \u4f60\u597d");
    const chatbox::client::MentionToken token =
        chatbox::client::mentionTokenAt(text, 3);
    QVERIFY(token.active);
    QCOMPARE(token.start, 0);
    QCOMPARE(token.query, QString::fromUtf8("\u674e\u786c"));

    const chatbox::client::MentionToken after_space =
        chatbox::client::mentionTokenAt(text, 4);
    QVERIFY(!after_space.active);
}

void TestClientCore::mentionTokenRejectsEmail() {
    const chatbox::client::MentionToken token =
        chatbox::client::mentionTokenAt(QStringLiteral("a@b.com"), 7);
    QVERIFY(!token.active);
}

void TestClientCore::mentionTokenNeedsBoundary() {
    const chatbox::client::MentionToken token =
        chatbox::client::mentionTokenAt(QString::fromUtf8("\u4f60\u597d@\u674e"), 4);
    QVERIFY(!token.active);

    const chatbox::client::MentionToken after_space = chatbox::client::mentionTokenAt(
        QString::fromUtf8("hello @\u674e"), 8);
    QVERIFY(after_space.active);
    QCOMPARE(after_space.start, 6);
}

void TestClientCore::mentionTokenAllowsAfterCjkComma() {
    const chatbox::client::MentionToken token =
        chatbox::client::mentionTokenAt(QString::fromUtf8("\u4f60\u597d\uff0c@\u674e"), 5);
    QVERIFY(token.active);
    QCOMPARE(token.start, 3);
}

void TestClientCore::mentionTokenIgnoresCursorBeforeAt() {
    const chatbox::client::MentionToken token =
        chatbox::client::mentionTokenAt(QString::fromUtf8("@\u674e"), 0);
    QVERIFY(!token.active);
}

void TestClientCore::mentionPopupFiltersByNameAndId() {
    MentionCandidate all;
    all.name = QString::fromUtf8("\u6240\u6709\u4eba");
    all.online = true;
    MentionCandidate li;
    li.user_id = 7;
    li.name = QString::fromUtf8("\u674e\u786c");
    li.public_id = QStringLiteral("U00000007");
    li.online = true;
    MentionCandidate he;
    he.user_id = 9;
    he.name = QString::fromUtf8("\u4f55\u586b");
    he.public_id = QStringLiteral("U00000009");

    MentionPopup popup;
    popup.setCandidates({all, li, he}, QString::fromUtf8("\u4f55"));
    QCOMPARE(popup.visibleCount(), 1);  // only 何填 matches
    QCOMPARE(popup.selectedCandidate().name, he.name);

    popup.setCandidates({all, li, he}, QStringLiteral("00000007"));
    QCOMPARE(popup.visibleCount(), 1);  // only 李硬 matches
    QCOMPARE(popup.selectedCandidate().name, li.name);

    popup.setCandidates({all, li, he}, QString::fromUtf8("\u6240"));
    QCOMPARE(popup.visibleCount(), 1);
    QCOMPARE(popup.selectedCandidate().user_id, quint64(0));
}

void TestClientCore::mentionPopupShowsEmptyState() {
    MentionCandidate li;
    li.user_id = 7;
    li.name = QString::fromUtf8("\u674e\u786c");
    MentionPopup popup;
    popup.setCandidates({li}, QStringLiteral("zzz"));
    QCOMPARE(popup.visibleCount(), 0);
    QVERIFY(!popup.hasSelection());
    auto* empty = popup.findChild<QLabel*>(QStringLiteral("MentionEmpty"));
    QVERIFY(empty != nullptr);
    QVERIFY2(empty->text().contains(QString::fromUtf8("\u6ca1\u6709\u5339\u914d")),
             "an empty filter must explain why there are no rows");
}

void TestClientCore::mentionPopupKeyboardSelectionEmitsChosen() {
    MentionCandidate all;
    all.name = QString::fromUtf8("\u6240\u6709\u4eba");
    MentionCandidate li;
    li.user_id = 7;
    li.name = QString::fromUtf8("\u674e\u786c");
    li.public_id = QStringLiteral("U00000007");
    MentionCandidate he;
    he.user_id = 9;
    he.name = QString::fromUtf8("\u4f55\u586b");
    he.public_id = QStringLiteral("U00000009");

    MentionPopup popup;
    QSignalSpy spy(&popup, &MentionPopup::chosen);
    popup.setCandidates({all, li, he}, QString());
    popup.moveSelection(2);  // 所有人 -> 李硬 -> 何填
    QCOMPARE(popup.selectedCandidate().name, he.name);
    popup.activateSelected();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toULongLong(), qulonglong(9));
    QCOMPARE(spy.at(0).at(1).toString(), he.name);
}

void TestClientCore::mentionPopupAllEntryStaysFirst() {
    MentionCandidate li;
    li.user_id = 7;
    li.name = QString::fromUtf8("\u674e\u786c");
    MentionPopup popup;
    popup.setCandidates({li}, QString());
    QCOMPARE(popup.visibleCount(), 2);
    QCOMPARE(popup.selectedCandidate().user_id, quint64(0));
    QCOMPARE(popup.selectedCandidate().name, QString::fromUtf8("\u6240\u6709\u4eba"));
}

void TestClientCore::mentionButtonOpensMentionPopup() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString function = sourceSection(
        source, QStringLiteral("void MainWindow::insertMention("),
        QStringLiteral("void MainWindow::setReplyTo("));
    QVERIFY2(function.contains(QStringLiteral("openMentionPopup(")),
             "the @ button must open the popup instead of a QMenu");
    QVERIFY2(!function.contains(QStringLiteral("menu.exec(")),
             "the @ button must not use the old QMenu");
}

void TestClientCore::typingAtOpensMentionPopup() {
    const QString app = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(app.contains(QStringLiteral("&MainWindow::updateMentionPopup")),
             "textChanged must refresh the mention picker");

    const QString source = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString function = sourceSection(
        source, QStringLiteral("void MainWindow::updateMentionPopup("),
        QStringLiteral("void MainWindow::insertMentionChoice("));
    QVERIFY2(function.contains(QStringLiteral("mentionTokenAt(")),
             "typing @ must be recognised");
    QVERIFY2(function.contains(QStringLiteral("openMentionPopup(")),
             "a recognised token must open the picker");
}

void TestClientCore::privateChatClosesMentionPopup() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString function = sourceSection(
        source, QStringLiteral("void MainWindow::selectPrivate("),
        QStringLiteral("void MainWindow::updatePrivateHeaderAvatar("));
    QVERIFY2(function.contains(QStringLiteral("closeMentionPopup(")),
             "switching to a private chat must close the picker");
}

void TestClientCore::atAllNotificationMatchesEveryone() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_net.cpp"));
    QVERIFY2(source.contains(QStringLiteral("\"@\u6240\u6709\u4eba\"")),
             "@所有人 must trigger the mention notification");
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

void TestClientCore::rendererAddsCenteredTimeSeparator() {
    const QDateTime first_local(QDate::currentDate(), QTime(10, 0),
                                Qt::LocalTime);
    const QDateTime second_local = first_local.addSecs(60);
    const QDateTime third_local = first_local.addSecs(360);

    chatbox::client::ChatMessage first;
    first.message_id = 1;
    first.content = "first";
    first.created_at_utc = first_local.toUTC();
    chatbox::client::ChatMessage second = first;
    second.message_id = 2;
    second.content = "second";
    second.created_at_utc = second_local.toUTC();
    chatbox::client::ChatMessage third = first;
    third.message_id = 3;
    third.content = "third";
    third.created_at_utc = third_local.toUTC();

    const QString html = chatbox::client::renderConversationHtml(
        {first, second, third}, 1);
    QVERIFY(html.contains(QStringLiteral("align=\"center\"")));
    QCOMPARE(html.count(QStringLiteral(">10:00</div>")), 1);
    QCOMPARE(html.count(QStringLiteral(">10:01</div>")), 0);
    QCOMPARE(html.count(QStringLiteral(">10:06</div>")), 1);
}

void TestClientCore::rendererBubbleSpecUsesBorderWithoutPerMessageTimestamp() {
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
    QVERIFY(html.contains(QStringLiteral("line-height: 110%")));
    const QString local_time = chatbox::client::formatLocalTime(created);
    const int timestamp = html.indexOf(local_time);
    const int bubble_close = html.indexOf(QStringLiteral("</table></td></tr></table>"));
    QVERIFY(timestamp > 0);
    QVERIFY(bubble_close > 0);
    // The timestamp lives in the centered separator above the bubble, never
    // in a per-message meta line below it.
    QVERIFY(timestamp < bubble_close);
    QCOMPARE(html.count(local_time), 1);
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
            first, 1, true, chatbox::client::timeSeparatorLabel(first)) +
        chatbox::client::renderMessageFragment(
            second, 1, true, chatbox::client::timeSeparatorLabel(second)) +
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

void TestClientCore::rendererShowsAvatarAndNameOnly() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 1;
    message.sender_id = 10001;
    message.sender_name = QString::fromUtf8("李硬");
    message.content = "hi";
    message.created_at_utc = created;
    message.status = chatbox::client::MessageStatus::kSent;

    chatbox::client::AvatarSrcMap avatars;
    avatars.insert(10001, QStringLiteral("chatbox-avatar://10001?v=3"));
    const QString html =
        chatbox::client::renderConversationHtml({message}, 99999, nullptr, avatars);

    QVERIFY(html.contains(QStringLiteral("chatbox-avatar://10001?v=3")));
    QVERIFY(!html.contains(QStringLiteral("U00010001")));
    QCOMPARE(html.count(QStringLiteral("profile-open:///10001")), 2);
    QVERIFY(html.contains(QStringLiteral("width=\"36\"")));
    QVERIFY(html.contains(QString::fromUtf8("李硬")));

    const QString window = readMainWindowSource();
    QVERIFY(window.contains(QStringLiteral("profile-open")));
    QVERIFY(window.contains(QStringLiteral("openOwnProfile")));
}

void TestClientCore::rendererTightensConsecutiveBubbleGap() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage first;
    first.message_id = 1;
    first.sender_id = 2;
    first.sender_name = "Bob";
    first.content = "one";
    first.created_at_utc = created;
    chatbox::client::ChatMessage second = first;
    second.message_id = 2;
    second.content = "two";
    second.created_at_utc = created.addSecs(30);
    chatbox::client::ChatMessage third = first;
    third.message_id = 3;
    third.content = "three";
    third.created_at_utc = created.addSecs(60);

    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({first, second, third}, 1, &keys);
    ChatView view;
    view.resize(640, 500);
    view.show();
    QTest::qWait(30);
    view.setConversationHtml(html, keys);

    const QRect first_bubble = view.messageBubbleRect(1);
    const QRect second_bubble = view.messageBubbleRect(2);
    const QRect third_bubble = view.messageBubbleRect(3);
    QVERIFY(!first_bubble.isNull());
    QVERIFY(!second_bubble.isNull());
    QVERIFY(!third_bubble.isNull());
    const int gap_12 = second_bubble.top() - first_bubble.bottom();
    const int gap_23 = third_bubble.top() - second_bubble.bottom();
    QVERIFY2(gap_12 <= 6,
             qPrintable(QStringLiteral("first continuation gap %1").arg(gap_12)));
    QVERIFY2(gap_23 <= 6,
             qPrintable(QStringLiteral("second continuation gap %1").arg(gap_23)));
}

void TestClientCore::rendererOwnMessageHidesNameAndId() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage own;
    own.message_id = 2;
    own.sender_id = 7;
    own.sender_name = QStringLiteral("ZeldaUnique");
    own.content = "mine";
    own.created_at_utc = created;
    own.outgoing = true;
    own.status = chatbox::client::MessageStatus::kSent;

    chatbox::client::AvatarSrcMap avatars;
    avatars.insert(7, QStringLiteral("chatbox-avatar://7?v=0"));
    const QString html =
        chatbox::client::renderConversationHtml({own}, 7, nullptr, avatars);

    QVERIFY(html.contains(QStringLiteral("chatbox-avatar://7?v=0")));
    QVERIFY(html.contains(QStringLiteral("profile-open:///7")));
    QVERIFY(!html.contains(QStringLiteral("ZeldaUnique")));
    QVERIFY(!html.contains(QStringLiteral("U00000007")));
}

void TestClientCore::rendererHidesAvatarForConsecutiveMessages() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage first;
    first.message_id = 1;
    first.sender_id = 11;
    first.sender_name = QStringLiteral("OnlyOnceName");
    first.content = "first";
    first.created_at_utc = created;
    chatbox::client::ChatMessage second = first;
    second.message_id = 2;
    second.content = "second";
    second.created_at_utc = created.addSecs(30);

    chatbox::client::AvatarSrcMap avatars;
    avatars.insert(11, QStringLiteral("chatbox-avatar://11?v=2"));
    const QString html =
        chatbox::client::renderConversationHtml({first, second}, 99, nullptr, avatars);

    QCOMPARE(html.count(QStringLiteral("chatbox-avatar://11?v=2")), 1);
    QCOMPARE(html.count(QStringLiteral("OnlyOnceName")), 1);
    QVERIFY(html.contains(QStringLiteral("td width=\"41\"")));
}

void TestClientCore::messageGroupingUsesPreviousMessage() {
    const QDateTime created =
        QDateTime(QDate::currentDate(), QTime(12, 0), Qt::LocalTime).toUTC();
    chatbox::client::ChatMessage first;
    first.message_id = 1;
    first.sender_id = 5;
    first.sender_name = "Bob";
    first.content = "first";
    first.created_at_utc = created;
    chatbox::client::ChatMessage second = first;
    second.message_id = 2;
    second.content = "second";
    second.created_at_utc = created.addSecs(30);
    chatbox::client::ChatMessage third = first;
    third.message_id = 3;
    third.content = "third";
    third.created_at_utc = created.addSecs(360);

    const chatbox::client::MessageGrouping opening =
        chatbox::client::messageGrouping(nullptr, first);
    QVERIFY(opening.show_separator);
    QVERIFY(opening.show_sender_name);

    const chatbox::client::MessageGrouping continuation =
        chatbox::client::messageGrouping(&first, second);
    QVERIFY(!continuation.show_separator);
    QVERIFY(!continuation.show_sender_name);

    const chatbox::client::MessageGrouping after_gap =
        chatbox::client::messageGrouping(&second, third);
    QVERIFY(after_gap.show_separator);
    QVERIFY(after_gap.show_sender_name);
    QCOMPARE(after_gap.separator_label, QStringLiteral("12:06"));
}

void TestClientCore::bubbleTextIsVerticallyCentered() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage own;
    own.message_id = 3;
    own.sender_id = 3;
    own.content = QString::fromUtf8("没办法");
    own.created_at_utc = created;
    own.outgoing = true;
    own.status = chatbox::client::MessageStatus::kSent;

    chatbox::client::AvatarSrcMap avatars;
    avatars.insert(3, QStringLiteral("chatbox-avatar://3?v=0"));
    const QString html =
        chatbox::client::renderConversationHtml({own}, 3, nullptr, avatars);

    QTextDocument doc;
    QImage avatar(36, 36, QImage::Format_ARGB32);
    avatar.fill(Qt::transparent);
    doc.addResource(QTextDocument::ImageResource,
                    QUrl(QStringLiteral("chatbox-avatar://3?v=0")), avatar);
    doc.setHtml(html);
    doc.setTextWidth(400);

    QImage image(420, 320, QImage::Format_ARGB32);
    image.fill(QColor(chatbox::client::theme::kCanvas));
    QPainter painter(&image);
    doc.drawContents(&painter, QRectF(0, 0, 420, 320));
    painter.end();

    const QColor bubble(chatbox::client::theme::kBubbleOwn);
    int bubble_top = -1;
    int bubble_bottom = -1;
    int text_top = -1;
    int text_bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel == bubble) {
                if (bubble_top < 0) {
                    bubble_top = y;
                }
                bubble_bottom = y;
            }
            if (pixel.red() < 80 && pixel.green() < 80 && pixel.blue() < 80) {
                if (text_top < 0) {
                    text_top = y;
                }
                text_bottom = y;
            }
        }
    }
    QVERIFY2(bubble_top >= 0 && text_top > bubble_top,
             "bubble and text pixels must be visible");
    const int top_gap = text_top - bubble_top;
    const int bottom_gap = bubble_bottom - text_bottom;
    QVERIFY2(qAbs(top_gap - bottom_gap) <= 1,
             qPrintable(QStringLiteral("top gap %1, bottom gap %2")
                            .arg(top_gap)
                            .arg(bottom_gap)));
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
    QVERIFY2(edge != border,
             "the bubble border must not be clipped at the viewport edge");
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

    // The centered time separator is not part of any message.
    const QTextCursor date_cursor = view.document()->find(
        chatbox::client::timeSeparatorLabel(first));
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

void TestClientCore::chatViewRegistersAvatarResources() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 1;
    message.sender_id = 7;
    message.sender_name = "Bob";
    message.content = "avatar body";
    message.created_at_utc = created;

    chatbox::client::AvatarSrcMap srcs;
    srcs.insert(7, QStringLiteral("chatbox-avatar://7?v=1"));
    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({message}, 99, &keys, srcs);

    QImage avatar(36, 36, QImage::Format_ARGB32);
    avatar.fill(QColor("#3366cc"));
    QHash<QString, QImage> images;
    images.insert(QStringLiteral("chatbox-avatar://7?v=1"), avatar);

    ChatView view;
    view.setConversationHtml(html, keys, images);

    const QVariant resource = view.document()->resource(
        QTextDocument::ImageResource, QUrl(QStringLiteral("chatbox-avatar://7?v=1")));
    QVERIFY2(resource.isValid(), "avatar resource must be registered");
    QCOMPARE(resource.value<QImage>().size(), QSize(36, 36));
}

void TestClientCore::chatViewBubbleRectExcludesAvatar() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage message;
    message.message_id = 1;
    message.sender_id = 7;
    message.sender_name = "Bob";
    message.content = "avatar body";
    message.created_at_utc = created;

    chatbox::client::AvatarSrcMap srcs;
    srcs.insert(7, QStringLiteral("chatbox-avatar://7?v=1"));
    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({message}, 99, &keys, srcs);

    QImage avatar(36, 36, QImage::Format_ARGB32);
    avatar.fill(QColor("#3366cc"));
    QHash<QString, QImage> images;
    images.insert(QStringLiteral("chatbox-avatar://7?v=1"), avatar);

    ChatView view;
    view.resize(560, 400);
    view.show();
    QTest::qWait(30);
    view.setConversationHtml(html, keys, images);

    const QRect bubble = view.messageBubbleRect(1);
    const QRect outer = view.messageOuterBubbleRect(1);
    QVERIFY(!bubble.isNull());
    QVERIFY(!outer.isNull());
    QVERIFY2(outer.left() > 40,
             qPrintable(QStringLiteral("avatar column must sit outside the bubble, "
                                       "outer left = %1")
                            .arg(outer.left())));
    QVERIFY(bubble.left() > outer.left());
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

void TestClientCore::messageSearchEmitsJumpOnClick() {
    MessageSearchDialog dialog;
    MessageSearchResult first;
    first.message_id = 11;
    first.nickname = "Alice";
    first.content = "hello";
    first.created_at = "2026-10-18 12:00:00";
    MessageSearchResult second = first;
    second.message_id = 12;
    second.content = "world";
    dialog.setResults({first, second});
    dialog.show();
    QTest::qWait(30);

    auto* list = dialog.findChild<QListWidget*>();
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 2);

    QSignalSpy spy(&dialog, &MessageSearchDialog::jumpRequested);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      list->visualItemRect(list->item(0)).center());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toULongLong(), qulonglong(11));
    dialog.close();
}

void TestClientCore::chatViewScrollsToMessageAndFlashes() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    QVector<chatbox::client::ChatMessage> messages;
    for (int i = 1; i <= 60; ++i) {
        chatbox::client::ChatMessage message;
        message.message_id = static_cast<quint64>(i);
        message.sender_id = 2;
        message.sender_name = "Bob";
        message.content = QStringLiteral("body %1").arg(i);
        message.created_at_utc = created.addSecs(i);
        messages.push_back(message);
    }
    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml(messages, 1, &keys);

    ChatView view;
    view.resize(560, 240);
    view.show();
    QTest::qWait(50);
    view.setConversationHtml(html, keys);
    QTest::qWait(30);

    QScrollBar* bar = view.verticalScrollBar();
    bar->setValue(0);
    view.scrollToMessage(50, true);
    QVERIFY2(bar->value() > 0, "jump must move the scrollbar to the target");
    QCOMPARE(view.highlightedMessageId(), quint64(50));
    QVERIFY(view.messageBubbleRect(50).intersects(view.viewport()->rect()));

    QTRY_VERIFY_WITH_TIMEOUT(view.highlightedMessageId() == 0, 3000);
}

void TestClientCore::jumpModeSkipsLiveAppend() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString function = sourceSection(
        source, QStringLiteral("void MainWindow::addIncomingMessage("),
        QStringLiteral("bool MainWindow::currentChatContext("));
    QVERIFY2(function.contains(QStringLiteral("if (jump_mode_)")),
             "live pushes must not append while the user reads an old page");
    QVERIFY2(function.contains(QStringLiteral("return;")),
             "the jump branch must leave the visible page untouched");
}

void TestClientCore::jumpModeShowsReturnButton() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString function = sourceSection(
        source, QStringLiteral("void MainWindow::jumpToMessage("),
        QStringLiteral("void MainWindow::exitJumpMode("));
    QVERIFY2(function.contains(QStringLiteral("jump_latest_button_->show()")),
             "jump mode must expose the return-to-latest button");
}

void TestClientCore::jumpLatestRestoresBottom() {
    const QString source = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(source.contains(QStringLiteral("exitJumpMode(true)")),
             "the return button must reload the latest page");
}

void TestClientCore::switchConversationExitsJumpMode() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    for (const QString& marker :
         {QStringLiteral("void MainWindow::selectRoom("),
          QStringLiteral("void MainWindow::selectPrivate(")}) {
        const QString function = sourceSection(source, marker, QStringLiteral("void MainWindow::"));
        QVERIFY2(function.contains(QStringLiteral("exitJumpMode(false)")),
                 qPrintable(marker));
    }
}

void TestClientCore::jumpRequestUsesTargetPlusOne() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString function = sourceSection(
        source, QStringLiteral("void MainWindow::jumpToMessage("),
        QStringLiteral("void MainWindow::exitJumpMode("));
    QVERIFY2(function.contains(QStringLiteral("message_id + 1")),
             "the jump page must be fetched with before_id = target + 1 so the "
             "target is its newest record");
}

void TestClientCore::jumpTimeoutResetsState() {
    const QString chat = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString jump = sourceSection(
        chat, QStringLiteral("void MainWindow::jumpToMessage("),
        QStringLiteral("void MainWindow::exitJumpMode("));
    QVERIFY2(jump.contains(QStringLiteral("jump_timeout_timer_->start()")),
             "a jump request must arm its timeout");
    const QString exit = sourceSection(
        chat, QStringLiteral("void MainWindow::exitJumpMode("),
        QStringLiteral("void MainWindow::positionJumpLatestButton("));
    QVERIFY2(exit.contains(QStringLiteral("jump_timeout_timer_->stop()")),
             "leaving jump mode must disarm the timeout");

    const QString app = readClientSource(QStringLiteral("ui/main_window.cpp"));
    QVERIFY2(app.contains(QStringLiteral("跳转超时")) &&
                 app.contains(QStringLiteral("exitJumpMode(false)")),
             "the timeout must clear the jump state through exitJumpMode");
}

void TestClientCore::switchConversationClearsSearchState() {
    const QString chat = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    for (const QString& marker :
         {QStringLiteral("void MainWindow::selectRoom("),
          QStringLiteral("void MainWindow::selectPrivate(")}) {
        const QString function = sourceSection(chat, marker,
                                                QStringLiteral("void MainWindow::"));
        QVERIFY2(function.contains(QStringLiteral("closeMessageSearch()")),
                 qPrintable(marker));
    }
    const QString close = sourceSection(
        chat, QStringLiteral("void MainWindow::closeMessageSearch("),
        QStringLiteral("void MainWindow::requestMessageSearch("));
    QVERIFY2(close.contains(QStringLiteral("message_search_seq_ = 0")),
             "a stale search response must be ignored");
    QVERIFY2(close.contains(QStringLiteral("message_search_timeout_timer_->stop()")),
             "the search timeout must be cancelled");
}

void TestClientCore::insertMentionChoiceRecomputesToken() {
    const QString chat = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString function = sourceSection(
        chat, QStringLiteral("void MainWindow::insertMentionChoice("),
        QStringLiteral("void MainWindow::setReplyTo("));
    QVERIFY2(function.contains(QStringLiteral("mentionTokenAt(")),
             "insertion must revalidate the token at the current caret");
}

void TestClientCore::presenceBookAppliesNewerSequence() {
    chatbox::client::PresenceBook book;
    QVERIFY(book.apply(7, true, 10));
    QVERIFY(book.online(7, false));
    QVERIFY(book.apply(7, false, 11));
    QVERIFY(!book.online(7, true));
    QCOMPARE(book.seq(7), quint64(11));
}

void TestClientCore::presenceBookIgnoresStaleSequence() {
    chatbox::client::PresenceBook book;
    QVERIFY(book.apply(7, true, 10));
    QVERIFY(!book.apply(7, false, 9));
    QVERIFY(book.online(7, false));
    QCOMPARE(book.seq(7), quint64(10));

    // Same sequence is also stale: the state was already applied.
    QVERIFY(!book.apply(7, false, 10));
}

void TestClientCore::presenceBookClearsOnDisconnect() {
    chatbox::client::PresenceBook book;
    QVERIFY(book.apply(7, false, 10));
    book.clear();
    QCOMPARE(book.seq(7), quint64(0));
    QVERIFY(book.online(7, true));  // falls back to the caller's snapshot
}

void TestClientCore::presenceOverridesFriendListResponse() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_net.cpp"));
    const QString function = sourceSection(
        source, QStringLiteral("case chatbox::kCmdFriendList:"),
        QStringLiteral("case chatbox::kCmdFriendRequests:"));
    QVERIFY2(function.contains(QStringLiteral("presence_book_.online(")),
             "friend list responses must be overlaid with newer pushes");
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
    QCOMPARE(cache.version(7), quint32(0));

    QImage source(8, 8, QImage::Format_RGB32);
    source.fill(Qt::red);
    cache.store(7, 3, source);
    QVERIFY(cache.contains(7));
    QVERIFY(!cache.needsFetch(7, 3));
    QVERIFY(cache.lookup(7, image));
    QVERIFY(image == source);
    QVERIFY(cache.needsFetch(7, 4));
    QCOMPARE(cache.version(7), quint32(3));

    cache.store(8, 1, QImage());
    QVERIFY(cache.contains(8));
    QVERIFY(!cache.needsFetch(8, 1));
    QVERIFY(!cache.lookup(8, image));
    QCOMPARE(cache.version(8), quint32(1));

    cache.remove(7);
    QVERIFY(!cache.contains(7));
    QVERIFY(cache.needsFetch(7, 3));
    QVERIFY(cache.contains(8));
    QCOMPARE(cache.version(7), quint32(0));

    cache.clear();
    QVERIFY(!cache.contains(8));
    QVERIFY(cache.needsFetch(7, 3));
    QVERIFY(!cache.lookup(7, image));
}

void TestClientCore::avatarSrcFallsBackToVersionZero() {
    QCOMPARE(chatbox::client::avatarSrc(7, 0),
             QStringLiteral("chatbox-avatar://7?v=0"));
    QCOMPARE(chatbox::client::avatarSrc(7, 3),
             QStringLiteral("chatbox-avatar://7?v=3"));
}

void TestClientCore::mainWindowRequestsAvatarForMessageSender() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString render = sourceSection(
        source, QStringLiteral("void MainWindow::renderCurrentConversation("),
        QStringLiteral("void MainWindow::addIncomingMessage("));
    QVERIFY2(render.contains(QStringLiteral("requestMissingMessageAvatars(")),
             "rendering must request avatars for senders without a cache entry");
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
    QVERIFY(source.contains(QStringLiteral("group_list_->setAccessibleName")));
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

void TestClientCore::lineEditsAvoidVerticalClipping() {
    for (const QString& file : {QStringLiteral("resources/style.qss"),
                                QStringLiteral("resources/style-dark.qss")}) {
        const QString qss = readClientSource(file);
        QVERIFY2(!qss.isEmpty(), qPrintable(file));
        QVERIFY2(qss.contains(QStringLiteral("min-height: 36px")), qPrintable(file));
        QVERIFY2(qss.contains(QStringLiteral("padding: 0 14px")), qPrintable(file));
    }
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

void TestClientCore::updatePromptRestoresHiddenWindow() {
    const QString update =
        readClientSource(QStringLiteral("ui/main_window_update.cpp"));
    const QString window = readMainWindowSource();
    const QString main = readClientSource(QStringLiteral("main.cpp"));
    QVERIFY(update.contains(QStringLiteral("showNormal();")));
    QVERIFY(update.contains(QStringLiteral("manualCheckForUpdates")));
    QVERIFY(window.contains(QStringLiteral("update_check_timer_")));
    QVERIFY(window.contains(QStringLiteral("manualCheckForUpdates")));
    // Old helpers clear the marker at five seconds; checking earlier would
    // report a false interruption after every successful update.
    QVERIFY(window.contains(QStringLiteral("checkPreviousUpdateInterrupted")));
    QVERIFY(window.contains(QStringLiteral("QTimer::singleShot(10000")));
    QVERIFY(update.contains(QStringLiteral("checkPreviousUpdateInterrupted")));
    QVERIFY(main.contains(QStringLiteral("setApplicationVersion(\"0.4.1\")")));
}

void TestClientCore::updateManifestParsesValid() {
    const QByteArray json = R"({
        "schema": 1,
        "version": "0.2.4",
        "url": "https://121.5.168.189/updates/chatbox-0.2.4-win64.zip",
        "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "notes": "test release",
        "published_at": "2026-10-27T12:00:00Z"
    })";
    QString error;
    const chatbox::client::UpdateManifest manifest =
        chatbox::client::parseUpdateManifest(json, &error);
    QVERIFY2(manifest.valid, qPrintable(error));
    QCOMPARE(manifest.version, QStringLiteral("0.2.4"));
    QCOMPARE(manifest.url.host(), QStringLiteral("121.5.168.189"));
    QCOMPARE(manifest.notes, QStringLiteral("test release"));
    QCOMPARE(manifest.sha256.size(), 64);
}

void TestClientCore::updateManifestRejectsInvalid() {
    const QByteArray prefix = R"({
        "schema": 1,
        "version": "0.2.4",
        "url": "https://121.5.168.189/updates/chatbox-0.2.4-win64.zip",
        "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    })";
    QString error;
    QVERIFY(!chatbox::client::parseUpdateManifest("{", &error).valid);
    QVERIFY(!error.isEmpty());

    QByteArray json = prefix;
    json.replace("121.5.168.189", "evil.example");
    QVERIFY(!chatbox::client::parseUpdateManifest(json, &error).valid);

    json = prefix;
    json.replace("https://", "http://");
    QVERIFY(!chatbox::client::parseUpdateManifest(json, &error).valid);

    json = prefix;
    json.replace(QByteArray(64, 'a'), QByteArray(63, 'a'));
    QVERIFY(!chatbox::client::parseUpdateManifest(json, &error).valid);

    // The version is used as a temp path component: path separators and
    // suffixes must not survive validation.
    json = prefix;
    json.replace("\"0.2.4\"", "\"0.2.5/../../..\"");
    QVERIFY(!chatbox::client::parseUpdateManifest(json, &error).valid);
    json = prefix;
    json.replace("\"0.2.4\"", "\"0.2.5\\\\..\\\\..\"");
    QVERIFY(!chatbox::client::parseUpdateManifest(json, &error).valid);
    json = prefix;
    json.replace("\"0.2.4\"", "\"0.2.5-beta\"");
    QVERIFY(!chatbox::client::parseUpdateManifest(json, &error).valid);
    json = prefix;
    json.replace("\"0.2.4\"", "\"\"");
    QVERIFY(!chatbox::client::parseUpdateManifest(json, &error).valid);

    QVERIFY(!chatbox::client::parseUpdateManifest(QByteArray(65537, ' '), &error).valid);
}

void TestClientCore::updateVersionComparison() {
    QVERIFY(chatbox::client::isNewerVersion("0.2.3", "0.2.4"));
    QVERIFY(chatbox::client::isNewerVersion("0.2.3", "0.3.0"));
    QVERIFY(!chatbox::client::isNewerVersion("0.2.4", "0.2.4"));
    QVERIFY(!chatbox::client::isNewerVersion("0.2.5", "0.2.4"));
    QVERIFY(!chatbox::client::isNewerVersion("0.2.4", "not-a-version"));
}

void TestClientCore::updateServiceRejectsUntrustedManifestUrl() {
    chatbox::client::UpdateService service;
    QSignalSpy failed(&service, &chatbox::client::UpdateService::checkFailed);
    service.checkForUpdates(QStringLiteral("http://121.5.168.189/updates/latest.json"),
                            QString());
    QCOMPARE(failed.count(), 1);
    QVERIFY(!failed.takeFirst().at(0).toString().isEmpty());
}

void TestClientCore::updateServiceUsesPinnedCa() {
    const QString source =
        readClientSource(QStringLiteral("core/update_service.cpp"));
    const QString header =
        readClientSource(QStringLiteral("core/update_service.h"));
    QVERIFY2(!source.isEmpty(), "core/update_service.cpp must be readable");
    QVERIFY2(!header.isEmpty(), "core/update_service.h must be readable");
    QVERIFY(source.contains(QStringLiteral("QSslConfiguration")));
    QVERIFY(source.contains(QStringLiteral("QSslCertificate")));
    QVERIFY(source.contains(QStringLiteral(":/certs/ca.crt")));
    const QString qrc =
        readClientSource(QStringLiteral("resources/resources.qrc"));
    QVERIFY(qrc.contains(QStringLiteral("certs/ca.crt")));
    QVERIFY(!source.contains(QStringLiteral("setPeerVerifyMode(QSslSocket::VerifyNone)")));
    QVERIFY(header.contains(QStringLiteral("QCryptographicHash")));
    QVERIFY(source.contains(QStringLiteral("download_hash_")));
    QVERIFY(source.contains(QStringLiteral("setTransferTimeout")));
    QVERIFY(source.contains(QStringLiteral("setMaximumRedirectsAllowed")));
}

void TestClientCore::updateServiceCancelIsSafe() {
    QVERIFY(QFileInfo(testCaPath()).isFile());
    AbortSignalsFinishedManager manager;
    chatbox::client::UpdateService service(&manager);
    QSignalSpy failed(&service,
                      &chatbox::client::UpdateService::downloadFailed);
    QSignalSpy finished(&service,
                        &chatbox::client::UpdateService::downloadFinished);

    chatbox::client::UpdateManifest manifest;
    manifest.valid = true;
    manifest.version = QStringLiteral("0.2.5");
    manifest.url = QUrl(QStringLiteral(
        "https://121.5.168.189/updates/chatbox-0.2.5-win64.zip"));
    manifest.sha256 = QString(64, QLatin1Char('a'));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("cancel.zip"));
    service.downloadUpdate(manifest, target, testCaPath());
    QVERIFY(QFile::exists(target));

    service.cancel();
    QCOMPARE(failed.count(), 0);
    QCOMPARE(finished.count(), 0);
    QVERIFY(!QFile::exists(target));
}

void TestClientCore::updateServiceRejectsShaMismatch() {
    QVERIFY(QFileInfo(testCaPath()).isFile());
    FixedBodyManager manager(QByteArray("this is not the expected archive"));
    chatbox::client::UpdateService service(&manager);
    QSignalSpy failed(&service,
                      &chatbox::client::UpdateService::downloadFailed);
    QSignalSpy finished(&service,
                        &chatbox::client::UpdateService::downloadFinished);

    chatbox::client::UpdateManifest manifest;
    manifest.valid = true;
    manifest.version = QStringLiteral("0.2.5");
    manifest.url = QUrl(QStringLiteral(
        "https://121.5.168.189/updates/chatbox-0.2.5-win64.zip"));
    manifest.sha256 = QString(64, QLatin1Char('0'));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("mismatch.zip"));
    service.downloadUpdate(manifest, target, testCaPath());

    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QCOMPARE(finished.count(), 0);
    QVERIFY(!QFile::exists(target));
}

void TestClientCore::updateDialogHasRequiredActions() {
    chatbox::client::UpdateManifest manifest;
    manifest.version = QStringLiteral("0.2.5");
    manifest.notes = QStringLiteral("更新说明");
    UpdateDialog dialog(manifest);
    QVERIFY(!dialog.isModal());
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("UpdateNotesLabel")) != nullptr);
    QVERIFY(dialog.findChild<QProgressBar*>(QStringLiteral("UpdateProgressBar")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("UpdateNowButton")) != nullptr);
    QVERIFY(dialog.findChild<QPushButton*>(QStringLiteral("UpdateLaterButton")) != nullptr);
    auto* cancel =
        dialog.findChild<QPushButton*>(QStringLiteral("UpdateCancelButton"));
    QVERIFY(cancel != nullptr);
    QVERIFY(cancel->isHidden());

    const QString source =
        readClientSource(QStringLiteral("ui/main_window_update.cpp"));
    QVERIFY2(!source.isEmpty(), "ui/main_window_update.cpp must be readable");
    QVERIFY(source.contains(QStringLiteral("update_dialog_->show()")));
    QVERIFY(!source.contains(QStringLiteral("update_dialog_->exec()")));
}

void TestClientCore::updateDialogShowsCancelDuringDownload() {
    chatbox::client::UpdateManifest manifest;
    manifest.version = QStringLiteral("0.2.5");
    UpdateDialog dialog(manifest);
    auto* cancel =
        dialog.findChild<QPushButton*>(QStringLiteral("UpdateCancelButton"));
    auto* later =
        dialog.findChild<QPushButton*>(QStringLiteral("UpdateLaterButton"));
    auto* progress =
        dialog.findChild<QProgressBar*>(QStringLiteral("UpdateProgressBar"));
    QVERIFY(cancel != nullptr);
    QVERIFY(later != nullptr);
    QVERIFY(progress != nullptr);
    QVERIFY(cancel->isHidden());

    dialog.setBusy(true);
    QVERIFY(!cancel->isHidden());
    QVERIFY(later->isHidden());
    dialog.setDownloadProgress(50, 100);
    QCOMPARE(progress->value(), 50);

    dialog.setBusy(false);
    QVERIFY(cancel->isHidden());
    QVERIFY(!later->isHidden());
    QVERIFY(progress->isHidden());
}

void TestClientCore::startupSchedulesOneUpdateCheck() {
    const QString source = readMainWindowSource();
    QVERIFY(source.contains(QStringLiteral("QTimer::singleShot(1500")));
    QVERIFY(source.contains(QStringLiteral("&MainWindow::checkForUpdates")));
}

void TestClientCore::updateInstallerValidatesPackage() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    QVERIFY(!chatbox::client::validateUpdateDirectory(dir.path(), &error));
    QVERIFY(!error.isEmpty());

    const QStringList required = {
        QStringLiteral("chatbox.exe"),
        QStringLiteral("Qt5Core.dll"),
        QStringLiteral("Qt5Gui.dll"),
        QStringLiteral("Qt5Widgets.dll"),
        QStringLiteral("Qt5Sql.dll"),
        QStringLiteral("Qt5Network.dll"),
        QStringLiteral("platforms/qwindows.dll"),
        QStringLiteral("sqldrivers/qsqlite.dll"),
        QStringLiteral("certs/ca.crt"),
        QStringLiteral("libssl-4-x64.dll"),
        QStringLiteral("libcrypto-4-x64.dll"),
    };
    auto touch = [&dir](const QString& relative) {
        const QString path = QDir(dir.path()).filePath(relative);
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
    };
    for (const QString& relative : required) {
        touch(relative);
    }
    QVERIFY2(chatbox::client::validateUpdateDirectory(dir.path(), &error),
             qPrintable(error));

    const QString platform = QDir(dir.path()).filePath(
        QStringLiteral("platforms/qwindows.dll"));
    QVERIFY(QFile::remove(platform));
    QVERIFY(!chatbox::client::validateUpdateDirectory(dir.path(), &error));
    touch(QStringLiteral("platforms/qwindows.dll"));

    const QString ssl = QDir(dir.path()).filePath(
        QStringLiteral("libssl-4-x64.dll"));
    QVERIFY(QFile::remove(ssl));
    QVERIFY(!chatbox::client::validateUpdateDirectory(dir.path(), &error));
}

void TestClientCore::updateInstallerExtractsZip() {
    const QString tar = QStandardPaths::findExecutable(QStringLiteral("tar.exe"));
    QVERIFY(!tar.isEmpty());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    auto touch = [](const QString& path) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.close();
        return true;
    };
    const QStringList required = {
        QStringLiteral("chatbox.exe"),
        QStringLiteral("Qt5Core.dll"),
        QStringLiteral("Qt5Gui.dll"),
        QStringLiteral("Qt5Widgets.dll"),
        QStringLiteral("Qt5Sql.dll"),
        QStringLiteral("Qt5Network.dll"),
        QStringLiteral("platforms/qwindows.dll"),
        QStringLiteral("sqldrivers/qsqlite.dll"),
        QStringLiteral("certs/ca.crt"),
        QStringLiteral("libssl-4-x64.dll"),
        QStringLiteral("libcrypto-4-x64.dll"),
    };
    const QString source = dir.filePath(QStringLiteral("source"));
    const QString package = source + QStringLiteral("/chatbox-0.2.5-win64");
    for (const QString& relative : required) {
        QVERIFY(touch(package + QLatin1Char('/') + relative));
    }
    const QString zip = dir.filePath(QStringLiteral("chatbox-0.2.5-win64.zip"));
    QProcess archive;
    archive.setProgram(tar);
    archive.setArguments({QStringLiteral("-a"), QStringLiteral("-cf"), zip,
                          QStringLiteral("-C"), source,
                          QStringLiteral("chatbox-0.2.5-win64")});
    archive.start();
    QVERIFY(archive.waitForFinished(60000));
    QCOMPARE(archive.exitStatus(), QProcess::NormalExit);
    QCOMPARE(archive.exitCode(), 0);

    QString root;
    QString error;
    QVERIFY2(chatbox::client::extractUpdateZip(
                 zip, dir.filePath(QStringLiteral("staging")), &root, &error),
             qPrintable(error));
    QCOMPARE(QFileInfo(root).fileName(), QStringLiteral("chatbox-0.2.5-win64"));
    QVERIFY(QFileInfo(root + QStringLiteral("/chatbox.exe")).isFile());

    const QString bad_source = dir.filePath(QStringLiteral("bad"));
    const QString bad_package =
        bad_source + QStringLiteral("/chatbox-0.2.5-win64");
    QVERIFY(touch(bad_package + QStringLiteral("/chatbox.exe")));
    const QString bad_zip = dir.filePath(QStringLiteral("bad.zip"));
    QProcess bad_archive;
    bad_archive.setProgram(tar);
    bad_archive.setArguments({QStringLiteral("-a"), QStringLiteral("-cf"),
                              bad_zip, QStringLiteral("-C"), bad_source,
                              QStringLiteral("chatbox-0.2.5-win64")});
    bad_archive.start();
    QVERIFY(bad_archive.waitForFinished(60000));
    QCOMPARE(bad_archive.exitCode(), 0);
    QVERIFY(!chatbox::client::extractUpdateZip(
        bad_zip, dir.filePath(QStringLiteral("bad-staging")), &root, &error));
}

void TestClientCore::updateMarkersRoundTrip() {
    const QString base = QDir::tempPath() + QStringLiteral("/ChatBox");
    QVERIFY(QDir().mkpath(base));
    const QString result_path =
        base + QStringLiteral("/update-result.txt");
    const QString marker_path =
        base + QStringLiteral("/update-in-progress");
    QFile::remove(result_path);
    QFile::remove(marker_path);

    QFile result(result_path);
    QVERIFY(result.open(QIODevice::WriteOnly));
    result.write("\xEF\xBB\xBF" "failed to replace files");
    result.close();
    QCOMPARE(chatbox::client::consumeUpdateResult(),
             QStringLiteral("failed to replace files"));
    QVERIFY(!QFile::exists(result_path));

    QFile marker(marker_path);
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("1");
    marker.close();
    QVERIFY(chatbox::client::consumeUpdateInterrupted());
    QVERIFY(!QFile::exists(marker_path));
    QVERIFY(!chatbox::client::consumeUpdateInterrupted());
}

void TestClientCore::historyDirectoryInsideInstallRejected() {
    const QString install = QCoreApplication::applicationDirPath();
    QVERIFY(chatbox::client::isHistoryDirectoryInsideInstall(install));
    QVERIFY(chatbox::client::isHistoryDirectoryInsideInstall(
        install + QStringLiteral("/history")));
    QVERIFY(!chatbox::client::isHistoryDirectoryInsideInstall(
        QDir::tempPath()));
    QVERIFY(!chatbox::client::isHistoryDirectoryInsideInstall(
        install + QStringLiteral("/../sibling")));
}

void TestClientCore::fileSaveDirectoryValidatesAndAvoidsCollisions() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    QVERIFY(chatbox::client::validateFileSaveDirectory(dir.path(), &error));
    QVERIFY(!chatbox::client::validateFileSaveDirectory(
        QCoreApplication::applicationDirPath(), &error));
    QVERIFY(!chatbox::client::validateFileSaveDirectory(QString(), &error));

    QFile first(QDir(dir.path()).filePath(QStringLiteral("a.txt")));
    QVERIFY(first.open(QIODevice::WriteOnly));
    first.close();
    const QString second =
        chatbox::client::uniqueFilePath(dir.path(), QStringLiteral("a.txt"));
    QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("a (1).txt"));
    QFile part(QDir(dir.path()).filePath(QStringLiteral("a (1).txt.part")));
    QVERIFY(part.open(QIODevice::WriteOnly));
    part.close();
    const QString third =
        chatbox::client::uniqueFilePath(dir.path(), QStringLiteral("a.txt"));
    QCOMPARE(QFileInfo(third).fileName(), QStringLiteral("a (2).txt"));
    QVERIFY(chatbox::client::defaultFileSaveDirectory().endsWith(
        QStringLiteral("ChatBox")));
}

void TestClientCore::updateServerSiteUsesHttps() {
    const QString base = QCoreApplication::applicationDirPath() +
                         QStringLiteral("/../../..");
    QFile nginx(base + QStringLiteral("/deploy/nginx-update.conf"));
    QVERIFY(nginx.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString config = QString::fromUtf8(nginx.readAll());
    QVERIFY(config.contains(QStringLiteral("listen 443 ssl")));
    QVERIFY(config.contains(QStringLiteral("alias /var/www/chatbox-updates/")));
    QVERIFY(!config.contains(QStringLiteral("autoindex on")));

    QFile publish(base + QStringLiteral("/tools/publish_update.ps1"));
    QVERIFY(publish.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString script = QString::fromUtf8(publish.readAll());
    QVERIFY(script.contains(QStringLiteral("scp")));
    QVERIFY(script.contains(QStringLiteral("sha256sum")));
    QVERIFY(script.contains(QStringLiteral("121.5.168.189")));
    QVERIFY(script.contains(QStringLiteral("latest.json.tmp")));
    QVERIFY(script.contains(QStringLiteral("releases/$Version")));
    QVERIFY(script.contains(QStringLiteral("$LASTEXITCODE")));
}

void TestClientCore::updateHelperHasRollbackSafety() {
    const QString helper =
        readClientSource(QStringLiteral("resources/update_helper.ps1"));
    QVERIFY2(!helper.isEmpty(), "update_helper.ps1 must be readable");
    QVERIFY(helper.contains(QStringLiteral("Wait-Process")));
    QVERIFY(helper.contains(QStringLiteral("Copy-Item")));
    QVERIFY(helper.contains(QStringLiteral("update-in-progress")));
    QVERIFY(helper.contains(QStringLiteral("Start-Sleep -Seconds 5")));
    QVERIFY(helper.contains(QStringLiteral("robocopy")));
    QVERIFY(helper.contains(QStringLiteral("backup-")));
    QVERIFY(helper.contains(QStringLiteral("backup_complete")));
    // The replacement marker must disappear before the new client starts:
    // otherwise the fresh process sees it during startup and reports a
    // false "update was interrupted" warning on every successful update.
    const int marker_removal =
        helper.indexOf(QStringLiteral("Remove-Item -LiteralPath $marker"));
    const int start_process = helper.indexOf(
        QStringLiteral("Start-Process -FilePath $Executable"));
    QVERIFY2(marker_removal >= 0 && start_process > marker_removal,
             "clear update-in-progress before starting the new client");
    for (const QChar ch : helper) {
        QVERIFY2(ch.unicode() < 128,
                 "update_helper.ps1 must stay ASCII for PowerShell 5.1");
    }
}

void TestClientCore::groupStoreFiltersAndSorts() {
    chatbox::client::GroupStore store;
    QVector<chatbox::client::GroupInfo> groups(3);
    groups[0].room_id = 2;
    groups[0].name = QStringLiteral("普通群");
    groups[1].room_id = 1;
    groups[1].name = QStringLiteral("群聊A");
    groups[1].is_default = true;
    groups[1].unread = 3;
    groups[2].room_id = 3;
    groups[2].name = QStringLiteral("旧群");
    groups[2].status = 1;
    store.replaceAll(groups);

    const QVector<chatbox::client::GroupInfo> active = store.active();
    QCOMPARE(active.size(), 2);
    QCOMPARE(active[0].room_id, quint64(1));
    QVERIFY(active[0].is_default);
    QCOMPARE(store.dissolved().size(), 1);
    QCOMPARE(store.dissolved()[0].room_id, quint64(3));

    QVector<chatbox::client::GroupMemberInfo> members(2);
    members[0].user_id = 4;
    members[0].nickname = QStringLiteral("阿杰");
    members[1].user_id = 5;
    store.setMembers(1, members);
    QCOMPARE(store.members(1).size(), 2);
    store.setMembers(3, members);
    store.replaceAll(active);
    QCOMPARE(store.members(1).size(), 2);
    QCOMPARE(store.members(3).size(), 0);
    store.setUnread(1, 0);
    QCOMPARE(store.find(1)->unread, 0);
}

void TestClientCore::groupDialogsValidateInput() {
    GroupCreateDialog create;
    QSignalSpy create_spy(&create, &GroupCreateDialog::createRequested);
    auto* name = create.findChild<QLineEdit*>(QStringLiteral("GroupNameEdit"));
    auto* create_button =
        create.findChild<QPushButton*>(QStringLiteral("GroupCreateSubmit"));
    QVERIFY(name != nullptr);
    QVERIFY(create_button != nullptr);
    create_button->click();
    QCOMPARE(create_spy.count(), 0);
    name->setText(QStringLiteral("设计群"));
    create_button->click();
    QCOMPARE(create_spy.count(), 1);
    QCOMPARE(create_spy.takeFirst().at(0).toString(), QStringLiteral("设计群"));

    GroupJoinDialog join;
    QSignalSpy search_spy(&join, &GroupJoinDialog::searchRequested);
    QSignalSpy join_spy(&join, &GroupJoinDialog::joinRequested);
    auto* number = join.findChild<QLineEdit*>(QStringLiteral("GroupNumberEdit"));
    auto* search_button =
        join.findChild<QPushButton*>(QStringLiteral("GroupJoinSearchButton"));
    auto* join_button =
        join.findChild<QPushButton*>(QStringLiteral("GroupJoinSubmit"));
    QVERIFY(number != nullptr);
    QVERIFY(search_button != nullptr);
    QVERIFY(join_button != nullptr);
    number->setText(QStringLiteral("123"));
    search_button->click();
    QCOMPARE(search_spy.count(), 0);
    number->setText(QStringLiteral("10000001"));
    search_button->click();
    QCOMPARE(search_spy.count(), 1);
    QVERIFY(!join_button->isEnabled());
    join.setSearchResult(true, QStringLiteral("设计群"), 12, 0);
    QVERIFY(join_button->isEnabled());
    join_button->click();
    QCOMPARE(join_spy.count(), 1);
    QCOMPARE(join_spy.takeFirst().at(0).toString(), QStringLiteral("10000001"));
}

void TestClientCore::sidebarHasGroupTabs() {
    const QString source = readMainWindowSource();
    QVERIFY(source.contains(QStringLiteral("SidebarTabs")));
    QVERIFY(source.contains(QStringLiteral("group_list_")));
    QVERIFY(source.contains(QStringLiteral("CreateGroupButton")));
    QVERIFY(source.contains(QStringLiteral("JoinGroupButton")));
    QVERIFY(source.contains(QStringLiteral("GroupNoticeButton")));
    QVERIFY(source.contains(QStringLiteral("QTabWidget")));
}

void TestClientCore::groupListTitleShowsOnlyName() {
    chatbox::client::GroupInfo group;
    group.name = QString::fromUtf8("公共大厅");
    group.is_default = true;
    group.member_count = 13;
    group.role = 2;
    group.unread = 4;
    QCOMPARE(chatbox::client::groupListTitle(group),
             QString::fromUtf8("公共大厅"));
}

void TestClientCore::sidebarAndMenuIconsAreDistinct() {
    const QColor color(60, 60, 60);
    const QImage collapsed =
        chatbox::client::sidebarToggleIcon(false, color).pixmap(20, 20).toImage();
    const QImage expanded =
        chatbox::client::sidebarToggleIcon(true, color).pixmap(20, 20).toImage();
    QVERIFY(!collapsed.isNull());
    QVERIFY(!expanded.isNull());
    QVERIFY(collapsed != expanded);
    QVERIFY(!chatbox::client::hamburgerIcon(color).pixmap(20, 20).isNull());

    const QString source = readMainWindowSource();
    QVERIFY(source.contains(QStringLiteral("right_settings_button_->setIcon(")));
    QVERIFY(source.contains(QStringLiteral("account_menu_hint_->setPixmap(")));
    QVERIFY(source.contains(QStringLiteral("hamburgerIcon(")));
}

void TestClientCore::rightSidebarHasRequiredPanels() {
    const QString source = readMainWindowSource();
    QVERIFY(source.contains(QStringLiteral("RightSidebar")));
    QVERIFY(source.contains(QStringLiteral("RightModeStack")));
    QVERIFY(source.contains(QStringLiteral("RightMemberList")));
    QVERIFY(source.contains(QStringLiteral("RightProfileCard")));
    QVERIFY(source.contains(QStringLiteral("RightGroupSearch")));
    QVERIFY(source.contains(QStringLiteral("RightPrivateSearch")));
    QVERIFY(source.contains(QStringLiteral("RightSearchPage")));
    QVERIFY(source.contains(QStringLiteral("RightSearchResults")));
    QVERIFY(source.contains(QStringLiteral("RightFileButton")));
    QVERIFY(source.contains(QStringLiteral("RightInviteButton")));
    QVERIFY(source.contains(QStringLiteral("RightLeaveButton")));
    QVERIFY(source.contains(QStringLiteral("RightProfileButton")));
    QVERIFY(!source.contains(QStringLiteral("more_menu_")));
    const QString profile =
        readClientSource(QStringLiteral("ui/main_window_profile.cpp"));
    QVERIFY(profile.contains(QStringLiteral("void MainWindow::toggleRightPanel()")));
    QVERIFY(profile.contains(QStringLiteral("right_panel_->setVisible")));
    const QString chat =
        readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    QVERIFY(chat.contains(QStringLiteral("void MainWindow::startPanelSearch")));
    const QString net =
        readClientSource(QStringLiteral("ui/main_window_net.cpp"));
    QVERIFY(net.contains(QStringLiteral("frame.header.flags & chatbox::kFlagError")));
    QVERIFY(net.contains(QStringLiteral("joined_groups_.clear();")));
    QVERIFY(net.contains(QStringLiteral("selectDissolvedGroup")));
    const QString groups =
        readClientSource(QStringLiteral("ui/main_window_groups.cpp"));
    QVERIFY(groups.contains(QStringLiteral("void MainWindow::openGroupInviteDialog")));
    QVERIFY(groups.contains(QStringLiteral("void MainWindow::leaveGroup")));
    QVERIFY(groups.contains(QStringLiteral("kCmdGroupLeave")));
}

void TestClientCore::scrollLoadsOlderAndJumpsToSearchHit() {
    const QString window = readMainWindowSource();
    const QString chat =
        readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString net =
        readClientSource(QStringLiteral("ui/main_window_net.cpp"));
    const QString search =
        readClientSource(QStringLiteral("ui/message_search_dialog.cpp"));
    QVERIFY(window.contains(QStringLiteral("maybeLoadOlder();")));
    QVERIFY(!window.contains(QStringLiteral("load_more_action_")));
    QVERIFY(chat.contains(QStringLiteral("ScrollMode::kPreserveAnchor")));
    QVERIFY(chat.contains(QStringLiteral("previous_max")));
    QVERIFY(chat.contains(QStringLiteral("void MainWindow::jumpToMessage")));
    QVERIFY(net.contains(QStringLiteral("ScrollMode::kPreserveAnchor")));
    QVERIFY(search.contains(QStringLiteral("jumpRequested")));
}

void TestClientCore::groupSettingsRespectsRoles() {
    chatbox::client::GroupInfo group;
    group.room_id = 2;
    group.name = QStringLiteral("设计群");
    group.group_no = QStringLiteral("10000002");
    group.role = 2;
    chatbox::client::GroupMemberInfo owner;
    owner.user_id = 1;
    owner.nickname = QStringLiteral("阿A");
    owner.role = 2;
    chatbox::client::GroupMemberInfo member;
    member.user_id = 2;
    member.nickname = QStringLiteral("阿B");
    member.role = 0;
    GroupSettingsDialog dialog(group, {owner, member}, 1);
    auto* disband =
        dialog.findChild<QPushButton*>(QStringLiteral("GroupDisbandButton"));
    auto* rename =
        dialog.findChild<QPushButton*>(QStringLiteral("GroupRenameButton"));
    auto* admin_list =
        dialog.findChild<QListWidget*>(QStringLiteral("GroupMemberAdminList"));
    auto* remove =
        dialog.findChild<QPushButton*>(QStringLiteral("GroupRemoveMemberButton"));
    QVERIFY(disband != nullptr);
    QVERIFY(rename != nullptr);
    QVERIFY(admin_list != nullptr);
    QVERIFY(remove != nullptr);
    QVERIFY(!disband->isHidden());
    QVERIFY(rename->isEnabled());
    QCOMPARE(admin_list->count(), 2);
    admin_list->setCurrentRow(1);
    QVERIFY(remove->isEnabled());

    group.is_default = true;
    member.user_id = 2;
    GroupSettingsDialog default_dialog(group, {owner, member}, 1);
    auto* default_disband =
        default_dialog.findChild<QPushButton*>(QStringLiteral("GroupDisbandButton"));
    auto* default_remove =
        default_dialog.findChild<QPushButton*>(QStringLiteral("GroupRemoveMemberButton"));
    auto* default_members =
        default_dialog.findChild<QListWidget*>(QStringLiteral("GroupMemberAdminList"));
    QVERIFY(default_disband != nullptr);
    QVERIFY(default_remove != nullptr);
    QVERIFY(default_members != nullptr);
    QVERIFY(default_disband->isHidden());
    default_members->setCurrentRow(1);
    QVERIFY2(!default_remove->isEnabled(),
             "default group members must not be removable");

    group.is_default = false;
    group.role = 0;
    GroupSettingsDialog member_dialog(group, {owner, member}, 2);
    auto* member_rename =
        member_dialog.findChild<QPushButton*>(QStringLiteral("GroupRenameButton"));
    QVERIFY(member_rename != nullptr);
    QVERIFY(!member_rename->isEnabled());

    chatbox::client::GroupRequestInfo request;
    request.request_id = 9;
    request.nickname = QStringLiteral("阿C");
    request.message = QStringLiteral("想加入");
    chatbox::client::GroupRequestInfo invite;
    invite.request_id = 10;
    invite.nickname = QStringLiteral("阿D");
    invite.room_name = QStringLiteral("设计群");
    GroupNotifyDialog notify;
    notify.setData({request}, {invite}, true);
    auto* list = notify.findChild<QListWidget*>(QStringLiteral("GroupNotifyList"));
    auto* accept =
        notify.findChild<QPushButton*>(QStringLiteral("GroupNotifyAccept"));
    QVERIFY(list != nullptr);
    QVERIFY(accept != nullptr);
    QCOMPARE(list->count(), 2);
    list->setCurrentRow(0);
    QVERIFY(accept->isEnabled());
    QSignalSpy spy(&notify, &GroupNotifyDialog::actionRequested);
    accept->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 0);
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

    // A custom/LAN endpoint is no longer selectable, so it is reset too.
    QSettings custom(dir.filePath(QStringLiteral("custom.ini")),
                     QSettings::IniFormat);
    custom.setValue(QStringLiteral("server/host"), QStringLiteral("example.test"));
    custom.setValue(QStringLiteral("server/port"), 12345);
    custom.setValue(QStringLiteral("session/token"), QStringLiteral("stale"));
    QVERIFY(chatbox::client::migrateServerEndpoint(custom));
    QCOMPARE(custom.value(QStringLiteral("server/host")).toString(),
             QStringLiteral("121.5.168.189"));
    QCOMPARE(custom.value(QStringLiteral("server/port")).toInt(), 9443);
    QVERIFY(!custom.contains(QStringLiteral("session/token")));
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
    QVERIFY2(source.contains(QStringLiteral("more_button_->setPopupMode(QToolButton::DelayedPopup)")),
             "the header more control is a plain toggle");
    QVERIFY2(source.contains(QStringLiteral("&MainWindow::toggleRightPanel")),
             "the header more control opens the right sidebar");
    QVERIFY2(source.contains(QStringLiteral("more_button_->setIcon(")) &&
                 source.contains(QStringLiteral("sidebarToggleIcon(")),
             "the header more control uses the sidebar state icon");
    QVERIFY2(!source.contains(QStringLiteral("more_button_->setText")),
             "the header more control must not render text");
    // The indicator rule moved from an inline setStyleSheet into style.qss so
    // it follows the theme; assert against the stylesheet that actually
    // carries it now.
    const QString qss = readClientSource(QStringLiteral("resources/style.qss"));
    QVERIFY2(qss.contains(QStringLiteral("QToolButton#MoreButton::menu-indicator")),
             "the header more control must hide the menu indicator arrow");
    QVERIFY2(!source.contains(QStringLiteral("more_menu_")),
             "the header must not keep a popup menu");
    QVERIFY2(source.contains(QStringLiteral("RightGroupSearch")),
             "message search lives in the right sidebar");
    QVERIFY2(source.contains(QStringLiteral("RightPrivateSearch")),
             "private message search lives in the right sidebar");
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

void TestClientCore::closeToTrayPolicy() {
    QVERIFY(chatbox::client::shouldHideToTray(true, true, false));
    QVERIFY(!chatbox::client::shouldHideToTray(true, true, true));
    QVERIFY(!chatbox::client::shouldHideToTray(false, true, false));
    QVERIFY(!chatbox::client::shouldHideToTray(true, false, false));

    const QString source = readMainWindowSource();
    QVERIFY2(source.contains(QStringLiteral("shouldHideToTray(")),
             "closeEvent must use the testable tray policy");
    QVERIFY2(source.contains(QStringLiteral("requestQuit(")),
             "tray Exit must have an explicit quit path");
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
        QVERIFY(db.tables().contains(QStringLiteral("pending_uploads")));
        QVERIFY(db.tables().contains(QStringLiteral("files")));
        QVERIFY(db.tables().contains(QStringLiteral("groups")));
        QCOMPARE(store.schemaVersion(), 3);
        QSqlQuery columns(db);
        QVERIFY(columns.exec(QStringLiteral("PRAGMA table_info(messages)")));
        QStringList column_names;
        while (columns.next()) {
            column_names << columns.value(1).toString();
        }
        QVERIFY(column_names.contains(QStringLiteral("file_id")));
        QVERIFY(column_names.contains(QStringLiteral("transfer_state")));
        QSqlQuery version(db);
        QVERIFY(version.exec(QStringLiteral("PRAGMA user_version")) && version.next());
        QCOMPARE(version.value(0).toInt(), 3);
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
void TestClientCore::historyStoreMigratesV1() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("history.sqlite"));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("v1_setup"));
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " user_id INTEGER NOT NULL, context INTEGER NOT NULL,"
            " target_id INTEGER NOT NULL, message_id INTEGER NOT NULL,"
            " sender_id INTEGER NOT NULL, sender_name TEXT NOT NULL DEFAULT '',"
            " content TEXT NOT NULL, created_at_utc TEXT NOT NULL,"
            " reply_to_id INTEGER NOT NULL DEFAULT 0,"
            " reply_sender_name TEXT NOT NULL DEFAULT '',"
            " reply_content TEXT NOT NULL DEFAULT '',"
            " recalled INTEGER NOT NULL DEFAULT 0,"
            " outgoing INTEGER NOT NULL DEFAULT 0)")));
        QVERIFY(query.exec(QStringLiteral(
            "CREATE TABLE conversations (user_id INTEGER NOT NULL,"
            " context INTEGER NOT NULL, target_id INTEGER NOT NULL,"
            " title TEXT NOT NULL, last_message_id INTEGER NOT NULL DEFAULT 0,"
            " last_content TEXT NOT NULL DEFAULT '',"
            " last_time_utc TEXT NOT NULL DEFAULT '',"
            " PRIMARY KEY (user_id, context, target_id))")));
        QVERIFY(query.exec(QStringLiteral(
            "INSERT INTO messages (user_id, context, target_id, message_id,"
            " sender_id, content, created_at_utc) VALUES (7, 0, 1, 42, 4,"
            " 'old', '2026-10-01 00:00:00')")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=1")));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("v1_setup"));

    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY2(store.open(dir.path(), 7, &error), qPrintable(error));
    QCOMPARE(store.schemaVersion(), 3);
    const QVector<chatbox::client::ChatMessage> messages =
        store.recent(chatbox::client::ChatContext::kRoom, 1, 10);
    QCOMPARE(messages.size(), 1);
    QCOMPARE(messages[0].content, QStringLiteral("old"));
    store.close();
}

void TestClientCore::historyStorePendingUploadRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY2(store.open(dir.path(), 21, &error), qPrintable(error));

    chatbox::client::HistoryStore::PendingUpload upload;
    upload.upload_id = QStringLiteral("upload-1");
    upload.context = chatbox::client::ChatContext::kRoom;
    upload.target_id = 9;
    upload.source_path = QStringLiteral("C:/tmp/a.txt");
    upload.name = QStringLiteral("a.txt");
    upload.size = 100;
    upload.mime = QStringLiteral("text/plain");
    upload.offset = 40;
    upload.status = QStringLiteral("uploading");
    QVERIFY(store.savePendingUpload(upload));

    QVector<chatbox::client::HistoryStore::PendingUpload> pending =
        store.pendingUploads();
    QCOMPARE(pending.size(), 1);
    QCOMPARE(pending[0].upload_id, QStringLiteral("upload-1"));
    QCOMPARE(pending[0].offset, quint64(40));
    QCOMPARE(pending[0].status, QStringLiteral("uploading"));

    QVERIFY(store.updatePendingUpload(QStringLiteral("upload-1"), 100,
                                      QStringLiteral("paused")));
    pending = store.pendingUploads();
    QCOMPARE(pending[0].offset, quint64(100));
    QCOMPARE(pending[0].status, QStringLiteral("paused"));

    QVERIFY(store.removePendingUpload(QStringLiteral("upload-1")));
    QVERIFY(store.pendingUploads().isEmpty());
    store.close();
}

void TestClientCore::historyStoreFileTaskRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY2(store.open(dir.path(), 22, &error), qPrintable(error));

    chatbox::client::HistoryStore::FileTask task;
    task.file_id = 77;
    task.context = chatbox::client::ChatContext::kPrivate;
    task.target_id = 5;
    task.message_id = 91;
    task.name = QStringLiteral("resume.bin");
    task.mime = QStringLiteral("application/octet-stream");
    task.save_path = QStringLiteral("C:/Downloads/ChatBox/resume.bin.part");
    task.state = QStringLiteral("downloading");
    task.received = 256;
    task.total = 1024;
    QVERIFY(store.saveFileTask(task));

    QVector<chatbox::client::HistoryStore::FileTask> tasks =
        store.pendingFileTasks();
    QCOMPARE(tasks.size(), 1);
    QCOMPARE(tasks[0].file_id, quint64(77));
    QCOMPARE(tasks[0].save_path, task.save_path);
    QCOMPARE(tasks[0].received, quint64(256));

    QVERIFY(store.updateFileTask(77, chatbox::client::ChatContext::kPrivate,
                                 5, QStringLiteral("paused"), 512, 1024));
    tasks = store.pendingFileTasks();
    QCOMPARE(tasks[0].received, quint64(512));
    QCOMPARE(tasks[0].state, QStringLiteral("paused"));

    QVERIFY(store.removeFileTask(77, chatbox::client::ChatContext::kPrivate,
                                 5));
    QVERIFY(store.pendingFileTasks().isEmpty());
    store.close();
}

void TestClientCore::messageStoreConfirmsFileUpload() {
    chatbox::client::MessageStore store;
    store.reset(chatbox::client::ChatContext::kRoom, 3);
    chatbox::client::ChatMessage card;
    card.client_msg_id = QStringLiteral("upload-task");
    card.context = chatbox::client::ChatContext::kRoom;
    card.target_id = 3;
    card.sender_id = 1;
    card.content = QStringLiteral("[文件] a.txt");
    card.outgoing = true;
    card.status = chatbox::client::MessageStatus::kSending;
    card.type = chatbox::client::MessageType::kFile;
    card.file.name = QStringLiteral("a.txt");
    card.file.size = 100;
    card.transfer_state = QStringLiteral("uploading");
    card.transfer_progress = 40;
    store.addPending(card);
    QVERIFY(store.updateFileTransfer(QStringLiteral("upload-task"),
                                     QStringLiteral("uploading"), 70));
    QCOMPARE(store.findByClientId(QStringLiteral("upload-task"))
                 ->transfer_progress,
             quint64(70));
    QVERIFY(store.confirmFileUpload(
        QStringLiteral("upload-task"), 12, 34,
        chatbox::client::parseServerUtc(QStringLiteral("2026-10-28 10:00:00"))));
    const chatbox::client::ChatMessage* confirmed =
        store.findByClientId(QStringLiteral("upload-task"));
    QVERIFY(confirmed != nullptr);
    QCOMPARE(confirmed->message_id, quint64(12));
    QCOMPARE(confirmed->file.file_id, quint64(34));
    QCOMPARE(confirmed->status, chatbox::client::MessageStatus::kSent);
    QCOMPARE(confirmed->transfer_state, QStringLiteral("done"));
}

void TestClientCore::rendererFileCardShowsProgress() {
    chatbox::client::ChatMessage message;
    message.message_id = 15;
    message.sender_id = 2;
    message.sender_name = QStringLiteral("B");
    message.content = QStringLiteral("[文件] report.pdf");
    message.created_at_utc =
        chatbox::client::parseServerUtc(QStringLiteral("2026-10-28 10:00:00"));
    message.type = chatbox::client::MessageType::kFile;
    message.file.file_id = 34;
    message.file.name = QStringLiteral("report.pdf");
    message.file.size = 200;
    message.transfer_state = QStringLiteral("uploading");
    message.transfer_progress = 100;
    const QString html = chatbox::client::renderMessageFragment(
        message, 1, false, QString(), false);
    QVERIFY(html.contains(QStringLiteral("report.pdf")));
    QVERIFY(html.contains(QStringLiteral("50%")));
    QVERIFY(html.contains(QStringLiteral("file-open:///15")));

    chatbox::client::ChatMessage pending = message;
    pending.message_id = 0;
    pending.transfer_state = QStringLiteral("queued");
    const QString queued = chatbox::client::renderMessageFragment(
        pending, 1, false, QString(), false);
    QVERIFY(queued.contains(QStringLiteral("report.pdf")));
    QVERIFY(!queued.contains(QStringLiteral("file-open://")));
}

namespace {

bool saveTestThumbnail(quint64 file_id, int width, int height) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (!image.save(&buffer, "JPG")) {
        return false;
    }
    return chatbox::client::saveThumbnail(file_id, bytes);
}

chatbox::client::ChatMessage testFileMessage(quint64 file_id, quint64 message_id,
                                             const QString& mime,
                                             const QString& name,
                                             quint32 duration_ms = 0) {
    chatbox::client::ChatMessage message;
    message.message_id = message_id;
    message.sender_id = 2;
    message.sender_name = QStringLiteral("Bob");
    message.content = QStringLiteral("[文件] ") + name;
    message.created_at_utc =
        chatbox::client::parseServerUtc(QStringLiteral("2026-10-18 12:00:00"));
    message.type = chatbox::client::MessageType::kFile;
    message.file.file_id = file_id;
    message.file.name = name;
    message.file.size = 200000;
    message.file.mime = mime;
    message.file.thumb_available = true;
    message.file.duration_ms = duration_ms;
    return message;
}

}  // namespace

void TestClientCore::rendererShowsLargeImagePreview() {
    const quint64 file_id = 990001;
    QVERIFY(saveTestThumbnail(file_id, 320, 640));

    const chatbox::client::ChatMessage message =
        testFileMessage(file_id, 77, QStringLiteral("image/jpeg"),
                        QStringLiteral("photo.jpg"));
    const QString html = chatbox::client::renderConversationHtml({message}, 1);

    QVERIFY(html.contains(QStringLiteral("file-open:///77")));
    QVERIFY(html.contains(QStringLiteral("width=\"240\" height=\"480\"")));
    QVERIFY(!html.contains(QStringLiteral("width=\"220\"")));
    QVERIFY(html.contains(
        QString::fromLatin1(chatbox::client::theme::kSurfaceStrong)));
    QFile::remove(chatbox::client::thumbnailCachePath(file_id));
}

void TestClientCore::rendererShowsVideoPreviewWithPlayMarker() {
    const quint64 file_id = 990002;
    QVERIFY(saveTestThumbnail(file_id, 320, 180));

    const chatbox::client::ChatMessage message =
        testFileMessage(file_id, 78, QStringLiteral("video/mp4"),
                        QStringLiteral("clip.mp4"), 23000);
    const QString html = chatbox::client::renderConversationHtml({message}, 1);

    QVERIFY(html.contains(QStringLiteral("file-open:///78")));
    QVERIFY(html.contains(QStringLiteral("width=\"320\" height=\"180\"")));
    QVERIFY(html.contains(QStringLiteral("▶ 00:23")));
    QFile::remove(chatbox::client::thumbnailCachePath(file_id));
}

void TestClientCore::rendererFallsBackToFileCardForVideoWithoutThumbnail() {
    const quint64 file_id = 990003;
    QFile::remove(chatbox::client::thumbnailCachePath(file_id));

    const chatbox::client::ChatMessage message =
        testFileMessage(file_id, 79, QStringLiteral("video/mp4"),
                        QStringLiteral("movie.mp4"), 5000);
    const QString html = chatbox::client::renderConversationHtml({message}, 1);

    QVERIFY(!html.contains(QStringLiteral("<img")));
    QVERIFY(html.contains(QStringLiteral("movie.mp4")));
    QVERIFY(html.contains(QStringLiteral("file-open:///79")));
}

void TestClientCore::rendererGroupedBubblesAlign() {
    const QDateTime created = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    chatbox::client::ChatMessage first;
    first.message_id = 1;
    first.sender_id = 2;
    first.sender_name = "Bob";
    first.content = "first";
    first.created_at_utc = created;
    chatbox::client::ChatMessage second = first;
    second.message_id = 2;
    second.content = "a much longer continuation body";
    second.created_at_utc = created.addSecs(30);

    chatbox::client::AvatarSrcMap srcs;
    srcs.insert(2, QStringLiteral("chatbox-avatar://2?v=1"));
    QHash<QString, QImage> images;
    QImage avatar(36, 36, QImage::Format_ARGB32);
    avatar.fill(Qt::green);
    images.insert(QStringLiteral("chatbox-avatar://2?v=1"), avatar);

    QVector<chatbox::client::MessageKey> keys;
    const QString html =
        chatbox::client::renderConversationHtml({first, second}, 99, &keys, srcs);
    ChatView view;
    view.resize(640, 400);
    view.show();
    QTest::qWait(30);
    view.setConversationHtml(html, keys, images);

    const QRect first_bubble = view.messageBubbleRect(1);
    const QRect second_bubble = view.messageBubbleRect(2);
    QVERIFY(!first_bubble.isNull());
    QVERIFY(!second_bubble.isNull());
    QCOMPARE(second_bubble.left(), first_bubble.left());
}

void TestClientCore::historyStorePreservesFileMediaDimensions() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY2(store.open(dir.path(), 1, &error), qPrintable(error));

    chatbox::client::ChatMessage message;
    message.message_id = 90;
    message.context = chatbox::client::ChatContext::kRoom;
    message.target_id = 3;
    message.sender_id = 2;
    message.sender_name = "Bob";
    message.content = "[文件] clip.mp4";
    message.created_at_utc =
        chatbox::client::parseServerUtc("2026-10-28 10:00:00");
    message.type = chatbox::client::MessageType::kFile;
    message.file.file_id = 44;
    message.file.name = "clip.mp4";
    message.file.size = 200000;
    message.file.mime = "video/mp4";
    message.file.thumb_available = true;
    message.file.width = 1080;
    message.file.height = 1920;
    message.file.duration_ms = 23000;
    QVERIFY(store.saveMessage(message));
    store.close();

    QVERIFY2(store.open(dir.path(), 1, &error), qPrintable(error));
    const QVector<chatbox::client::ChatMessage> loaded =
        store.recent(chatbox::client::ChatContext::kRoom, 3, 10);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().file.width, quint32(1080));
    QCOMPARE(loaded.first().file.height, quint32(1920));
    QCOMPARE(loaded.first().file.duration_ms, quint32(23000));
    QVERIFY(loaded.first().file.thumb_available);
    store.close();
}

void TestClientCore::thumbnailCacheUsesVersionedPath() {
    const quint64 file_id = 990010;
    QVERIFY(QDir().mkpath(chatbox::client::thumbnailCacheDirectory()));
    const QString legacy = QDir(chatbox::client::thumbnailCacheDirectory())
                               .filePath(QString::number(file_id) +
                                         QStringLiteral(".jpg"));
    QFile old_file(legacy);
    QVERIFY(old_file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    old_file.write("old");
    old_file.close();

    QVERIFY(chatbox::client::thumbnailCachePath(file_id)
                .endsWith(QStringLiteral("990010-v2.jpg")));
    QVERIFY(saveTestThumbnail(file_id, 32, 32));
    QVERIFY(!QFile::exists(legacy));
    QVERIFY(QFile::exists(chatbox::client::thumbnailCachePath(file_id)));
    QFile::remove(chatbox::client::thumbnailCachePath(file_id));
}

void TestClientCore::historyStoreFileDeleteIsConversationScoped() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    chatbox::client::HistoryStore store;
    QString error;
    QVERIFY2(store.open(dir.path(), 33, &error), qPrintable(error));
    chatbox::client::ChatMessage room;
    room.message_id = 1;
    room.context = chatbox::client::ChatContext::kRoom;
    room.target_id = 1;
    room.sender_id = 2;
    room.content = QStringLiteral("room file");
    room.type = chatbox::client::MessageType::kFile;
    room.file.file_id = 10;
    room.file.name = QStringLiteral("a.txt");
    room.file.size = 10;
    QVERIFY(store.saveMessage(room));
    chatbox::client::ChatMessage private_chat = room;
    private_chat.context = chatbox::client::ChatContext::kPrivate;
    private_chat.target_id = 2;
    private_chat.content = QStringLiteral("private file");
    private_chat.file.file_id = 11;
    QVERIFY(store.saveMessage(private_chat));

    QVERIFY(store.markFileDeleted(chatbox::client::ChatContext::kRoom, 1, 1));
    const QVector<chatbox::client::ChatMessage> room_rows =
        store.recent(chatbox::client::ChatContext::kRoom, 1, 10);
    QCOMPARE(room_rows.size(), 1);
    QCOMPARE(room_rows[0].content,
             QStringLiteral("\u6587\u4ef6\u5df2\u5220\u9664"));
    QCOMPARE(room_rows[0].type, chatbox::client::MessageType::kText);
    const QVector<chatbox::client::ChatMessage> private_rows =
        store.recent(chatbox::client::ChatContext::kPrivate, 2, 10);
    QCOMPARE(private_rows.size(), 1);
    QCOMPARE(private_rows[0].content, QStringLiteral("private file"));
    QCOMPARE(private_rows[0].type, chatbox::client::MessageType::kFile);
    store.close();
}

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

// The main socket uses OpenSSL 4 directly, but Qt 5.15.2's QSslSocket (file
// transfer) loads OpenSSL 1.1 by name. A portable package needs both stacks.
void TestClientCore::buildScriptShipsQtOpenSslForFileTransfer() {
    const QString build = readClientSource(QStringLiteral("build_client.ps1"));
    QVERIFY2(!build.isEmpty(), "build_client.ps1 must be readable");
    QVERIFY(build.contains(QStringLiteral("libssl-1_1-x64.dll")));
    QVERIFY(build.contains(QStringLiteral("libcrypto-1_1-x64.dll")));

    const QString package = readClientSource(QStringLiteral("package_client.ps1"));
    QVERIFY2(!package.isEmpty(), "package_client.ps1 must be readable");
    QVERIFY(package.contains(QStringLiteral("libssl-1_1-x64.dll")));
    QVERIFY(package.contains(QStringLiteral("libcrypto-1_1-x64.dll")));
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

// 生产服务器已固定；登录页不再给用户改 IP/端口/TLS 的入口。
void TestClientCore::loginPageHasNoServerSettings() {
    LoginPage page;
    QVERIFY(page.findChild<QToolButton*>(QStringLiteral("AdvancedOptionsButton")) == nullptr);
    QVERIFY(page.findChild<QWidget*>(QStringLiteral("AdvancedOptionsPanel")) == nullptr);
    QVERIFY(page.findChild<QLineEdit*>(QStringLiteral("ServerHostEdit")) == nullptr);
    QVERIFY(page.findChild<QSpinBox*>(QStringLiteral("ServerPortSpin")) == nullptr);
    QVERIFY(page.findChild<QCheckBox*>(QStringLiteral("ServerTlsCheck")) == nullptr);
    QCOMPARE(QString::fromLatin1(chatbox::client::kServerHost),
             QStringLiteral("121.5.168.189"));
    QCOMPARE(chatbox::client::kServerPort, quint16(9443));
    QVERIFY(chatbox::client::kServerTls);
}

void TestClientCore::layoutWeightsFollowAudit() {
    const QString source = readMainWindowSource();
    QVERIFY2(!source.isEmpty(), "ui/main_window.cpp must be readable");
    QVERIFY(!source.contains(QStringLiteral("room_list_->setFixedHeight(58)")));
    QVERIFY(source.contains(QStringLiteral("group_list_->setMinimumHeight(120)")));
    QVERIFY(source.contains(QStringLiteral("group_list_->setSizeAdjustPolicy")));
    QVERIFY(source.contains(QStringLiteral("groups_layout->addWidget(group_list_, 1)")));
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
    QVERIFY(source.contains(QStringLiteral("setMinimumWidth(200)")));
    QVERIFY(source.contains(QStringLiteral("setMaximumWidth(300)")));
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
    auto* username = page.findChild<QLineEdit*>(QStringLiteral("UsernameEdit"));
    auto* password = page.findChild<QLineEdit*>(QStringLiteral("PasswordEdit"));
    auto* nickname = page.findChild<QLineEdit*>(QStringLiteral("NicknameEdit"));
    QVERIFY(username != nullptr);
    QVERIFY(password != nullptr);
    QVERIFY(nickname != nullptr);
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

void TestClientCore::settingsDialogHasFixedServerAddress() {
    SettingsDialog dialog(QStringLiteral("U00001001"), QString(), 0, false);
    QVERIFY(dialog.findChild<QLineEdit*>(QStringLiteral("ServerHostEdit")) == nullptr);
    QVERIFY(dialog.findChild<QSpinBox*>(QStringLiteral("ServerPortSpin")) == nullptr);
    QVERIFY(dialog.findChild<QCheckBox*>(QStringLiteral("ServerTlsCheck")) == nullptr);
    auto* address = dialog.findChild<QLabel*>(QStringLiteral("ServerAddressLabel"));
    auto* appearance =
        dialog.findChild<QComboBox*>(QStringLiteral("AppearanceCombo"));
    auto* buttons = dialog.findChild<QDialogButtonBox*>();
    QVERIFY(address != nullptr);
    QVERIFY(appearance != nullptr);
    QVERIFY(buttons != nullptr);
    QCOMPARE(address->text(), QStringLiteral("121.5.168.189:9443"));
    QCOMPARE(appearance->count(), 3);
    QCOMPARE(buttons->button(QDialogButtonBox::Save)->text(),
             QStringLiteral("保存"));
    QCOMPARE(buttons->button(QDialogButtonBox::Cancel)->text(),
             QStringLiteral("取消"));
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
void TestClientCore::fileManagerDialogBuilds() {
    FileManagerDialog dialog(1, 1, true);
    QVector<chatbox::client::FileEntryView> entries(1);
    entries[0].file_id = 9;
    entries[0].name = QStringLiteral("a.txt");
    entries[0].size = 12;
    entries[0].uploader_name = QStringLiteral("A");
    entries[0].can_delete = true;
    dialog.setFiles(1, 12, entries);
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("FileList"));
    QVERIFY(list != nullptr);
    QCOMPARE(list->count(), 1);
    QVERIFY(dialog.findChild<QPushButton*>(
                QStringLiteral("FileDownloadPauseButton")) != nullptr);
    dialog.setBusy(false);
}

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

// 设置页必须提供检查更新入口，不能只藏在托盘菜单里。
void TestClientCore::settingsDialogOffersUpdateCheck() {
    SettingsDialog dialog(QStringLiteral("U00001001"), QString(), 0, false);
    auto* check = dialog.findChild<QPushButton*>(
        QStringLiteral("CheckUpdateButton"));
    QVERIFY2(check != nullptr, "settings must expose a check-update action");
    QVERIFY(check->text().contains(QStringLiteral("检查更新")));
}

void TestClientCore::settingsDialogOffersCloseToTray() {
    QSettings settings;
    const QVariant previous = settings.value("settings/close_to_tray");
    settings.setValue("settings/close_to_tray", true);
    SettingsDialog dialog(QStringLiteral("U00001001"), QString(), 0, false);
    auto* check =
        dialog.findChild<QCheckBox*>(QStringLiteral("CloseToTrayCheck"));
    QVERIFY2(check != nullptr, "settings must expose the close-to-tray option");
    QVERIFY(check->isChecked());
    QVERIFY(check->text().contains(QStringLiteral("托盘")));
    if (previous.isValid()) {
        settings.setValue("settings/close_to_tray", previous);
    } else {
        settings.remove("settings/close_to_tray");
    }
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

void TestClientCore::messageMenuKeepsReplyAndRecall() {
    const QString source = readClientSource(QStringLiteral("ui/main_window_chat.cpp"));
    const QString menu = sourceSection(source, QStringLiteral("void MainWindow::showMessageMenu("),
                                       QStringLiteral("void MainWindow::copyMessageText("));
    QVERIFY2(menu.contains(QStringLiteral("\u56de\u590d")),
             "right-click menu must keep reply");
    QVERIFY2(menu.contains(QStringLiteral("\u64a4\u56de")),
             "right-click menu must keep recall");
    QVERIFY2(menu.contains(QStringLiteral("\u91cd\u53d1")),
             "right-click menu must keep retry");
    QVERIFY2(menu.contains(QStringLiteral("\u590d\u5236")),
             "right-click menu must keep copy");
}

QTEST_MAIN(TestClientCore)
#include "test_client_core.moc"
