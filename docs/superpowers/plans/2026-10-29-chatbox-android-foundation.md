# ChatBox Android 底座 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 交付一个能安装的 Android APK：service 进程持有 TLS 连接、同步和通知，UI 进程显示登录/会话列表/1:1 文本聊天；为后续完整手机 UI 留好 IPC 和存储边界。

**Architecture:** `chatbox/clientcore` 是 Qt/C++ 的共享核心（transport、session、store、sync、IPC），Android service 用 `QtService` 跑无界面核心，UI 用 Qt Quick/QML 通过 `QLocalSocket` 取快照和事件。前台服务类型 `remoteMessaging` 保持连接并弹通知。桌面端不在本计划内，桌面适配见服务端计划。

**Tech Stack:** Qt 6.8 LTS、Qt Quick/QML、Qt Network/QSslSocket、Qt SQL/SQLite、Android SDK 35、NDK r26、JDK 17、CMake、Java/Kotlin 胶水、PowerShell 构建脚本。

**Spec:** `docs/superpowers/specs/2026-10-29-chatbox-android-foundation-design.md`

**Prerequisite:** 服务端计划 `docs/superpowers/plans/2026-10-29-chatbox-multidevice-server.md` 的 Task 1（协议编解码）必须先完成；Task 4/6/7/8 完成前，Android 只能做 F0 工具链和本地 IPC 验证。

## Global Constraints

- C++17；4 空格缩进，禁止 Tab；K&R 大括号；头文件 `#ifndef/#define/#endif`。
- Qt 6.8 LTS，锁定具体 6.8.x 补丁；`minSdk 28`（Qt 6.8 要求）、`targetSdk 35`、`compileSdk 35`；release 只出 `arm64-v8a`。
- 协议帧、命令值、能力位以 `chatbox/common/protocol.h` 和 `session_protocol.h` 为准；客户端不得自定义第二套协议。
- service 进程是唯一持有服务端连接的进程；UI 进程不得直接创建 `QSslSocket`。
- 所有 Android 敏感数据（token、device_id）走 Keystore；日志不得打印 token、密码、消息正文。
- 通知默认显示内容预览；常驻通知和消息通知分渠道；Android 13+ 必须处理 `POST_NOTIFICATIONS`。
- 前台服务类型 `remoteMessaging`；`START_STICKY`；开机自启尽力而为，不承诺所有 ROM。
- 构建脚本必须可复现；签名 keystore 不提交仓库。
- 真机验收至少覆盖 Android 8/10/13/14/15 和一台国产 ROM。

## Review Focus

- 划掉任务、锁屏、重启手机后，service 是否仍能收消息并通知；不能收时是否明确显示“连接已断开”。
- UI 进程和 service 进程断开/重连时，不能重复登录、不能产生第二条服务端会话。
- 同一账号手机和电脑同时在线时，手机发消息、电脑读消息、手机未读角标是否一致。
- 终止错误（1002/1010/1011）必须清 token 回登录页，不能指数退避重连。
- 通知点击必须打开正确会话；当前正在看的会话不能重复弹通知。

---

## Task 1: 工具链与 Hello QML APK（F0）

**Files:**
- Create: `chatbox/mobile/CMakeLists.txt`
- Create: `chatbox/mobile/src/main.cpp`
- Create: `chatbox/mobile/qml/Main.qml`
- Create: `chatbox/mobile/android/AndroidManifest.xml`
- Create: `chatbox/mobile/build_android.ps1`
- Create: `chatbox/mobile/README.md`

**Interfaces:**
- Produces: 可安装的 `chatbox-mobile-debug.apk`；包名 `com.chatbox.mobile`；应用名 `ChatBox`。

- [ ] **Step 1: 安装并锁定工具链**

```powershell
python -m pip install aqtinstall
$QtRoot = "D:\Qt"
python -m aqt install-qt windows android 6.8.3 android_arm64_v8a -m qtdeclarative qtquickcontrols2 qtsvg qtimageformats qtshadertools qtsql -O $QtRoot
python -m aqt install-qt windows android 6.8.3 android_x86_64  -m qtdeclarative qtquickcontrols2 qtsvg qtimageformats qtshadertools qtsql -O $QtRoot
```

安装 JDK 17、Android command-line tools，然后：

```powershell
sdkmanager "platform-tools" "platforms;android-35" "build-tools;35.0.0" "ndk;26.1.10909125"
```

设置 `JAVA_HOME`、`ANDROID_SDK_ROOT`、`ANDROID_NDK_ROOT`。如果 6.8.3 不可用，选可用的最新 6.8.x，并把实际版本写进 `README.md`；不要混用 6.7/6.9。

- [ ] **Step 2: 写最小 CMake 和 QML**

`CMakeLists.txt` 使用 `qt_add_executable`、`qt_add_qml_module`，设置：

```cmake
set_target_properties(chatbox_mobile PROPERTIES
    QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
    QT_ANDROID_MIN_SDK_VERSION 26
    QT_ANDROID_TARGET_SDK_VERSION 35
    QT_ANDROID_PACKAGE_NAME "com.chatbox.mobile"
)
```

`Main.qml` 只显示“ChatBox Android”和 Qt 版本；`main.cpp` 加载 QML。

- [ ] **Step 3: 写自定义 Manifest**

在 `android/AndroidManifest.xml` 保留 QtActivity，设置 `android:label="ChatBox"`、`android:allowBackup="false"`、`android:usesCleartextTraffic="false"`。先不加 service。

- [ ] **Step 4: 写 `build_android.ps1`**

脚本检查 `qt-cmake.bat`、`adb`、`JAVA_HOME`；配置 `build-android`；执行 `cmake --build --target apk`；输出 APK 路径。示例：

```powershell
& "$QtRoot\6.8.3\android_arm64_v8a\bin\qt-cmake.bat" -S . -B build-android `
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Debug
cmake --build build-android --target apk -j 8
```

- [ ] **Step 5: 构建并安装**

Run: `cd chatbox\mobile; .\build_android.ps1`
Expected: 生成 APK，无 CMake/Gradle 错误。

Run: `adb install -r <apk路径>; adb shell am start -n com.chatbox.mobile/org.qtproject.qt.android.bindings.QtActivity`
Expected: 手机/模拟器显示“ChatBox Android”。

- [ ] **Step 6: 提交**

```bash
git add chatbox/mobile
git commit -m "feat(android): add Qt 6 Android hello project"
```

---

## Task 2: TLS + PING 冒烟（F0）

**Files:**
- Create: `chatbox/clientcore/CMakeLists.txt`
- Create: `chatbox/clientcore/transport/transport.h`
- Create: `chatbox/clientcore/transport/qt_ssl_transport.h`
- Create: `chatbox/clientcore/transport/qt_ssl_transport.cpp`
- Create: `chatbox/mobile/certs/ca.crt`
- Modify: `chatbox/mobile/src/main.cpp`
- Modify: `chatbox/mobile/qml/Main.qml`
- Modify: `chatbox/mobile/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct TransportConfig { QString host; quint16 port=0; bool use_tls=true; QString ca_pem; }`
  - `class Transport : public QObject { virtual bool start(const TransportConfig&, QString* error)=0; virtual void stop()=0; virtual bool sendFrame(const QByteArray&, QString* error)=0; virtual bool isConnected() const=0; signals: connected(); disconnected(QString); frameReceived(QByteArray); }`
  - `class QtSslTransport : public Transport` — worker 线程内用 `QSslSocket`，校验 CA 和 peer name，按 14 字节帧头拆帧

- [ ] **Step 1: 复制 CA**

```powershell
Copy-Item chatbox\client\certs\ca.crt chatbox\mobile\certs\ca.crt
```

在 CMake 里把 `certs/ca.crt` 作为资源/资产打包，运行时读入 `QString ca_pem`。

- [ ] **Step 2: 准备 Android OpenSSL 3**

Qt 6 Android 的 `QSslSocket` 需要随包提供 OpenSSL 3。用 Qt 官方 `android_openssl` 预编译产物，或按 NDK r26 自行编译 `libssl_3.so` / `libcrypto_3.so`，放到 `chatbox/mobile/android/libs/arm64-v8a/`，并在 CMake 中用 `QT_ANDROID_EXTRA_LIBS` 打包。若 F0 发现 Qt 6.8 已自带可用 TLS 后端，记录实际路径并跳过自编译；不要依赖 Android 系统信任库。

- [ ] **Step 3: 实现 transport**

`QtSslTransport::start` 在 worker `QThread` 里 `connectToHostEncrypted(host, port)`；`QSslConfiguration` 加入 CA；`setPeerVerifyName(host)`；`readyRead` 拆帧；`sendFrame` 用 `QMetaObject::invokeMethod` 投递到 worker。帧头用 `chatbox::decodeHeader/validateHeader`，body 上限 `kMaxOutFrameBody`（同步响应可能到 512 KiB）。

- [ ] **Step 4: 在 QML 加“连接测试”按钮**

点击后连接 `121.5.168.189:9443`，发送 `kCmdPing`（seq=1），等待 `kCmdPong`；状态文本显示 `PONG` 或错误。不要在这一步做登录。

- [ ] **Step 5: 构建、安装、验证**

Run: `.\build_android.ps1; adb install -r <apk>`
Run: `adb logcat -s ChatBox`
Expected: 点击按钮后日志出现 `TLS connected`、`PONG received`；锁屏后连接不断（此时还没有前台服务，验证 TLS 即可）。

- [ ] **Step 6: 提交**

```bash
git add chatbox/clientcore chatbox/mobile
git commit -m "feat(android): add TLS transport and ping smoke"
```

---

## Task 3: QtService + QLocalSocket + 通知冒烟（F0）

**Files:**
- Create: `chatbox/mobile/android/src/com/chatbox/mobile/ChatBoxService.java`
- Create: `chatbox/mobile/android/src/com/chatbox/mobile/ChatBoxNotifications.java`
- Create: `chatbox/mobile/android/src/com/chatbox/mobile/ChatBoxBootReceiver.java`
- Create: `chatbox/mobile/src/service_app.h`
- Create: `chatbox/mobile/src/service_app.cpp`
- Create: `chatbox/clientcore/ipc/ipc_frame.h`
- Create: `chatbox/clientcore/ipc/ipc_frame.cpp`
- Create: `chatbox/clientcore/ipc/ipc_server.h`
- Create: `chatbox/clientcore/ipc/ipc_server.cpp`
- Modify: `chatbox/clientcore/CMakeLists.txt`
- Modify: `chatbox/mobile/src/main.cpp`
- Modify: `chatbox/mobile/android/AndroidManifest.xml`
- Modify: `chatbox/mobile/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `QByteArray encodeIpcFrame(const QJsonObject&)`
  - `bool decodeIpcFrame(QByteArray& buffer, QJsonObject& out, QString* error)` — 4 字节大端长度 + UTF-8 JSON
  - `class IpcServer : public QObject { bool listen(const QString& name, QString* error); void sendEvent(const QJsonObject&); signal requestReceived(quint64 id, QJsonObject request); }`
  - Java `ChatBoxService extends QtService`：`onCreate` 创建渠道并 `startForeground`，`onStartCommand` 返回 `START_STICKY`，`onTaskRemoved` 不停止
  - Java `ChatBoxNotifications`：`createChannels(Context)`、`startForeground(Service)`、`notifyTest(Context)`

- [ ] **Step 1: 写 Manifest service/receiver**

```xml
<uses-permission android:name="android.permission.INTERNET"/>
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
<uses-permission android:name="android.permission.FOREGROUND_SERVICE"/>
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_REMOTE_MESSAGING"/>
<uses-permission android:name="android.permission.POST_NOTIFICATIONS"/>
<uses-permission android:name="android.permission.RECEIVE_BOOT_COMPLETED"/>

<service android:name=".ChatBoxService" android:process=":qt_service"
         android:exported="false" android:foregroundServiceType="remoteMessaging">
  <meta-data android:name="android.app.lib_name" android:value="-- %%INSERT_APP_LIB_NAME%% --"/>
  <meta-data android:name="android.app.arguments" android:value="-service"/>
  <meta-data android:name="android.app.background_running" android:value="true"/>
</service>
<receiver android:name=".ChatBoxBootReceiver" android:exported="true">
  <intent-filter><action android:name="android.intent.action.BOOT_COMPLETED"/></intent-filter>
</receiver>
```

- [ ] **Step 2: 实现 Java service 和通知**

用平台 `NotificationChannel`/`Notification.Builder`（minSdk 28，不依赖 AndroidX）。`ChatBoxService.onCreate` 创建 `chatbox_service`（低优先级常驻）和 `chatbox_messages`（高优先级）渠道；`startForeground` 用低优先级通知。`ChatBoxBootReceiver` 用 `startForegroundService` 并 catch `ForegroundServiceStartNotAllowedException`（API 31+）。

- [ ] **Step 3: 实现 C++ service 分支**

`main.cpp`：

```cpp
if (argc > 1 && std::strcmp(argv[1], "-service") == 0) {
    QAndroidService app(argc, argv);
    chatbox::mobile::ServiceApp service;
    return service.run();
}
QGuiApplication app(argc, argv);
```

`ServiceApp` 起 `IpcServer`，名字用 `QStandardPaths::RuntimeLocation + "/chatbox_service"`，收到 `{"cmd":"ping"}` 回 `{"pong":true}`；同时调用 Java 通知发一条测试通知。CMake 里 `chatbox_mobile` 链接 `Qt6::CorePrivate`（`QAndroidService` 需要），并把 `clientcore` 的 IPC 源文件加入库。

- [ ] **Step 4: UI 连接 IPC 并显示状态**

UI 进程用 `QLocalSocket` 连接同一个 socket 名；连上后发 ping，显示 `service: pong`。UI 启动时用 `startForegroundService` 启动 service。

- [ ] **Step 5: 构建、安装、验证**

Run: `.\build_android.ps1; adb install -r <apk>; adb shell am start ...`
Run: `adb shell dumpsys activity services com.chatbox.mobile`
Expected: `ChatBoxService` 在 `:qt_service` 进程；通知栏有“ChatBox 正在保持连接”；UI 显示 `service: pong`。

- [ ] **Step 6: 提交**

```bash
git add chatbox/mobile chatbox/clientcore/ipc
git commit -m "feat(android): add foreground service, local IPC, and notifications"
```

---

## Task 4: 后台行为矩阵与开机自启（F0）

**Files:**
- Create: `chatbox/mobile/docs/f0-device-matrix.md`
- Modify: `chatbox/mobile/README.md`
- Modify: `chatbox/mobile/android/src/com/chatbox/mobile/ChatBoxService.java`

**Interfaces:**
- Produces: 一份实测记录，列出每台测试机的锁屏、划掉、重启、省电模式结果。

- [ ] **Step 1: 在 service 加连接保活日志**

`onStartCommand` 记录 `START_STICKY` 返回；`onTaskRemoved` 记录“task removed, keep service”；`onDestroy` 记录原因。C++ 侧记录 TCP 断开/重连。

- [ ] **Step 2: 按矩阵测试**

每台设备执行：
1. 启动 app → 锁屏 5 分钟 → 从另一账号发消息 → 是否通知。
2. 划掉最近任务 → 发消息 → 是否通知/service 是否仍在。
3. 重启手机 → 不打开 app → 发消息 → 是否通知（记录 Android 15 限制）。
4. 打开省电模式 → 重复 1。
5. 手动“强行停止”app → 是否还能收到（预期不能，需重新打开）。

- [ ] **Step 3: 记录结果**

`f0-device-matrix.md` 表格：机型、ROM、Android 版本、锁屏、划掉、重启、省电、备注。对失败项写明是否需要电池白名单/自启动设置。

- [ ] **Step 4: 决策**

如果 QtService 不能稳定保活，或 QLocalSocket 跨进程不可用，停止后续任务，按设计 §5.5 的备选方案改架构；把结论写入 matrix 文档再继续。

- [ ] **Step 5: 提交**

```bash
git add chatbox/mobile/docs/f0-device-matrix.md chatbox/mobile/README.md chatbox/mobile/android/src/com/chatbox/mobile/ChatBoxService.java
git commit -m "test(android): record foreground service behavior matrix"
```

---

## Task 5: SessionClient 与协议编解码测试

**Files:**
- Create: `chatbox/clientcore/session/session_client.h`
- Create: `chatbox/clientcore/session/session_client.cpp`
- Create: `chatbox/clientcore/tests/clientcore_tests.pro`
- Create: `chatbox/clientcore/tests/fake_transport.h`
- Create: `chatbox/clientcore/tests/test_session_client.cpp`

**Interfaces:**
- Consumes: Task 1（server plan）`common/session_protocol.h`、Task 2 `Transport`
- Produces:
  - `struct SessionConfig { TransportConfig transport; QString device_id, device_name, platform, app_version; }`
  - `struct LoginResult { quint64 user_id=0; QString nickname, token; quint32 server_caps=0, ttl_seconds=0; }`
  - `class SessionClient : public QObject { void login(QString username, QString password); void resume(QString token); void logout(); void requestSessionList(); void revokeSession(QString session_id); signals: loginSucceeded(LoginResult); loginFailed(int code, QString message); sessionListReceived(QVector<SessionInfo>); sessionRevoked(QString session_id); }`
  - `FakeTransport : public Transport` — capture sent frames、注入响应

- [ ] **Step 1: 写失败测试**

`test_session_client.cpp` 覆盖：`loginSendsDeviceInfoAndCaps`、`resumeSendsTokenAndDeviceInfo`、`parsesAuthExtras`、`legacyAuthResponseLeavesCapsZero`、`sessionListRoundTrip`、`revokeSendsSessionId`。用 `FakeTransport` 检查发出去的 body 字节；用 `QSignalSpy` 检查信号。

- [ ] **Step 2: 写 qmake 测试工程并运行**

```qmake
QT += core network testlib
CONFIG += console c++17
TARGET = clientcore_tests
SOURCES += test_session_client.cpp ../session/session_client.cpp ../../common/protocol.cpp ../../common/session_protocol.cpp ../../common/group_protocol.cpp ../../common/file_protocol.cpp
INCLUDEPATH += ../.. ../../..
```

Run: `qmake; mingw32-make -f Makefile.Release -j8; .\release\clientcore_tests.exe`
Expected: 编译失败/测试失败。

- [ ] **Step 3: 实现 SessionClient**

登录 body：`username,password` + `appendDeviceInfo`；resume：`token` + `appendDeviceInfo`。caps 固定包含 `kCapMultiDevice|kCapSyncPull|kCapReadStatePush|kCapMessageEcho|kCapSessionManage|kCapDeviceMetadata`。解析响应时先读现有字段，再用 `readAuthExtras`；尾部不足按 0 处理。`next_seq_` 单增；忽略未知推送。

- [ ] **Step 4: 运行测试**

Expected: all tests pass。

- [ ] **Step 5: 提交**

```bash
git add chatbox/clientcore/session chatbox/clientcore/tests
git commit -m "feat(clientcore): add session client with device metadata"
```

---

## Task 6: MobileStore（SQLite）

**Files:**
- Create: `chatbox/clientcore/store/mobile_store.h`
- Create: `chatbox/clientcore/store/mobile_store.cpp`
- Create: `chatbox/clientcore/tests/test_mobile_store.cpp`
- Modify: `chatbox/clientcore/tests/clientcore_tests.pro`

**Interfaces:**
- Produces:
  - `struct ConversationRow { int kind; quint64 target_id; QString title, last_content, last_time; quint64 last_message_id=0; quint32 unread=0; }`
  - `struct MessageRow { int kind; quint64 target_id, message_id, sender_id, reply_to_id, file_id; QString client_msg_id, nickname, content, created_at, reply_sender_name, reply_content; bool recalled; quint8 msg_type; }`
  - `struct OutboxRow { QString client_msg_id, content, created_at, state; int kind; quint64 target_id; int retry_count=0; }`
  - `class MobileStore { bool open(QString path, QString* error); void close(); bool upsertConversation(const ConversationRow&, QString*); bool saveMessage(const MessageRow&, QString*); bool loadRecentMessages(int kind, quint64 target_id, int limit, QVector<MessageRow>&, QString*); bool enqueueOutbox(const OutboxRow&, QString*); bool markOutboxSent(QString client_msg_id, quint64 message_id, QString*); bool pendingOutbox(QVector<OutboxRow>&, QString*); bool setMeta(QString key, QString value, QString*); QString meta(QString key) const; }`

- [ ] **Step 1: 写失败测试**

覆盖：`schemaCreated`、`conversationUpsert`、`messageDedupeByMessageId`、`recentMessagesAscending`、`outboxLifecycle`、`metaRoundTrip`、`recalledMessageContentCleared`。使用 `QTemporaryDir`。

- [ ] **Step 2: 加入测试工程并确认失败**

Run: `qmake; mingw32-make; .\release\clientcore_tests.exe`
Expected: 编译失败，`MobileStore` 不存在。

- [ ] **Step 3: 实现 schema 与方法**

表：`conversations(kind,target_id,title,last_message_id,last_content,last_time,unread,updated_at, PRIMARY KEY(kind,target_id))`、`messages(kind,target_id,message_id,client_msg_id,sender_id,nickname,content,created_at,reply_to_id,reply_sender_name,reply_content,recalled,msg_type,file_id, PRIMARY KEY(kind,target_id,message_id))`、`outbox(client_msg_id PRIMARY KEY,kind,target_id,content,created_at,state,retry_count)`、`meta(key PRIMARY KEY,value)`。开 WAL；所有 SQL 用预处理/绑定。

- [ ] **Step 4: 运行测试**

Expected: all tests pass。

- [ ] **Step 5: 提交**

```bash
git add chatbox/clientcore/store chatbox/clientcore/tests
git commit -m "feat(clientcore): add mobile SQLite store"
```

---

## Task 7: ChatService 编排（登录、推送、发送、同步）

**Files:**
- Create: `chatbox/clientcore/service/chat_service.h`
- Create: `chatbox/clientcore/service/chat_service.cpp`
- Create: `chatbox/clientcore/secure/secure_store.h`
- Create: `chatbox/clientcore/tests/test_secure_store.cpp`
- Create: `chatbox/clientcore/tests/test_chat_service.cpp`
- Modify: `chatbox/clientcore/tests/clientcore_tests.pro`

**Interfaces:**
- Consumes: Task 5 `SessionClient`、Task 6 `MobileStore`、server plan Task 1/8 协议
- Produces:
  - `class SecureStore { virtual QString loadToken()=0; virtual void saveToken(const QString&)=0; virtual void clearToken()=0; virtual QString loadDeviceId()=0; virtual void saveDeviceId(const QString&)=0; }`
  - `class MemorySecureStore : public SecureStore` — 测试用
  - `struct ServiceConfig { SessionConfig session; QString db_path; SecureStore* secure_store=nullptr; }`
  - `class ChatService : public QObject { bool start(const ServiceConfig&, QString* error); void stop(); void login(QString username, QString password); void logout(); void sendText(int kind, quint64 target_id, QString content); void markRead(int kind, quint64 target_id, quint64 message_id); void setActiveConversation(int kind, quint64 target_id); QJsonObject snapshot() const; signals: stateChanged(QString state, QString detail); snapshotChanged(QJsonObject); eventReceived(QJsonObject); }`

- [ ] **Step 1: 写失败测试**

`test_secure_store.cpp` 覆盖 `memoryStoreRoundTrip`；`test_chat_service.cpp` 用 `FakeTransport` + `QTemporaryDir` + `MemorySecureStore`：
- `loginStoresProfileAndRequestsLists`：注入登录成功，断言发出 `GROUP_LIST`/`PRIVATE_CONVERSATIONS`/`FRIEND_LIST`/`PROFILE_GET`。
- `loginSavesTokenAndResumeUsesIt`：登录成功后 secure store 有 token；重启 ChatService 调 `start` 发 RESUME。
- `terminalErrorClearsToken`：注入 1002，断言状态 `logged_out`，secure store token 清空，不再重连。
- `incomingPrivatePushPersistsAndEmits`：注入 `kCmdPrivatePush`，断言 store 有消息、`eventReceived` 有消息、conversation 更新。
- `sendTextCreatesOutboxAndFrame`：调用 `sendText`，断言 outbox 行、`kCmdPrivateSend` 帧、client_msg_id 幂等。
- `sendAckMarksOutboxSent`：注入 `kCmdPrivateSendResp`，断言 outbox 清空、消息 state=sent。
- `readStatePushUpdatesUnread`：注入 `kCmdReadStatePush`，断言未读清 0。
- `syncPullAppliesAfterCursor`：注入 `kCmdSyncResp`，断言消息按 ID 升序、无重复。

- [ ] **Step 2: 运行测试确认失败**

Run: `.\release\clientcore_tests.exe`
Expected: 编译失败/新测试失败。

- [ ] **Step 3: 实现状态机**

状态：`disconnected → connecting → authenticating → syncing → online → reconnecting → logged_out`。`start()` 从 `SecureStore` 读 token：有 token 先 RESUME，无 token 等 UI 登录；登录成功保存 token，LOGOUT/终止错误清 token。网络断开用 1/2/4/8/16/30 秒退避；1002/1010/1011 直接 `logged_out`。同步顺序：登录成功 → 拉列表 → 对本地 cursor>0 的会话发 `SYNC_PULL` → 最近页合并 → online。push 到达先落库再发事件；发送先写 outbox 再发帧。

- [ ] **Step 4: 运行测试**

Expected: all tests pass。

- [ ] **Step 5: 提交**

```bash
git add chatbox/clientcore/service chatbox/clientcore/tests
git commit -m "feat(clientcore): orchestrate auth, sync, outbox, and pushes"
```

---

## Task 8: IPC JSON 协议与本地 socket

**Files:**
- Create: `chatbox/clientcore/ipc/ipc_protocol.h`
- Create: `chatbox/clientcore/ipc/ipc_protocol.cpp`
- Create: `chatbox/clientcore/ipc/ipc_client.h`
- Create: `chatbox/clientcore/ipc/ipc_client.cpp`
- Create: `chatbox/clientcore/tests/test_ipc.cpp`
- Modify: `chatbox/clientcore/tests/clientcore_tests.pro`

**Interfaces:**
- Produces:
  - `QJsonObject ipcOk(quint64 id, QJsonObject data)`
  - `QJsonObject ipcError(quint64 id, int code, QString message)`
  - `bool decodeIpcRequest(const QJsonObject&, QString& cmd, QJsonObject& args, int& code, QString& error)`
  - `class IpcClient : public QObject { void connectToService(QString name); void request(QString cmd, QJsonObject args); signals: connectedChanged(bool); responseReceived(quint64 id, QJsonObject); eventReceived(QString event, QJsonObject data); }`
  - `IpcServer` 增加 `setRequestHandler(std::function<QJsonObject(quint64, const QJsonObject&)>)`

- [ ] **Step 1: 写失败测试**

覆盖：`frameRoundTrip`、`partialFrameWaits`、`oversizeFrameRejected`、`requestResponseOverLocalSocket`、`eventBroadcast`。用 `QLocalServer`/`QLocalSocket` 对。

- [ ] **Step 2: 运行测试确认失败**

Expected: 编译失败/失败。

- [ ] **Step 3: 实现**

帧：4 字节大端长度（最大 1 MiB）+ UTF-8 JSON。请求字段：`id`、`cmd`、其余参数；响应：`id`、`ok`、`data` 或 `error{code,message}`；事件：`event`、`data`。`IpcServer` 用 `QLocalServer::removeServer(name)` 后 listen；socket 名放 `QStandardPaths::RuntimeLocation`。

- [ ] **Step 4: 运行测试**

Expected: all tests pass。

- [ ] **Step 5: 提交**

```bash
git add chatbox/clientcore/ipc chatbox/clientcore/tests
git commit -m "feat(clientcore): add versioned JSON IPC over local socket"
```

---

## Task 9: Android service 集成 ChatService + 通知

**Files:**
- Modify: `chatbox/mobile/src/service_app.h/.cpp`
- Create: `chatbox/clientcore/notify/notification_sink.h`
- Create: `chatbox/mobile/src/android_notification_sink.h/.cpp`
- Create: `chatbox/mobile/src/android_secure_store.h/.cpp`
- Create: `chatbox/mobile/android/src/com/chatbox/mobile/ChatBoxKeystore.java`
- Modify: `chatbox/mobile/android/src/com/chatbox/mobile/ChatBoxNotifications.java`
- Modify: `chatbox/mobile/src/main.cpp`
- Modify: `chatbox/mobile/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class NotificationSink { virtual void postMessage(int id, QString title, QString body, int kind, quint64 target_id)=0; virtual void cancel(int id)=0; virtual void setForegroundState(QString text)=0; }`
  - `AndroidNotificationSink : NotificationSink` — 通过 `QJniObject` 调 Java 静态方法
  - `AndroidSecureStore : SecureStore` — 通过 `ChatBoxKeystore` 的 AES/GCM 加密读写 token/device_id
  - `ServiceApp` 组合 `ChatService` + `IpcServer` + `AndroidNotificationSink` + `AndroidSecureStore`

- [ ] **Step 1: 写 Java Keystore 封装**

`ChatBoxKeystore.getSecret(Context, String alias)` / `putSecret(Context, String alias, String value)` / `clearSecret(Context, String alias)`：用 `KeyStore.getInstance("AndroidKeyStore")` 生成/读取 AES-256-GCM 密钥，密文和 IV 存 `SharedPreferences`；token 和 device_id 各一个 alias。不要在日志里打印明文。

- [ ] **Step 2: 写 Java 通知方法**

`ChatBoxNotifications.postMessage(Context, int id, String title, String body, int kind, long targetId)`：高优先级渠道；`PendingIntent` 带 extras 打开 QtActivity；`setAutoCancel(true)`；同一会话用固定 id 覆盖。`setForegroundState` 更新常驻通知文本。

- [ ] **Step 3: 实现 C++ sink、安全存储和 ServiceApp**

`AndroidSecureStore` 首次启动生成随机 UUID 作为 device_id 并存入 Keystore；`loadToken/saveToken/clearToken` 调 Java。`ServiceApp::run` 起 `ChatService`，把 `stateChanged` 映射到常驻通知；`eventReceived` 若 `event=="message"` 且不是当前会话，调 `postMessage`；把 `IpcServer` 的请求转给 ChatService，响应回 UI。UI 的 `set_active` 更新 suppression。

- [ ] **Step 4: 构建、安装、登录验证**

Run: `.\build_android.ps1; adb install -r <apk>`
手工：登录测试账号；从另一账号发私聊；锁屏收到通知；点通知打开 app 并进入会话；当前会话打开时不重复通知；杀进程重开自动 RESUME。

- [ ] **Step 5: 提交**

```bash
git add chatbox/mobile chatbox/clientcore/notify
git commit -m "feat(android): connect service to chat core and notifications"
```

---

## Task 10: QML 登录 / 会话列表 / 1:1 聊天

**Files:**
- Create: `chatbox/mobile/src/ipc_bridge.h`
- Create: `chatbox/mobile/src/ipc_bridge.cpp`
- Create: `chatbox/mobile/android/src/com/chatbox/mobile/ChatBoxPermissions.java`
- Create: `chatbox/mobile/qml/LoginPage.qml`
- Create: `chatbox/mobile/qml/ConversationListPage.qml`
- Create: `chatbox/mobile/qml/ChatPage.qml`
- Create: `chatbox/mobile/qml/SettingsPage.qml`
- Modify: `chatbox/mobile/qml/Main.qml`
- Modify: `chatbox/mobile/src/main.cpp`
- Modify: `chatbox/mobile/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `class IpcBridge : public QObject { Q_INVOKABLE int request(QString cmd, QVariantMap args); signals: response(int id, QVariantMap data); event(QString name, QVariantMap data); connectedChanged(bool); }`
  - QML 页面通过 `ipc` context property 调用。

- [ ] **Step 1: 写 QML 页面**

登录页：用户名/密码/登录/自动登录；错误显示。会话列表：`ListView` 显示 title/last_content/time/unread；连接横幅。聊天页：消息 `ListView`、输入框、发送、失败重试、进入即 mark_read。设置占位：通知权限、电池优化、连接状态、退出登录。

- [ ] **Step 2: 运行时权限与电池设置**

`ChatBoxPermissions.requestNotifications(Activity)`：Android 13+ 请求 `POST_NOTIFICATIONS`；`openBatteryOptimizationSettings(Context)` 打开系统电池优化页；`openAppNotificationSettings(Context)` 打开通知设置。QML 设置页按钮通过 `IpcBridge`/JNI 调这些方法；拒绝通知时显示“锁屏通知不可用”，不阻塞登录。

- [ ] **Step 3: 实现 IpcBridge**

`request` 生成自增 id，发送 JSON；收到 response 按 id emit；收到 event 原样 emit。`connectedChanged` 驱动 UI 横幅。

- [ ] **Step 4: 构建并手工验收**

Run: `.\build_android.ps1; adb install -r <apk>`
Expected: 登录成功进入会话列表；点私聊进入聊天页；发文本对方收到；对方发来实时显示；退出登录回登录页；首次启动弹通知权限。

- [ ] **Step 5: 提交**

```bash
git add chatbox/mobile
git commit -m "feat(android): add QML login, conversations, and 1:1 chat"
```

---

## Task 11: 打包、签名准备与验收

**Files:**
- Create: `chatbox/mobile/package_apk.ps1`
- Create: `chatbox/mobile/docs/acceptance.md`
- Create: `chatbox/mobile/docs/release-signing.md`
- Modify: `chatbox/mobile/README.md`
- Modify: `docs/superpowers/specs/2026-10-29-chatbox-android-foundation-design.md`（仅补实际 Qt 版本/测试机结果，不改设计决定）

**Interfaces:**
- Produces: release APK + SHA-256 + 签名指纹；验收记录。

- [ ] **Step 1: 写 release 构建脚本**

`package_apk.ps1`：
1. 检查环境变量 `CHATBOX_ANDROID_KEYSTORE`、`CHATBOX_ANDROID_KEY_ALIAS`、`CHATBOX_ANDROID_KEYSTORE_PASSWORD`。
2. `cmake --build build-android --target apk`。
3. `apksigner sign --ks ... --out release/chatbox-<version>.apk`。
4. `apksigner verify --print-certs`，输出 SHA-256。
5. 生成 `release/chatbox-<version>.sha256`。
6. 不把 keystore/密码写进仓库。

- [ ] **Step 2: 写验收文档**

按设计 §9.4 的 10 个场景逐条记录：设备、Android 版本、结果、日志路径。特别是：锁屏通知、划掉、重启、双设备、旧桌面不踢、注销手机、离线 30 分钟同步、服务端重启。

- [ ] **Step 3: 全量回归**

```powershell
cd chatbox\client\tests; .\run_client_tests.ps1
cd ..\..\mobile; .\build_android.ps1
cd ..\..; python tools\run_multidevice_tests.ps1
```

Expected: 桌面 204+ 全绿；Android 构建成功；服务端多设备脚本全 PASS；验收 10 条全 PASS 或有明确记录的限制。

- [ ] **Step 4: 提交**

```bash
git add chatbox/mobile/package_apk.ps1 chatbox/mobile/docs chatbox/mobile/README.md docs/superpowers/specs/2026-10-29-chatbox-android-foundation-design.md
git commit -m "chore(android): package signed APK and record acceptance"
```

---

## Self-Review

- **Spec coverage:** F0 工具链（Task 1-4）、共享核心 transport/session/store/sync（Task 5-7）、IPC（Task 8）、service/通知（Task 9）、QML 最小 UI（Task 10）、打包/验收（Task 11）。
- **Type consistency:** `TransportConfig` 在 Task 2 定义，Task 5 使用；`SessionConfig` 在 Task 5 定义，Task 7 使用；`MobileStore` 在 Task 6 定义，Task 7 使用；`IpcServer` 在 Task 3/8 定义，Task 9 使用；`NotificationSink` 在 Task 9 定义。
- **Review Focus tests:** 划掉/锁屏/重启（Task 4/9/11）、双进程不重复登录（Task 7/9）、多设备一致性（Task 7/9/11）、终止错误（Task 7）、通知点击/当前会话抑制（Task 9/11）。
- **Deferred:** 完整手机 UI、群管理、文件、搜索、资料页、应用内更新、厂商推送不在本计划。
