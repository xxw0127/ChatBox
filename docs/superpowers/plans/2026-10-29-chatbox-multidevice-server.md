# ChatBox 多设备服务端与桌面适配 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 ChatBox 服务端从单会话改成多设备会话，补齐按会话游标同步、已读同步、私聊回显，并让现有 Qt 5.15 桌面端兼容且新增设备管理入口。

**Architecture:** 服务端以 `user_id → 多个 SessionBinding` 为在线路由核心；群成员仍按 user 去重，但广播到该 user 的全部连接。消息同步用每会话 `last_message_id` 游标和新增的 ASC 查询；控制状态靠重拉列表校正。桌面端只增加能力位、已读/回显处理和设备管理页。

**Tech Stack:** C++17、Linux epoll 服务端、MySQL 8.4、CMake、CTest、Python 协议联调脚本、Qt 5.15 Widgets、QtTest。

**Spec:** `docs/superpowers/specs/2026-10-29-chatbox-android-foundation-design.md`

## Global Constraints

- C++17；4 空格缩进，禁止 Tab；K&R 大括号；头文件用 `#ifndef/#define/#endif`。
- 服务端编译参数 `-Wall -Wextra -O2`；GCC 9+。
- 所有网络字段网络字节序；禁止直接 send 结构体；复用 `common/` 的 `ByteWriter`/`ByteReader`。
- MySQL 全部使用预处理语句；禁止字符串拼接 SQL；每个线程独立连接。
- IO 线程不做阻塞 DB/磁盘操作；同步和 fan-out 在 worker 线程完成。
- 协议只追加命令/字段，不改现有帧头和现有命令编码；旧客户端必须继续可用。
- 迁移脚本按 `information_schema` 判断，必须可重复执行；不做 `DROP`。
- 部署/迁移/日志查看只走 SSH；不在仓库写真实口令、token、keystore。
- 服务端测试在 Linux 构建；桌面测试用 `chatbox\client\tests\run_client_tests.ps1`。

## Review Focus

- 旧 0.4.0 客户端登录不能踢掉新手机；旧客户端之间仍保持单会话。
- 同账号两台设备同时在线时，在线状态不能因一台断开而闪成离线。
- 群消息、私聊、好友申请、资料更新、文件通知必须到达目标 user 的全部 session。
- 被注销/被淘汰的 session 必须收到终止错误，客户端不能拿同一个 token 循环重连。
- 同步分页在 60 条以上、重复推送、乱序到达时不能丢消息、不能重复、不能超过 512 KiB。

---

## Task 1: 协议常量与会话编解码

**Files:**
- Modify: `chatbox/common/protocol.h`
- Create: `chatbox/common/session_protocol.h`
- Create: `chatbox/common/session_protocol.cpp`
- Create: `chatbox/tests/test_session_protocol.cpp`
- Modify: `chatbox/CMakeLists.txt`

**Interfaces:**
- Consumes: `ByteWriter`、`ByteReader`、`Frame`（`common/protocol.h`）
- Produces:
  - `enum Command` 新值：`kCmdSessionList=0x00D0`、`kCmdSessionRevoke=0x00D1`、`kCmdReadStatePush=0x00D2`、`kCmdSyncPull=0x00D3`、`kCmdSyncResp=0x00D4`、`kCmdMessageEchoPush=0x00D5`
  - `constexpr uint32_t kCapMultiDevice=1u<<0; kCapSyncPull=1u<<1; kCapReadStatePush=1u<<2; kCapMessageEcho=1u<<3; kCapSessionManage=1u<<4; kCapDeviceMetadata=1u<<5;`
  - `struct DeviceInfo { std::string device_id, device_name, platform, app_version; uint32_t caps=0; }`
  - `struct SessionInfo { std::string session_id, device_id, device_name, platform, app_version, created_at, last_seen_at; bool current=false; }`
  - `void appendDeviceInfo(ByteWriter&, const DeviceInfo&)`
  - `bool readDeviceInfo(ByteReader&, DeviceInfo&, bool& present)` — `present=false` 表示旧客户端没有该字段
  - `void appendAuthExtras(ByteWriter&, uint32_t server_caps, uint32_t ttl_seconds)`
  - `bool readAuthExtras(ByteReader&, uint32_t& server_caps, uint32_t& ttl_seconds)` — 尾部不足 8 字节时返回 false，调用方按旧响应处理
  - `void appendSessionList(ByteWriter&, const std::vector<SessionInfo>&)`
  - `bool readSessionList(ByteReader&, std::vector<SessionInfo>&)`
  - `struct SyncCursor { uint8_t kind; uint64_t target_id; uint64_t last_message_id; }`
  - `bool readSyncPull(ByteReader&, std::vector<SyncCursor>&, uint16_t& start_index)`
  - `struct ReadStateEvent { uint8_t kind; uint64_t target_id; uint64_t last_read_message_id; uint32_t unread; }`
  - `void appendReadState(ByteWriter&, const ReadStateEvent&)`
  - `bool readReadState(ByteReader&, ReadStateEvent&)`

- [ ] **Step 1: 写失败测试 `chatbox/tests/test_session_protocol.cpp`**

覆盖：`deviceInfoRoundTrip`、`legacyBodyHasNoDeviceInfo`、`authExtrasRoundTrip`、`missingAuthExtrasReturnsFalse`、`sessionListRoundTrip`、`syncPullRoundTrip`、`readStateRoundTrip`。使用 `tests/test_framework.h` 的 `CHECK_TRUE/CHECK_EQ`，`main()` 返回失败数。

- [ ] **Step 2: 把测试加入 CMake 并确认失败**

```cmake
add_library(chatbox_common
    common/protocol.cpp
    common/group_protocol.cpp
    common/file_protocol.cpp
    common/session_protocol.cpp
    common/text_utils.cpp
)
add_executable(test_session_protocol tests/test_session_protocol.cpp)
target_link_libraries(test_session_protocol PRIVATE chatbox_common)
add_test(NAME session_protocol COMMAND test_session_protocol)
```

Run: `cmake --build build -j"$(nproc)" && ./build/test_session_protocol`
Expected: 编译失败，`session_protocol.h` 不存在。

- [ ] **Step 3: 实现 `common/session_protocol.h/.cpp`**

编码规则：`DeviceInfo` 用 `u8 present=1` + 4 个 string + `u32 caps`；`SessionInfo` 用 7 个 string + `u8 current`；`SyncCursor` 用 `u8 kind,u64 target_id,u64 last_message_id`；`ReadStateEvent` 用 `u8,u64,u64,u32`。字符串仍用现有 `putString/getString`（u16 长度 + UTF-8）。

- [ ] **Step 4: 运行测试**

Run: `cmake --build build -j"$(nproc)" && ./build/test_session_protocol`
Expected: `test_session_protocol: all checks passed`

- [ ] **Step 5: 提交**

```bash
git add chatbox/common/protocol.h chatbox/common/session_protocol.h chatbox/common/session_protocol.cpp chatbox/tests/test_session_protocol.cpp chatbox/CMakeLists.txt
git commit -m "feat(protocol): add multi-device session codecs"
```

---

## Task 2: sessions 迁移与 DB API

**Files:**
- Create: `chatbox/deploy/migrate_20261029_multidevice.sql`
- Modify: `chatbox/deploy/schema.sql`
- Modify: `chatbox/server/db/mysql_connection.h`
- Modify: `chatbox/server/db/mysql_connection.cpp`
- Create: `chatbox/tests/test_session_db.cpp`
- Modify: `chatbox/CMakeLists.txt`

**Interfaces:**
- Consumes: `MysqlConnection`、`DbConfig`、`common/session_protocol.h`
- Produces:
  - `struct SessionRecord { std::string session_id, token_hash, device_id, device_name, platform, app_version, created_at, last_seen_at, expires_at; uint64_t user_id=0; uint32_t caps=0; int revoked=0; bool valid=false; }`
  - `bool insertSession(const SessionRecord& session, const std::string& token_hash, std::string& error)`
  - `bool findSession(const std::string& token_hash, SessionRecord& session, bool& found, std::string& error)`（替换现有同名方法）
  - `bool listSessions(uint64_t user_id, std::vector<SessionRecord>& sessions, std::string& error)`
  - `bool revokeSessionById(uint64_t user_id, const std::string& session_id, int reason, std::string& error)`
  - `bool revokeSessionsForDevice(uint64_t user_id, const std::string& device_id, int reason, std::string& error)`
  - `bool revokeLegacySessions(uint64_t user_id, int reason, std::string& error)`
  - `bool countActiveNewSessions(uint64_t user_id, uint32_t& count, std::string& error)`
  - `bool revokeOldestNewSession(uint64_t user_id, int reason, std::string& revoked_session_id, std::string& error)`
  - `bool upgradeSessionDevice(const std::string& token_hash, const DeviceInfo& info, const std::string& expires_at, std::string& error)`
  - `bool touchSession(const std::string& token_hash, const std::string& expires_at, std::string& error)`
  - `bool touchActiveSessions(const std::string& expires_at, std::string& error)`

- [ ] **Step 1: 写迁移脚本**

用 `migrate_profiles.sql` 的存储过程模式，依次添加 `session_id CHAR(36) NULL`、`device_id VARCHAR(36) NOT NULL DEFAULT ''`、`device_name VARCHAR(64) NOT NULL DEFAULT ''`、`platform VARCHAR(16) NOT NULL DEFAULT ''`、`app_version VARCHAR(32) NOT NULL DEFAULT ''`、`caps INT UNSIGNED NOT NULL DEFAULT 0`、`last_seen_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP`。回填 `session_id=UUID()`、`last_seen_at=created_at`。加唯一键 `uk_session_id(session_id)`、索引 `idx_user_device(user_id,device_id)`、`idx_user_seen(user_id,last_seen_at)`。同步更新 `schema.sql` 的 `sessions` 定义。

- [ ] **Step 2: 在测试库执行两次迁移**

```bash
mysql -uroot chatbox_test < chatbox/deploy/migrate_20261029_multidevice.sql
mysql -uroot chatbox_test < chatbox/deploy/migrate_20261029_multidevice.sql
mysql -uroot chatbox_test -e "SHOW COLUMNS FROM sessions; SHOW INDEX FROM sessions;"
```

Expected: 第二次无报错；列和索引齐全。

- [ ] **Step 3: 写失败测试 `tests/test_session_db.cpp`**

测试从 `CHATBOX_TEST_DB_*` 环境变量读取连接；缺失时返回 77（CTest SKIP）。覆盖：`insertAndFindSession`、`listSessionsOnlyCurrentUser`、`revokeById`、`revokeForDeviceKeepsOtherDevice`、`revokeLegacyOnly`、`countAndEvictOldestNew`、`touchSessionExtendsExpiry`。每个测试用唯一 username 前缀并清理自己插入的行。

- [ ] **Step 4: 加入 CMake 并确认失败**

```cmake
add_executable(test_session_db
    tests/test_session_db.cpp
    server/db/mysql_connection.cpp
    server/auth/token.cpp)
target_include_directories(test_session_db PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${MYSQL_INCLUDE_DIR})
target_link_libraries(test_session_db PRIVATE ${MYSQL_LIBRARY} OpenSSL::Crypto Threads::Threads)
add_test(NAME session_db COMMAND test_session_db)
set_tests_properties(session_db PROPERTIES SKIP_RETURN_CODE 77)
```

Run: `cmake --build build -j"$(nproc)" && ./build/test_session_db`
Expected: 编译失败，方法未声明。

- [ ] **Step 5: 实现 DB 方法**

全部用 `MysqlConnection` 现有的 `Stmt`、`bindU64`、`bindString`、`execute`、`mysql_stmt_fetch` 模式。`revokeOldestNewSession` 用 `ORDER BY last_seen_at ASC, created_at ASC LIMIT 1` 只选 `device_id <> '' AND revoked=0 AND expires_at > UTC_TIMESTAMP()`。`touchActiveSessions` 一次更新所有 `revoked=0 AND expires_at > UTC_TIMESTAMP()` 的行。

- [ ] **Step 6: 运行测试**

Run: `CHATBOX_TEST_DB_NAME=chatbox_test CHATBOX_TEST_DB_PASSWORD=... ./build/test_session_db`
Expected: `test_session_db: all checks passed`

- [ ] **Step 7: 提交**

```bash
git add chatbox/deploy/migrate_20261029_multidevice.sql chatbox/deploy/schema.sql chatbox/server/db/mysql_connection.h chatbox/server/db/mysql_connection.cpp chatbox/tests/test_session_db.cpp chatbox/CMakeLists.txt
git commit -m "feat(server): persist multi-device sessions"
```

---

## Task 3: SessionManager 多绑定

**Files:**
- Modify: `chatbox/server/auth/session_manager.h`
- Modify: `chatbox/server/auth/session_manager.cpp`
- Rewrite: `chatbox/tests/test_session_manager.cpp`

**Interfaces:**
- Consumes: `Connection`、`SessionBinding`
- Produces:
  - `struct SessionBinding { uint64_t connection_id; int io_thread_id; std::string peer_ip; std::weak_ptr<Connection> connection; std::string session_id; std::string device_id; uint32_t caps=0; }`
  - `struct SessionChange { bool became_online=false; bool went_offline=false; uint64_t presence_seq=0; std::vector<SessionBinding> removed; }`
  - `SessionChange add(uint64_t user_id, const SessionBinding& binding)`
  - `SessionChange removeConnection(uint64_t connection_id)`
  - `SessionChange removeSession(uint64_t user_id, const std::string& session_id)`
  - `bool find(uint64_t user_id, SessionBinding& binding) const`
  - `bool findAll(uint64_t user_id, std::vector<SessionBinding>& bindings) const`
  - `bool findBySession(const std::string& session_id, SessionBinding& binding) const`
  - `bool isOnline(uint64_t user_id) const`
  - `std::vector<SessionTarget> targets() const`

- [ ] **Step 1: 重写测试为多设备语义**

覆盖：`firstAddBecomesOnline`、`secondAddKeepsOnlineWithoutSeq`、`removeOneKeepsOnline`、`removeLastGoesOffline`、`removeSessionOnlyRemovesTarget`、`findAllReturnsBoth`、`duplicateConnectionReturnsRemovedBinding`、`expiredWeakPointerIsOffline`。断言：只有上线/下线时 `presence_seq != 0`。

- [ ] **Step 2: 运行测试确认失败**

Run: `cmake --build build -j"$(nproc)" && ./build/test_session_manager`
Expected: 编译失败，`add/removeConnection` 不存在。

- [ ] **Step 3: 实现**

内部用 `std::unordered_map<uint64_t, std::unordered_map<uint64_t, SessionBinding>>`（user → connection_id → binding）。`add` 若同 connection 已存在，先移除并放入 `removed`；若该 user 原本为空，`became_online=true` 并分配 seq。`removeConnection`/`removeSession` 只有移除最后一条时 `went_offline=true` 并分配 seq。`findAll` 过滤 `connection.expired()`。

为保持 Task 4 之前服务端可编译，暂时保留旧的 `bind/unbind/unbindByConnection` 包装：`bind` 取该 user 的第一条旧 binding 作为 `old_binding`，再调 `add`；`unbind`/`unbindByConnection` 调 `removeConnection`。Task 4 完成后再删除包装和旧测试。

- [ ] **Step 4: 运行测试**

Run: `./build/test_session_manager`
Expected: all checks passed

- [ ] **Step 5: 提交**

```bash
git add chatbox/server/auth/session_manager.h chatbox/server/auth/session_manager.cpp chatbox/tests/test_session_manager.cpp
git commit -m "feat(server): allow multiple sessions per user"
```

---

## Task 4: AuthService 多设备登录、恢复与设备管理

**Files:**
- Modify: `chatbox/server/auth/auth_service.h`
- Modify: `chatbox/server/auth/auth_service.cpp`
- Modify: `chatbox/server/auth/token.h`（如需新的 token 生成辅助）
- Create: `chatbox/tools/test_multidevice_auth.py`

**Interfaces:**
- Consumes: Task 1 codecs、Task 2 DB API、Task 3 SessionManager
- Produces:
  - `AuthResult` 增加 `std::vector<SessionBinding> close_bindings; bool became_online=false; bool went_offline=false; std::string session_id; uint32_t caps=0;`
  - `handleLogin` 解析可选 `DeviceInfo`；`handleResume` 解析可选 `DeviceInfo` 并就地升级旧会话；`handleLogout` 只移除当前 session
  - `handleSessionList(const Frame&, uint64_t user_id)` / `handleSessionRevoke(const Frame&, uint64_t user_id, const SessionBinding&)`

- [ ] **Step 1: 写失败集成测试 `tools/test_multidevice_auth.py`**

复用 `auth_client.py` 的帧编解码。用例：
1. 注册唯一账号 A；A 用 device1 登录、device2 登录，两次都成功；device1 仍能 PING/PONG。
2. device1 再登录一次，旧 device1 连接收到 1010；device2 不受影响。
3. `SESSION_LIST` 返回 2 个 session，当前标记正确。
4. device1 注销 device2；device2 收到 1002/1010，RESUME 失败。
5. 旧格式登录（不带 device info）成功，且不踢 device1/device2。
6. 6 个新设备依次登录，最旧设备被 1011 淘汰，当前 5 个仍在。

Run（旧服务端）：`python chatbox/tools/test_multidevice_auth.py --host 127.0.0.1 --port 19100`
Expected: 第 1 条失败（第二次登录踢掉第一次）。

- [ ] **Step 2: 修改 `AuthResult` 与登录流程**

新客户端：`revokeSessionsForDevice`（reason 3）→ `countActiveNewSessions`，>=5 时 `revokeOldestNewSession`（reason 5）→ `insertSession` → `sessions_.add`。旧客户端：`revokeLegacySessions`（reason 1）→ 插入 `device_id=''` 的 session。所有被替换/淘汰的 session_id 通过 `SessionManager::removeSession` 找到 binding，放入 `close_bindings`。

- [ ] **Step 3: 修改 RESUME**

读取可选 DeviceInfo；若记录 `device_id` 为空且请求带设备信息，调用 `upgradeSessionDevice`；刷新 caps/last_seen/expires；若同一 token 已绑定另一连接，移除旧 binding 放入 `close_bindings`。绑定新 connection。

- [ ] **Step 4: 实现会话列表/注销**

`SESSION_LIST` 调 `listSessions`，标记 `current` 为当前 token_hash。`SESSION_REVOKE` 校验 session 属于当前 user；`revokeSessionById(reason 4)`；`SessionManager::removeSession` 得到 binding，放入 `close_bindings`；返回成功后由 main 关闭目标连接。

- [ ] **Step 5: 更新 main.cpp 的 auth 分支**

遍历 `result.close_bindings`，按 `io_thread_id` 调 `postErrorAndClose(connection_id, code, message, reason)`；根据 `became_online/went_offline` 发 presence；`bind_session` 时 `connection->setAuth(user_id, nickname, token_hash, session_id, device_id, caps)`。

同时把 `kCmdSessionList`/`kCmdSessionRevoke` 加入 `session_command` 分类，路由到 `AuthService::handle`；列表响应发回 origin；注销响应先发 origin，再关闭目标连接。`broadcastPresence` 改为排除该 user 的全部 session（不只 origin），避免自己的其他设备收到自己的上下线事件。

- [ ] **Step 6: 运行集成测试**

Run: `python chatbox/tools/test_multidevice_auth.py --host 127.0.0.1 --port 19100`
Expected: 6 条全 PASS。

- [ ] **Step 7: 提交**

```bash
git add chatbox/server/auth/auth_service.h chatbox/server/auth/auth_service.cpp chatbox/server/main.cpp chatbox/tools/test_multidevice_auth.py
git commit -m "feat(server): support multi-device login and session management"
```

---

## Task 5: RoomManager 多连接群成员

**Files:**
- Modify: `chatbox/server/room/room_manager.h`
- Modify: `chatbox/server/room/room_manager.cpp`
- Modify: `chatbox/server/room/room_service.cpp`
- Modify: `chatbox/server/main.cpp`
- Create: `chatbox/tests/test_room_manager.cpp`
- Modify: `chatbox/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `bool join(int room_id, const RoomMember& member, std::string& error)`
  - `bool leave(int room_id, uint64_t user_id)`
  - `bool isMember(int room_id, uint64_t user_id) const`
  - `bool snapshot(int room_id, std::vector<RoomMember>& members) const` — 每个 user 一行
  - `bool targets(int room_id, std::vector<RoomMember>& members) const` — 每个连接一行
  - `int onlineCount(int room_id) const` — 按 user 去重
  - `std::vector<int> removeConnection(uint64_t connection_id)`
  - `void updateNickname(uint64_t user_id, const std::string& nickname)`

- [ ] **Step 1: 写失败测试 `tests/test_room_manager.cpp`**

覆盖：`twoConnectionsOneMemberRow`、`onlineCountCountsUsers`、`leaveRemovesAllUserConnections`、`removeConnectionKeepsOtherDevice`、`maxMembersCountsUsers`、`targetsIncludesBothDevices`、`snapshotDedupesUser`。

- [ ] **Step 2: 加入 CMake 并确认失败**

```cmake
add_executable(test_room_manager
    tests/test_room_manager.cpp
    server/room/room_manager.cpp)
target_include_directories(test_room_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_room_manager PRIVATE Threads::Threads)
add_test(NAME room_manager COMMAND test_room_manager)
```

Run: `cmake --build build -j"$(nproc)" && ./build/test_room_manager`
Expected: 编译失败，`targets` 不存在。

- [ ] **Step 3: 实现**

内部 `members` 以 `connection_id` 为键；`user_connections[user_id]` 保存该 user 的连接集合。`join` 重复连接直接更新；人数上限按 `user_connections.size()` 判断。`snapshot` 每个 user 取最新连接（按加入顺序或 connection_id 最大），`targets` 返回全部。`leave` 删除该 user 全部连接；`removeConnection` 只删一条。

- [ ] **Step 4: 更新 RoomService 与 main**

`handleJoinRoom` 广播用 `rooms_.targets(room_id, targets)`；成员列表帧内容用 `snapshot`。`broadcastChangedRoomMembers` 也从 `targets` 取接收者，帧内容用 `snapshot()`。

- [ ] **Step 5: 运行测试**

Run: `./build/test_room_manager`
Expected: all checks passed

- [ ] **Step 6: 提交**

```bash
git add chatbox/server/room/room_manager.h chatbox/server/room/room_manager.cpp chatbox/server/room/room_service.cpp chatbox/server/main.cpp chatbox/tests/test_room_manager.cpp chatbox/CMakeLists.txt
git commit -m "feat(server): route group traffic to every device"
```

---

## Task 6: 私聊/好友/群/资料/文件 fan-out

**Files:**
- Modify: `chatbox/server/private/private_service.h`
- Modify: `chatbox/server/private/private_service.cpp`
- Modify: `chatbox/server/friend/friend_service.h`
- Modify: `chatbox/server/friend/friend_service.cpp`
- Modify: `chatbox/server/profile/profile_service.h`
- Modify: `chatbox/server/profile/profile_service.cpp`
- Modify: `chatbox/server/main.cpp`
- Create: `chatbox/tools/test_multidevice_fanout.py`

**Interfaces:**
- Produces:
  - `PrivateResult { Frame response; bool push; uint64_t target_user_id; std::string push_frame; }`
  - `FriendResult { Frame response; bool push; uint64_t target_user_id; std::string push_frame; }`
  - `ProfileResult { Frame response; ...; std::vector<uint64_t> push_user_ids; }`
  - main helper `void postToUserSessions(SessionManager&, const std::vector<IoThread*>&, uint64_t user_id, const std::string& encoded, uint64_t exclude_connection_id=0)`

- [ ] **Step 1: 写失败 fan-out 测试**

`tools/test_multidevice_fanout.py`：
1. A 两设备登录；B 一设备；B 向 A 发私聊；A 两设备都收到 `kCmdPrivatePush`。
2. A device1 向 B 发私聊；A device2 收到 `kCmdMessageEchoPush`。
3. 创建群，A 两设备都 `JOIN_ROOM`；B 在群里发消息；A 两设备都收到。
4. B 向 A 发好友申请；A 两设备都收到 `kCmdFriendRequestPush`。
5. A 修改昵称；在线好友 B 的两设备都收到 `kCmdProfileUpdatePush`（B 开两设备）。

Run（当前服务端）：Expected: 第 1 条失败（只有一台 A 设备收到）。

- [ ] **Step 2: 改 PrivateService**

`handlePrivateSend`：接收方只判断 `sessions_.isOnline(receiver_id)`，返回 `target_user_id=receiver_id`。`handleRecall` 同理返回目标 user。main 用 `findAll(target_user_id)` fan-out；发送方其他 session 由 Task 7 的 echo 处理。

- [ ] **Step 3: 改 FriendService**

`push` 结果返回 `target_user_id`；main 用 `findAll` fan-out。好友申请/接受两处都改。

- [ ] **Step 4: 改 ProfileService**

`push_targets` 改为 `push_user_ids`：收集好友/在线成员 user_id 并去重；main 对每个 user 调 `postToUserSessions`。昵称变更的 room_manager 更新保留。

- [ ] **Step 5: 改 GroupService/FileService 通知**

`GroupNotice`/`FileNotice` 已是 user_id；main 中把 `sessions.find` 改为 `findAll` 并 fan-out。

- [ ] **Step 6: 实现 main helper 并运行测试**

Run: `python chatbox/tools/test_multidevice_fanout.py --host 127.0.0.1 --port 19100`
Expected: 5 条全 PASS。

- [ ] **Step 7: 提交**

```bash
git add chatbox/server/private chatbox/server/friend chatbox/server/profile chatbox/server/group chatbox/server/file chatbox/server/main.cpp chatbox/tools/test_multidevice_fanout.py
git commit -m "feat(server): fan out pushes to all user sessions"
```

---

## Task 7: 已读同步与私聊回显

**Files:**
- Modify: `chatbox/server/private/private_service.h`
- Modify: `chatbox/server/private/private_service.cpp`
- Modify: `chatbox/server/group/group_service.cpp`
- Modify: `chatbox/server/history/history_service.h`
- Modify: `chatbox/server/history/history_service.cpp`
- Modify: `chatbox/server/main.cpp`
- Create: `chatbox/tools/test_multidevice_read_echo.py`

**Interfaces:**
- Produces:
  - `PrivateResult` 增加 `bool notify_self; ReadStateEvent read_state; std::string echo_frame;`
  - `GroupResult` 增加 `bool notify_self; ReadStateEvent read_state;`
  - `HistoryResult { Frame response; bool notify_self; ReadStateEvent read_state; }`
  - main 对 `notify_self` 用 `findAll(user_id)` 发 `kCmdReadStatePush`，排除 origin；`echo_frame` 发 `kCmdMessageEchoPush`，排除 origin。

- [ ] **Step 1: 写失败测试**

`tools/test_multidevice_read_echo.py`：
1. A 两设备；B 给 A 发私聊；A device1 打开历史（`HISTORY before=0`）或发 `PRIVATE_MARK_READ`；A device2 收到 `kCmdReadStatePush`，unread=0。
2. A 在群里发消息；A device2 不发 echo（群广播已覆盖），但消息只出现一次（按 message_id 去重）。
3. A device1 给 B 发私聊；A device2 收到 `kCmdMessageEchoPush`，`origin_device_id` 为 device1。
4. A device2 当前正打开该会话时收到 echo，不增加未读。

Run（当前服务端）：Expected: 失败，命令未实现/未发送。

- [ ] **Step 2: 实现已读推送**

`handleMarkRead` 更新 DB 后填 `notify_self=true`、`read_state={kind,target,message_id,0}`。`HistoryService` 在 `before_id==0` 自动标记私聊已读时同样填。群 `handleMarkRead` 填 kind=1。

- [ ] **Step 3: 实现私聊 echo**

`handlePrivateSend` 成功后构造 `kCmdMessageEchoPush`：`u8 kind=2`、`u64 target_id=receiver_id`、完整消息字段（与 `kCmdPrivatePush` 相同的 id/sender/nickname/content/created_at/reply/...）、`string origin_device_id`。main 发给发送方除 origin 外的 session。

- [ ] **Step 4: 能力位过滤**

只有 `(binding.caps & kCapReadStatePush)` 的 session 收 read state；只有 `(binding.caps & kCapMessageEcho)` 的收 echo。旧客户端 caps=0，不受影响。

- [ ] **Step 5: 运行测试**

Run: `python chatbox/tools/test_multidevice_read_echo.py --host 127.0.0.1 --port 19100`
Expected: 4 条全 PASS。

- [ ] **Step 6: 提交**

```bash
git add chatbox/server/private chatbox/server/group/group_service.cpp chatbox/server/history chatbox/server/main.cpp chatbox/tools/test_multidevice_read_echo.py
git commit -m "feat(server): sync read state and echo private messages"
```

---

## Task 8: 按会话游标同步

**Files:**
- Create: `chatbox/server/sync/sync_service.h`
- Create: `chatbox/server/sync/sync_service.cpp`
- Create: `chatbox/server/history/history_codec.h`
- Create: `chatbox/server/history/history_codec.cpp`
- Modify: `chatbox/server/db/mysql_connection.h`
- Modify: `chatbox/server/db/mysql_connection.cpp`
- Modify: `chatbox/server/history/history_service.cpp`
- Modify: `chatbox/CMakeLists.txt`
- Create: `chatbox/tools/test_multidevice_sync.py`

**Interfaces:**
- Produces:
  - `bool listRoomHistoryAfter(int room_id, uint64_t after_id, int limit, HistoryPage& page, std::string& error)`
  - `bool listPrivateHistoryAfter(uint64_t user_id, uint64_t peer_id, uint64_t after_id, int limit, HistoryPage& page, std::string& error)`
  - `struct EncodedHistory { std::string body; uint16_t count=0; bool has_more=false; uint64_t next_after=0; }`
  - `bool encodeHistoryMessages(const std::vector<HistoryMessage>&, const std::vector<FileMeta>&, std::size_t max_bytes, EncodedHistory&, std::string& error)`
  - `Frame SyncService::handle(const Frame& request, const RoomMember& caller)`；请求 `kCmdSyncPull`，响应 `kCmdSyncResp`

- [ ] **Step 1: 写失败测试**

`tools/test_multidevice_sync.py`：
1. A 离线；B 给 A 发 60 条私聊；A 登录后发 `SYNC_PULL`，cursor=0；收 50 条 + `has_more_messages=1`，再收 10 条；顺序正确、无重复。
2. 群消息同样分页；`next_index` 在多会话请求时正确续传。
3. 同一批消息重复 sync 不产生重复。
4. 一条带文件元数据的消息，同步响应包含 `FileMeta`。
5. 响应 body 不超过 512 KiB（构造 20 个会话各 50 条长消息）。

Run（当前服务端）：Expected: 失败，命令不存在。

- [ ] **Step 2: 实现 DB ASC 查询**

SQL 用 `WHERE ... AND id > ? ORDER BY id ASC LIMIT ?`，取 `limit+1` 判断 `has_more`；`next_after` = 最后一条的 id。私聊需要双向条件。`HistoryPage` 增加 `uint64_t next_after_id=0`（或由调用方从 messages.back() 取）。

- [ ] **Step 3: 提取历史编码**

把 `history_service.cpp` 中消息 + FileMeta 编码提取到 `history_codec`，`HistoryService` 与 `SyncService` 共用；`max_bytes` 用于预算控制。

- [ ] **Step 4: 实现 SyncService**

解析 `u16 count, u16 start_index, per cursor`；从 `start_index` 顺序处理；每会话最多 50 条；累计 body 到 480 KiB 就停；输出 `u16 count, u16 next_index, u8 has_more, per conversation { u8 kind, u64 target, u16 message_count, u8 has_more_messages, u64 next_after, messages..., u16 file_meta_count, ... }`。校验房间成员/私聊对象合法性。

- [ ] **Step 5: 加入 main 路由与 CMake**

`kCmdSyncPull` 归入 `sync_command`，worker 调 `context.sync.handle`；响应走 owner->sendFrame。只有 `kCapSyncPull` 的 session 才能调用。

- [ ] **Step 6: 运行测试**

Run: `python chatbox/tools/test_multidevice_sync.py --host 127.0.0.1 --port 19100`
Expected: 5 条全 PASS。

- [ ] **Step 7: 提交**

```bash
git add chatbox/server/sync chatbox/server/history chatbox/server/db/mysql_connection.h chatbox/server/db/mysql_connection.cpp chatbox/server/main.cpp chatbox/CMakeLists.txt chatbox/tools/test_multidevice_sync.py
git commit -m "feat(server): sync messages by per-conversation cursor"
```

---

## Task 9: 能力位、终止错误与 last_seen 后台刷新

**Files:**
- Modify: `chatbox/server/net/connection.h`
- Modify: `chatbox/server/net/connection.cpp`
- Modify: `chatbox/server/main.cpp`
- Create: `chatbox/server/auth/session_toucher.h`
- Create: `chatbox/server/auth/session_toucher.cpp`
- Modify: `chatbox/CMakeLists.txt`
- Create: `chatbox/tools/test_multidevice_caps.py`

**Interfaces:**
- Produces:
  - `Connection::setAuth(uint64_t user_id, const std::string& nickname, const std::string& token_hash, const std::string& session_id="", const std::string& device_id="", uint32_t caps=0)`
  - `Connection::caps() const`
  - `class SessionToucher { SessionToucher(const DbConfig&, int interval_seconds); void start(); void stop(); }`
  - 错误码：1011 `too many devices`

- [ ] **Step 1: 写失败测试**

`tools/test_multidevice_caps.py`：
1. 旧格式登录（caps=0）后，A 的另一台新设备发私聊给 B；旧连接不收到 echo/read state，但仍收到原有 `kCmdPrivatePush`。
2. 新客户端 A 两设备，已读推送和 echo 正常。
3. 被 reason 4 注销的连接收到 1002；被 reason 5 淘汰收到 1011；RESUME 失败后不再重连（脚本只验证错误码）。
4. `last_seen_at` 在 10 分钟 touch 任务后更新（测试用 2 秒间隔的 SessionToucher 验证 SQL 路径）。

Run（当前服务端）：Expected: 失败。

- [ ] **Step 2: 扩展 Connection**

新增 `session_id_`、`device_id_`、`caps_` 和 getter；`setAuth` 增加默认参数，文件服务现有三参数调用保持兼容。`clearAuth` 清零。

- [ ] **Step 3: 能力位 gate**

main 中构造 `SessionBinding` 时带 session_id/device_id/caps；所有新推送前检查对应 cap。登录/恢复响应追加 `appendAuthExtras(server_caps, 30*24*3600)`。

- [ ] **Step 4: 终止错误映射**

main 关闭 `close_bindings` 时按 reason 选 code/message：3→1010 `session replaced`，4→1002 `session revoked`，5→1011 `too many devices`，1→1010 `kicked by new login`。RESUME takeover 同 token →1010 `session taken over`。

- [ ] **Step 5: SessionToucher**

独立线程 + 自己的 `MysqlConnection`；每 `interval_seconds` 调 `touchActiveSessions(utcPlusDays(30))`；启动时立即执行一次；`stop()` 用 condition_variable 唤醒并 join。main 在 worker pool 启动后创建，在退出时 stop。失败只记 WARN，不退出进程。

- [ ] **Step 6: 运行测试**

Run: `python chatbox/tools/test_multidevice_caps.py --host 127.0.0.1 --port 19100`
Expected: 4 条全 PASS。

- [ ] **Step 7: 提交**

```bash
git add chatbox/server/net/connection.h chatbox/server/net/connection.cpp chatbox/server/main.cpp chatbox/server/auth/session_toucher.h chatbox/server/auth/session_toucher.cpp chatbox/CMakeLists.txt chatbox/tools/test_multidevice_caps.py
git commit -m "feat(server): gate new pushes by capability and expire sessions safely"
```

---

## Task 10: 桌面端能力位、已读与回显

**Files:**
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/core/app_settings.h` / `.cpp`（保存 device_id）
- Modify: `chatbox/client/tests/test_client_core.cpp`
- Modify: `chatbox/client/chatbox.pro`（如新增源文件）

**Interfaces:**
- Produces:
  - `QString MainWindow::deviceId()` — 首次用 `QUuid::createUuid()` 生成并存 `QSettings("device/id")`
  - `void MainWindow::appendDeviceInfo(chatbox::ByteWriter&)` — device_id/name/platform/app_version/caps
  - `void MainWindow::handleReadStatePush(const QByteArray& body)`
  - `void MainWindow::handleMessageEchoPush(const QByteArray& body)`
  - `uint32_t server_caps_ = 0` 成员

- [ ] **Step 1: 写失败 QtTest**

在 `test_client_core.cpp` 增加：`desktopSendsDeviceInfoOnLogin`（捕获 frame body）、`desktopParsesAuthExtras`、`desktopToleratesLegacyAuthResponse`、`desktopAppliesReadState`、`desktopAppliesOwnEcho`。先运行 `.\run_client_tests.ps1`，Expected: 新测试失败。

- [ ] **Step 2: 登录/恢复追加设备信息**

`sendLogin`/`sendResume` 在现有字段后 `appendDeviceInfo`；caps 至少包含 `kCapMultiDevice|kCapSyncPull|kCapReadStatePush|kCapMessageEcho|kCapSessionManage|kCapDeviceMetadata`。device_name 用 `QSysInfo::machineHostName()`，platform `"windows"`，app_version `QCoreApplication::applicationVersion()`。

- [ ] **Step 3: 解析响应尾部**

`kCmdLogin`/`kCmdResume` 读取完现有字段后，用 `chatbox::readAuthExtras` 尝试读 caps/ttl；失败则 caps=0，不报错。

- [ ] **Step 4: 已读推送**

`case kCmdReadStatePush`：`readReadState`；按 kind/target 找会话；更新未读角标（私聊/群）；如果当前上下文匹配，清角标并 `markConversationRead`（仅本地，不回发 mark read）。未知 kind 忽略。

- [ ] **Step 5: 回显推送**

`case kCmdMessageEchoPush`：解析消息字段；按 `(kind,target,message_id)` 去重；当前会话匹配则追加到 `message_store_`，否则只更新会话摘要；`outgoing=true`，不计未读。不要复用 `kCmdPrivatePush` 的“sender==自己就忽略”逻辑。

- [ ] **Step 6: 运行 QtTest**

Run: `cd chatbox\client\tests; .\run_client_tests.ps1`
Expected: 204 + 新增测试全绿，0 failed。

- [ ] **Step 7: 提交**

```bash
git add chatbox/client/ui/main_window_net.cpp chatbox/client/ui/main_window.h chatbox/client/core/app_settings.* chatbox/client/tests/test_client_core.cpp chatbox/client/chatbox.pro
git commit -m "feat(client): handle multi-device auth, read state, and echo"
```

---

## Task 11: 桌面端设备管理页

**Files:**
- Create: `chatbox/client/ui/session_list_dialog.h`
- Create: `chatbox/client/ui/session_list_dialog.cpp`
- Modify: `chatbox/client/ui/settings_dialog.h` / `.cpp`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/tests/tests.pro`
- Modify: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  - `struct SessionView { QString session_id, device_name, platform, app_version, last_seen_at; bool current=false; }`
  - `SessionListDialog::setSessions(const QVector<SessionView>&)`
  - signal `void revokeRequested(const QString& session_id)`
  - signal `void refreshRequested()`
  - `SettingsDialog` signal `void deviceManagementRequested()`

- [ ] **Step 1: 写失败 QtTest**

`sessionDialogShowsDevices`、`sessionDialogDisablesCurrentDevice`、`sessionDialogEmitsRevoke`、`settingsDialogEmitsDeviceManagement`。Run: Expected: 编译失败/测试失败。

- [ ] **Step 2: 实现对话框**

`QDialog` + `QTableWidget`（设备名/平台/版本/最后活跃/操作），当前设备行禁用注销按钮；底部“刷新”。`setSessions` 重建行；注销按钮弹确认框后 emit session_id。

- [ ] **Step 3: 设置页入口**

在设置页加“设备管理”按钮，点击 emit `deviceManagementRequested`；不阻塞现有保存逻辑。

- [ ] **Step 4: MainWindow 接线**

收到 `deviceManagementRequested`：发送 `kCmdSessionList`，保存 seq；收到响应解析 `readSessionList`，打开/刷新 `SessionListDialog`。收到 `revokeRequested`：发送 `kCmdSessionRevoke`（body 为 session_id），成功后重新请求列表。注销当前设备时直接走 `logout`。

- [ ] **Step 5: 更新 qmake 工程并运行测试**

把新文件加入 `chatbox.pro` 与 `tests.pro`；Run: `.\run_client_tests.ps1`。Expected: 全绿。

- [ ] **Step 6: 手工验证**

启动桌面客户端，登录后打开设置→设备管理，看到当前设备；用手机登录同账号，刷新后出现手机；点注销手机，手机退出登录，桌面不受影响。

- [ ] **Step 7: 提交**

```bash
git add chatbox/client/ui/session_list_dialog.* chatbox/client/ui/settings_dialog.* chatbox/client/ui/main_window_net.cpp chatbox/client/ui/main_window.h chatbox/client/chatbox.pro chatbox/client/tests/tests.pro chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): add device management settings page"
```

---

## Task 12: 部署文档、全量回归与回滚

**Files:**
- Create: `chatbox/docs/multidevice-deployment.md`
- Modify: `chatbox/README.md`
- Modify: `README.md`
- Create: `chatbox/tools/run_multidevice_tests.ps1`

- [ ] **Step 1: 写部署文档**

内容：迁移顺序（备份→`migrate_20261029_multidevice.sql`→部署新服务端→旧客户端兼容检查→发新客户端）、回滚（停新服务→恢复旧二进制；schema 可保留）、验收命令、SSH 操作要求、测试库 `chatbox_test` 说明。

- [ ] **Step 2: 写一键测试脚本**

`run_multidevice_tests.ps1` 依次运行 `test_multidevice_auth.py`、`test_multidevice_fanout.py`、`test_multidevice_read_echo.py`、`test_multidevice_sync.py`、`test_multidevice_caps.py`，任一失败退出非零。

- [ ] **Step 3: 全量回归**

```bash
cd chatbox
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
(cd build && ctest --output-on-failure)
cd client/tests && ./run_client_tests.ps1
```

Expected: CTest 全绿（新增测试也通过）；客户端 204+ 全绿；Python 多设备脚本全 PASS。

- [ ] **Step 4: 旧客户端兼容验收**

用 0.4.0 桌面客户端连接新服务端：登录、发消息、收消息；同时手机/新桌面在线不被踢；旧客户端之间再登录仍按旧行为替换。记录到部署文档。

- [ ] **Step 5: 提交**

```bash
git add chatbox/docs/multidevice-deployment.md chatbox/README.md README.md chatbox/tools/run_multidevice_tests.ps1
git commit -m "docs: add multi-device deployment and acceptance guide"
```

---

## Self-Review

- **Spec coverage:** 多设备会话（Task 2/3/4）、在线聚合（Task 3/5）、群/私聊/好友/资料/文件 fan-out（Task 6）、已读与回显（Task 7）、按会话同步（Task 8）、能力位/终止错误/last_seen（Task 9）、桌面兼容（Task 10）、设备管理（Task 11）、部署回滚（Task 12）。
- **Type consistency:** `SessionBinding` 的 `session_id/device_id/caps` 在 Task 3 定义，Task 4/6/9 使用；`SessionRecord` 在 Task 2 定义，Task 4 使用；`ReadStateEvent` 在 Task 1 定义，Task 7 使用；`SyncCursor` 在 Task 1 定义，Task 8 使用。
- **Review Focus tests:** 旧客户端兼容（Task 4/9/12）、在线状态不闪（Task 3/5）、全量 fan-out（Task 6）、终止错误（Task 9）、同步分页/去重/512 KiB（Task 8）。
- **Deferred:** 全量事件日志、厂商推送、完整手机 UI 不在本计划。
