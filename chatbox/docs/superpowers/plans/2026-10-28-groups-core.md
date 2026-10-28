# 群基础与右侧栏 Implementation Plan（Phase 1/3）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把现有单房间/纯文本客户端升级为多群客户端：持久成员关系、群主/管理员、创建/搜索/申请/审批/邀请/解散、默认群保护、服务端未读、左栏“会话/群组”Tab、右侧栏 B、上滑加载和栏内搜索；文件入口先占位，文件传输在 Phase 2。

**Architecture:** 服务端在现有 MySQL `rooms` 上扩展群字段，新增 `room_members`/`room_read_state`/申请/邀请表；新增 `GroupService` 处理群命令，`RoomManager` 只保留在线 presence；旧 `ROOM_*` 命令继续服务默认群，保证 0.2.5 客户端兼容。客户端新增 `common/group_protocol` 编解码、`core/group_types` 模型、左栏 Tab、右侧栏 `QStackedWidget`（成员/资料、搜索、文件占位），并复用现有 `selectRoom`/`CHAT_SEND`/`HISTORY` 完成群聊天。

**Tech Stack:** C++17、Qt 5.15 Widgets、原生 Socket/TLS、MySQL 8.4、CMake/qmake、现有 QtTest 测试。

**Spec:** `chatbox/docs/superpowers/specs/2026-10-28-groups-files-panel-design.md`

## Global Constraints

- 服务端 C++17、GCC 9+、`-Wall -Wextra -O2`；客户端 Qt 5.15、MinGW 8.1、C++17。
- 网络字节序；禁止直接收发结构体；所有字符串 `u16` 长度 + UTF-8，超过 `0xFFFF` 截断。
- 单帧入站 ≤ 64KiB；跨线程 socket 读写必须加锁；DB 只在 worker 线程。
- 不拼接 SQL；全部预处理语句 + 参数绑定。
- 默认群 `id=1`，名称“群聊A”，群号 `10000001`，`is_default=1`，不可解散/不可退群，新注册用户自动加入。
- 普通群上限 500 人；每用户 20 个 active 群；建群 5/天；申请/邀请 20/天。
- 群号 8 位数字、全局唯一；群名 1–32 字符，允许重名。
- 历史从 `room_members.join_message_id` 之后开始；群文件对当前成员开放全部（Phase 2）；退群/移除立即失去服务器访问。
- 未读由 `room_read_state.last_read_message_id` 计算；打开群/聚焦且滚到底时上报。
- 默认群群主配置为 `user_id=4`（可通过服务端配置覆盖）。
- 旧 0.2.5 客户端必须继续在默认群发文字、拉历史、看文件回退文字；不新增群/文件 UI。
- 文件保存位置/文件传输不在本阶段实现；右侧栏文件入口只显示“文件功能即将开放”的占位页。
- 不修改 `chatbox/docs/ui-fix-prompts-kimi-k3.md`；该文件不提交。
- 本阶段不合并/推送，完成并通过审查后再由用户确认。

## Review Focus

- **默认群注册/迁移**：新用户注册事务必须同时写入 `room_members`，迁移必须保留 room 1 全部历史；失败不得留下无群用户。
- **成员撤销与历史游标**：退群/被移除后历史、搜索、成员列表、发送必须全部拒绝；重新加入从新游标开始，不能恢复旧服务器历史。
- **旧客户端兼容**：0.2.5 的 ROOM_LIST/JOIN/CHAT/HISTORY 必须继续工作；群命令和未读字段不能破坏旧解析。
- **上滑锚点**：加载旧页后用户正在看的那条消息不能跳走；重复滚动/快速切群不能发出重复请求或把旧响应写进新会话。
- **右侧栏回归**：`⋯` 改为直接开栏后，成员列表、@ 提及、成员双击私聊、右键资料、私聊资料卡都必须继续工作。

---

### Task 1: Schema、迁移与群领域类型

**Files:**
- Modify: `chatbox/deploy/schema.sql`
- Create: `chatbox/deploy/migrate_20261028_groups.sql`
- Create: `chatbox/server/group/group_types.h`
- Create: `chatbox/server/group/group_rules.h`
- Create: `chatbox/server/group/group_rules.cpp`
- Modify: `chatbox/CMakeLists.txt`
- Test: `chatbox/tests/test_group_rules.cpp`
- Test: `chatbox/tools/check_group_schema.ps1`

**Interfaces:**
- Produces:
  ```cpp
  enum class GroupRole : uint8_t { kMember = 0, kAdmin = 1, kOwner = 2 };
  enum class GroupStatus : uint8_t { kActive = 0, kDissolved = 1 };
  struct GroupInfo {
      uint64_t room_id = 0;
      std::string name;
      std::string group_no;
      uint64_t owner_id = 0;
      uint32_t member_count = 0;
      GroupRole role = GroupRole::kMember;
      uint32_t unread = 0;
      bool is_default = false;
      GroupStatus status = GroupStatus::kActive;
  };
  struct GroupMemberInfo { uint64_t user_id; std::string nickname; GroupRole role; bool online; };
  bool validGroupName(const std::string& name);
  bool validGroupNumber(const std::string& group_no);
  bool canManageGroup(GroupRole actor, GroupRole target);
  bool canDisband(GroupRole actor, bool is_default);
  ```
- Schema adds `rooms.group_no`, `rooms.owner_id`, `rooms.is_default`, `rooms.status`, `rooms.dissolved_at`; new `room_members(room_id,user_id,role,joined_at,join_message_id)`, `room_read_state`, `room_join_requests`, `room_invites`.
- Migration keeps room 1 only; sets default group; inserts all existing users into `room_members` (default owner `user_id=4` if present, else `MIN(id)`).

- [ ] **Step 1: Write failing rule tests**

```cpp
void groupRulesValidateNames() {
    QVERIFY(chatbox::validGroupName("设计群"));
    QVERIFY(!chatbox::validGroupName(""));
    QVERIFY(!chatbox::validGroupName(QString(33, 'x').toStdString()));
    QVERIFY(chatbox::validGroupNumber("10000001"));
    QVERIFY(!chatbox::validGroupNumber("1234567"));
    QVERIFY(!chatbox::validGroupNumber("1000000a"));
}
void groupRulesEnforceRoles() {
    using chatbox::GroupRole;
    QVERIFY(chatbox::canManageGroup(GroupRole::kOwner, GroupRole::kAdmin));
    QVERIFY(chatbox::canManageGroup(GroupRole::kAdmin, GroupRole::kMember));
    QVERIFY(!chatbox::canManageGroup(GroupRole::kAdmin, GroupRole::kAdmin));
    QVERIFY(!chatbox::canManageGroup(GroupRole::kMember, GroupRole::kMember));
    QVERIFY(chatbox::canDisband(GroupRole::kOwner, false));
    QVERIFY(!chatbox::canDisband(GroupRole::kOwner, true));
    QVERIFY(!chatbox::canDisband(GroupRole::kAdmin, false));
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake -S chatbox -B chatbox/build && cmake --build chatbox/build --target test_group_rules`
Expected: compile failure: `group_rules.h` / symbols missing.

- [ ] **Step 3: Add schema, migration and rule implementation**

Schema/migration must be idempotent (`ADD COLUMN IF NOT EXISTS` is not available on MySQL 8.4 in all builds; use a migration script with `information_schema` guards or document one-time execution). Keep the schema file as the fresh-install source of truth. Migration SQL:

```sql
ALTER TABLE rooms
  ADD COLUMN group_no VARCHAR(12) NULL,
  ADD COLUMN owner_id BIGINT UNSIGNED NULL,
  ADD COLUMN is_default TINYINT NOT NULL DEFAULT 0,
  ADD COLUMN status TINYINT NOT NULL DEFAULT 0,
  ADD COLUMN dissolved_at DATETIME(3) NULL;
UPDATE rooms SET group_no='10000001', owner_id=4, is_default=1, status=0 WHERE id=1;
UPDATE rooms SET status=1, dissolved_at=NOW(3) WHERE id<>1;
CREATE TABLE IF NOT EXISTS room_members (...);
CREATE TABLE IF NOT EXISTS room_read_state (...);
CREATE TABLE IF NOT EXISTS room_join_requests (...);
CREATE TABLE IF NOT EXISTS room_invites (...);
INSERT IGNORE INTO room_members(room_id,user_id,role,joined_at,join_message_id)
  SELECT 1, id, IF(id=4,2,0), NOW(), 0 FROM users;
```

`group_rules.cpp` implements the four functions with ASCII/UTF-8 character counts (reuse existing text length helpers if present).

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build chatbox/build --target test_group_rules && ctest --test-dir chatbox/build -R group_rules --output-on-failure`
Expected: PASS.

Also run: `powershell -File chatbox/tools/check_group_schema.ps1` after adding a source-level guard that both schema files contain all new tables/columns. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add chatbox/deploy/schema.sql chatbox/deploy/migrate_20261028_groups.sql \
  chatbox/server/group chatbox/tests/test_group_rules.cpp chatbox/CMakeLists.txt
git commit -m "feat(server): add durable group schema and rules"
```

---

### Task 2: 群协议编解码 + GroupService 创建/搜索/申请/审批

**Files:**
- Create: `chatbox/common/group_protocol.h`
- Create: `chatbox/common/group_protocol.cpp`
- Modify: `chatbox/common/protocol.h`
- Create: `chatbox/server/group/group_service.h`
- Create: `chatbox/server/group/group_service.cpp`
- Modify: `chatbox/server/main.cpp`
- Modify: `chatbox/server/business/worker_pool.h`
- Modify: `chatbox/server/business/worker_pool.cpp`
- Modify: `chatbox/CMakeLists.txt`
- Test: `chatbox/tests/test_group_protocol.cpp`

**Interfaces:**
- Commands:
  ```cpp
  kCmdGroupList = 0x00B0, kCmdGroupCreate = 0x00B1,
  kCmdGroupUpdate = 0x00B2, kCmdGroupDisband = 0x00B3,
  kCmdGroupSearch = 0x00B4, kCmdGroupJoinRequest = 0x00B5,
  kCmdGroupJoinRespond = 0x00B6, kCmdGroupInvite = 0x00B7,
  kCmdGroupInviteRespond = 0x00B8, kCmdGroupMemberList = 0x00B9,
  kCmdGroupMemberUpdate = 0x00BA, kCmdGroupRequestList = 0x00BB,
  kCmdGroupPush = 0x00BC, kCmdGroupMarkRead = 0x00BD
  ```
- `group_protocol` produces:
  ```cpp
  std::string encodeGroupList(const std::vector<GroupInfo>& groups);
  bool decodeGroupList(const std::string& body, std::vector<GroupInfo>& out);
  std::string encodeGroupMembers(uint64_t room_id, const std::vector<GroupMemberInfo>& members);
  bool decodeGroupMembers(const std::string& body, uint64_t& room_id, std::vector<GroupMemberInfo>& out);
  ```
  Plus request/response encoders for create/search/join/respond/invite/member update/request list.
- `GroupResult`:
  ```cpp
  struct GroupNotice { uint64_t user_id; std::string frame; };
  struct GroupResult {
      Frame response;
      bool broadcast = false;
      std::string broadcast_frame;
      std::vector<RoomMember> targets;
      std::vector<GroupNotice> notices;
  };
  ```
- `GroupService::handle(const Frame&, const RoomMember&)` returns `GroupResult`.

- [ ] **Step 1: Write failing protocol tests**

Add a round-trip test:
```cpp
void groupProtocolRoundTrips() {
    std::vector<chatbox::GroupInfo> groups(2);
    groups[0].room_id = 1; groups[0].name = "群聊A"; groups[0].group_no = "10000001";
    groups[0].owner_id = 4; groups[0].member_count = 12;
    groups[0].role = chatbox::GroupRole::kOwner; groups[0].unread = 3;
    groups[0].is_default = true;
    groups[1].room_id = 7; groups[1].name = "设计群"; groups[1].group_no = "10000002";
    groups[1].role = chatbox::GroupRole::kMember;
    const std::string body = chatbox::encodeGroupList(groups);
    std::vector<chatbox::GroupInfo> decoded;
    QVERIFY(chatbox::decodeGroupList(body, decoded));
    QCOMPARE(decoded.size(), 2u);
    QCOMPARE(decoded[0].group_no, std::string("10000001"));
    QVERIFY(decoded[0].is_default);
    QCOMPARE(decoded[1].role, chatbox::GroupRole::kMember);
}
```
Also test truncated bodies return false, extra trailing bytes are ignored.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_group_protocol`
Expected: compile failure.

- [ ] **Step 3: Implement protocol + service + dispatch**

- `ByteWriter`/`ByteReader` only; no Qt in `common`.
- `GroupService` in this task is a protocol/dispatch skeleton: `handle` validates the command and returns a `1001 not implemented` error for DB-backed operations. Task 3 fills the handlers and adds the `MysqlConnection` methods. Do not reference not-yet-declared DB methods here.
- `main.cpp`: treat `command >= kCmdGroupList && command <= kCmdGroupMarkRead` as `group_command`; submit to worker; call `context.group.handle(...)`; enqueue response; for `broadcast` use existing room multicast; for `notices` use `SessionManager::find(user_id)` and post the frame to the target IO thread.
- `WorkerContext` adds `GroupService& group`; `WorkerPool` creates it per worker.

- [ ] **Step 4: Run tests and build server**

Run:
```
cmake --build chatbox/build --target test_group_protocol chatbox_server
ctest --test-dir chatbox/build -R group_protocol --output-on-failure
```
Expected: PASS and `chatbox_server` builds.

- [ ] **Step 5: Commit**

```bash
git add chatbox/common chatbox/server/group chatbox/server/main.cpp \
  chatbox/server/business chatbox/tests/test_group_protocol.cpp chatbox/CMakeLists.txt
git commit -m "feat(server): add group protocol and service dispatch"
```

---

### Task 3: 群仓储与完整群行为

**Files:**
- Modify: `chatbox/server/db/mysql_connection.h`
- Modify: `chatbox/server/db/mysql_connection.cpp`
- Modify: `chatbox/server/group/group_service.cpp`
- Test: `chatbox/tests/test_group_service_logic.cpp`
- Create: `chatbox/tools/test_groups_integration.ps1`

**Interfaces:**
- `MysqlConnection` adds:
  ```cpp
  bool listGroupsForUser(uint64_t user_id, std::vector<GroupInfo>& out, std::string& error);
  bool findGroupByNumber(const std::string& group_no, GroupInfo& out, std::string& error);
  bool createGroup(const std::string& name, uint64_t owner_id, GroupInfo& out, std::string& error);
  bool updateGroupName(uint64_t room_id, const std::string& name, std::string& error);
  bool disbandGroup(uint64_t room_id, std::string& error);
  bool addRoomMember(uint64_t room_id, uint64_t user_id, GroupRole role, std::string& error);
  bool removeRoomMember(uint64_t room_id, uint64_t user_id, std::string& error);
  bool listRoomMembers(uint64_t room_id, std::vector<GroupMemberInfo>& out, std::string& error);
  bool getRoomMemberRole(uint64_t room_id, uint64_t user_id, GroupRole& out, std::string& error);
  bool setRoomMemberRole(uint64_t room_id, uint64_t user_id, GroupRole role, std::string& error);
  bool transferRoomOwner(uint64_t room_id, uint64_t new_owner, std::string& error);
  bool listJoinRequests(uint64_t room_id, std::vector<GroupRequestInfo>& out, std::string& error);
  bool createJoinRequest(uint64_t room_id, uint64_t user_id, const std::string& message, uint64_t& request_id, std::string& error);
  bool respondJoinRequest(uint64_t request_id, uint64_t reviewer_id, bool accept, std::string& error);
  bool createInvite(uint64_t room_id, uint64_t inviter_id, uint64_t invitee_id, std::string& error);
  bool respondInvite(uint64_t invite_id, uint64_t invitee_id, bool accept, std::string& error);
  bool markRoomRead(uint64_t room_id, uint64_t user_id, uint64_t message_id, std::string& error);
  bool unreadCount(uint64_t room_id, uint64_t user_id, uint32_t& out, std::string& error);
  bool isRoomMember(uint64_t room_id, uint64_t user_id, std::string& error);
  bool roomJoinMessageId(uint64_t room_id, uint64_t user_id, uint64_t& out, std::string& error);
  ```
- `GroupService` handles create/list/search/join/respond/invite/respond/member list/member update/request list/mark read/update/disband; all writes are prepared statements.
- On approve join: insert `room_members` with `join_message_id = max(messages.id)` for the room; set `room_read_state` to same; push `GROUP_PUSH(member_joined)` to online members and a notice to the applicant.
- On disband: only owner, not default; update `rooms.status=1`, delete `messages` for the room, delete files in Phase 2 (placeholder now); keep `room_members` rows; push dissolved to online members.
- On member removal/leave: delete membership; push; file cleanup not in Phase 1.

- [ ] **Step 1: Write failing service-logic tests**

Use pure helpers for role transitions and target validation; test:
- Admin cannot remove admin/owner; owner can remove admin.
- Owner cannot disband default; admin cannot disband.
- A user cannot join a dissolved group; cannot request twice.
- `join_message_id` must be set from the current max message id (helper `computeJoinCursor(max_message_id)` returns max).
- `unread` is `messages after max(join_message_id,last_read)` (helper `computeUnread(max_id, join_id, last_read)`).

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_group_service_logic`
Expected: compile failure.

- [ ] **Step 3: Implement repository and service**

All SQL uses `mysql_stmt_prepare`/`bind`; no string concatenation. For `listGroupsForUser`, one query joins `room_members` + `rooms` and a subquery for unread count. For group number generation, `SELECT MAX(id)+10000001` under a transaction or retry on duplicate key.

- [ ] **Step 4: Run unit tests and integration script**

Run:
```
cmake --build chatbox/build --target test_group_service_logic chatbox_server
ctest --test-dir chatbox/build -R group_service_logic --output-on-failure
```
Integration: `chatbox/tools/test_groups_integration.ps1` starts the server against a test DB, uses a small Python TCP client to register two users, create/search/apply/approve/invite/remove/disband, and asserts responses and DB counts. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add chatbox/server/db/mysql_connection.* chatbox/server/group/group_service.cpp \
  chatbox/tests/test_group_service_logic.cpp chatbox/tools/test_groups_integration.ps1
git commit -m "feat(server): implement group membership and lifecycle"
```

---

### Task 4: 默认群、注册自动加入、历史游标与旧客户端兼容

**Files:**
- Modify: `chatbox/server/auth/auth_service.cpp`
- Modify: `chatbox/server/auth/auth_service.h`
- Modify: `chatbox/server/room/room_service.cpp`
- Modify: `chatbox/server/history/history_service.cpp`
- Modify: `chatbox/server/history/history_service.h`
- Modify: `chatbox/server/main.cpp`
- Test: `chatbox/tests/test_default_group_logic.cpp`

**Interfaces:**
- `AuthService` registration inserts the user and `room_members(room_id=1,user_id,role=0,join_message_id=0)` in the same transaction; failure rolls back user creation.
- `HistoryService` group query uses `join_message_id` as the lower bound:
  ```cpp
  // room history: WHERE room_id=? AND id > join_message_id AND id < before_id
  ```
  Search uses the same lower bound.
- `RoomService::handleJoinRoom` checks `isRoomMember` before joining presence; old clients are members because migration/registration inserts them.
- `RoomService::handleChatSend` checks `isRoomMember`; default group remains fully compatible.
- `ROOM_LIST` returns only active groups with default first; old clients show the first (default).

- [ ] **Step 1: Write failing logic tests**

```cpp
void defaultGroupRules() {
    QCOMPARE(chatbox::defaultGroupId(), 1);
    QCOMPARE(chatbox::defaultGroupNumber(), std::string("10000001"));
    QVERIFY(chatbox::isDefaultGroupProtected(1));
    QVERIFY(!chatbox::isDefaultGroupProtected(2));
    QCOMPARE(chatbox::computeJoinCursor(0), 0u);
    QCOMPARE(chatbox::computeJoinCursor(42), 42u);
    QCOMPARE(chatbox::computeUnread(100, 10, 20), 80u);
    QCOMPARE(chatbox::computeUnread(100, 10, 90), 10u);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_default_group_logic`
Expected: compile failure.

- [ ] **Step 3: Implement registration transaction, membership checks, history cursor**

Add `defaultGroupId()`, `defaultGroupNumber()`, `isDefaultGroupProtected()`, `computeJoinCursor()`, `computeUnread()` to `group_rules`. Registration uses `START TRANSACTION`/`COMMIT` around user insert + membership insert; on any failure `ROLLBACK` and return an error.

- [ ] **Step 4: Run tests and a manual 0.2.5 compatibility check**

Run:
```
cmake --build chatbox/build --target test_default_group_logic chatbox_server
ctest --test-dir chatbox/build -R default_group_logic --output-on-failure
```
Manual: launch the existing 0.2.5 client against the test server, log in, select the default group, send text, load history. Expected: text send/history work; no crash from unknown group commands (client never sends them).

- [ ] **Step 5: Commit**

```bash
git add chatbox/server/auth chatbox/server/room/room_service.cpp \
  chatbox/server/history chatbox/server/main.cpp chatbox/server/group/group_rules.* \
  chatbox/tests/test_default_group_logic.cpp
git commit -m "feat(server): protect default group and scope history by join cursor"
```

---

### Task 5: 客户端群模型与协议处理

**Files:**
- Create: `chatbox/client/core/group_types.h`
- Create: `chatbox/client/core/group_store.h`
- Create: `chatbox/client/core/group_store.cpp`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/tests/tests.pro`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Client models:
  ```cpp
  enum class GroupRole { kMember = 0, kAdmin = 1, kOwner = 2 };
  struct GroupInfo { quint64 room_id; QString name; QString group_no; quint64 owner_id;
                     int member_count; GroupRole role; int unread; bool is_default; int status; };
  struct GroupMemberInfo { quint64 user_id; QString nickname; GroupRole role; bool online; };
  struct GroupRequestInfo { quint64 request_id; quint64 room_id; quint64 user_id;
                            QString nickname; QString message; int status; };
  ```
- `GroupStore`:
  ```cpp
  void replaceAll(const QVector<GroupInfo>& groups);
  void upsert(const GroupInfo& group);
  const GroupInfo* find(quint64 room_id) const;
  QVector<GroupInfo> active() const;
  QVector<GroupInfo> dissolved() const;
  void setMembers(quint64 room_id, const QVector<GroupMemberInfo>& members);
  QVector<GroupMemberInfo> members(quint64 room_id) const;
  void setUnread(quint64 room_id, int unread);
  ```
- `MainWindow` slots: `requestGroupList()`, `onGroupList(const QByteArray&)`, `onGroupMembers(...)`, `onGroupPush(...)`, `onGroupRequestList(...)`, `onGroupSearchResult(...)`, `onGroupActionResponse(...)`.
- Frame handler cases for `kCmdGroup*`; send helpers `sendGroupCommand(cmd, payload)`.

- [ ] **Step 1: Write failing tests**

Add tests that encode a group list body with `chatbox::encodeGroupList`, feed it to a `GroupStore`, and assert active/dissolved split, default first, unread update, and member replacement. Add a source guard that `main_window_net.cpp` handles `kCmdGroupList`/`kCmdGroupMemberList`/`kCmdGroupPush`.

- [ ] **Step 2: Run to verify failure**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: compile failure.

- [ ] **Step 3: Implement models/store and network handlers**

Use `common/group_protocol` decoders; convert `std::string` to `QString::fromUtf8`. Errors set status and log; malformed frames do not crash. `GROUP_PUSH` updates group/member/request state and refreshes the group list/panel.

- [ ] **Step 4: Run tests**

Run: `powershell -ExecutionPolicy Bypass -File chatbox/client/tests/run_client_tests.ps1`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/core/group_* chatbox/client/ui/main_window.h \
  chatbox/client/ui/main_window_net.cpp chatbox/client/chatbox.pro \
  chatbox/client/tests/tests.pro chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): add group models and protocol handlers"
```

---

### Task 6: 左栏“会话/群组”Tab、群列表、创建/加群对话框

**Files:**
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window.cpp`
- Create: `chatbox/client/ui/group_create_dialog.h/.cpp`
- Create: `chatbox/client/ui/group_join_dialog.h/.cpp`
- Create: `chatbox/client/ui/main_window_groups.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Object names: `SidebarTabs`, `GroupList`, `CreateGroupButton`, `JoinGroupButton`, `GroupNoticeButton`, `GroupNoticeBadge`.
- `GroupCreateDialog(QWidget*)` signal `createRequested(const QString& name)`; validates 1–32 chars.
- `GroupJoinDialog(QWidget*)` signal `joinRequested(const QString& group_no, const QString& message)`; validates 8 digits.
- `MainWindow::selectGroup(quint64 room_id, const QString& name)` calls `selectRoom`, updates `context_label_`, requests `GROUP_MEMBER_LIST`, and marks read.
- `onGroupList` populates group rows: default pinned first, role badge, member count, unread badge; dissolved groups in a separate “已退出/已解散” section.
- The visible `room_list_` is removed; `group_list_` replaces it. The 0.3.0 client stops sending `ROOM_LIST`; the server retains the handler for 0.2.5. Update `selectRoom`/`clearStaleListSelection` to clear `group_list_` instead of `room_list_`.

- [ ] **Step 1: Write failing UI tests**

Assert `MainWindow` has `SidebarTabs` with two tabs “会话”“群组”; `GroupList` exists; create/join dialogs expose the object names and emit signals on valid input; invalid group number does not emit.

- [ ] **Step 2: Run to verify failure**

Run: `run_client_tests.ps1`; expected compile/test failure.

- [ ] **Step 3: Implement left tabs and dialogs**

Replace the “群聊” label + `room_list_` block with the tab widget. Keep `room_list_` out of the visible layout (or remove it; update `selectRoom`/`clearStaleListSelection` to use `group_list_`). The “会话” page wraps the existing conversation list/empty state. The “群组” page contains the notice row, group list, and create/join buttons. On group row click, call `selectGroup`; on double-click, open the group chat. Disable create/join while offline.

- [ ] **Step 4: Run tests and build client**

Run: `run_client_tests.ps1` and `build_client.ps1`; expected 0 failures, build 0.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/ui/main_window* chatbox/client/ui/group_* \
  chatbox/client/chatbox.pro chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): add group tabs and create/join flows"
```

---

### Task 7: 群设置、成员管理、申请/邀请通知

**Files:**
- Create: `chatbox/client/ui/group_settings_dialog.h/.cpp`
- Create: `chatbox/client/ui/group_notify_dialog.h/.cpp`
- Modify: `chatbox/client/ui/main_window_groups.cpp`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/chatbox.pro`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- `GroupSettingsDialog` signals:
  ```cpp
  void renameRequested(quint64 room_id, const QString& name);
  void disbandRequested(quint64 room_id);
  void memberActionRequested(quint64 room_id, quint64 user_id, int action);
  void requestActionRequested(quint64 request_id, bool accept);
  ```
  Actions: 1=remove, 2=set_admin, 3=unset_admin, 4=transfer_owner.
- `GroupNotifyDialog` shows pending join requests/invites; owner/admin can approve/reject; invitee can accept/decline.
- Visibility: gear only for owner/admin; disband only owner and not default; role badges in the member list.

- [ ] **Step 1: Write failing tests**

Construct dialogs with owner/admin/member roles; assert gear/disband/role actions visibility and emitted signals. Assert default group hides disband.

- [ ] **Step 2: Run to verify failure**

Run: `run_client_tests.ps1`; expected failure.

- [ ] **Step 3: Implement dialogs and actions**

Wire gear in the right panel/group panel; after each action, send the command and wait for `GROUP_PUSH`/response, then refresh. Confirm destructive actions with QMessageBox.

- [ ] **Step 4: Run tests/build**

Run: `run_client_tests.ps1`, `build_client.ps1`; expected pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/ui/group_settings_dialog.* chatbox/client/ui/group_notify_dialog.* \
  chatbox/client/ui/main_window_groups.cpp chatbox/client/ui/main_window.h \
  chatbox/client/chatbox.pro chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): add group settings and request handling"
```

---

### Task 8: 右侧栏 B（成员/资料、栏内搜索、文件占位）

**Files:**
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window.cpp`
- Create: `chatbox/client/ui/main_window_panel.cpp`
- Modify: `chatbox/client/ui/main_window_profile.cpp`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Right panel object names: `RightSidebar`, `RightHeader`, `RightSearchEdit`, `RightMemberList`, `RightFileButton`, `RightFilesPage`, `RightSearchResults`, `RightProfileCard`, `RightSettingsButton`, `RightBackButton`.
- `⋯` becomes `QToolButton` with `clicked` -> `toggleRightPanel()`; no `QMenu`.
- `toggleRightPanel()`: if no context, no-op; otherwise flip `member_panel_visible_`, save `settings/member_panel`, show/hide.
- Group mode: header group name + member count; member list from `GroupStore` (durable members; online flag), search at top; file button bottom -> `RightFilesPage` placeholder “文件功能将在下一阶段开放”.
- Private mode: profile card from `UserProfile`/avatar cache; `selectPrivate` requests `requestProfile(peer_id)` so the card has data; search at bottom; file button; “查看资料” opens existing `ProfileDialog`.
- Search: input + Enter/button sends `MESSAGE_SEARCH`; results replace member list; click calls `jumpToMessage(message_id)` and restores the panel.

- [ ] **Step 1: Write failing UI tests**

Assert `RightSidebar` exists; `⋯` has no menu; group mode shows `RightMemberList` and `RightFileButton`; private mode shows `RightProfileCard`; search results list exists; clicking a result emits/calls jump; `RightFilesPage` placeholder text. Assert `member_list_` is still used for @ mention (existing source guard).

- [ ] **Step 2: Run to verify failure**

Run: `run_client_tests.ps1`; expected failure.

- [ ] **Step 3: Implement the panel and search mode**

Move the existing member list into the group home page; keep the same `member_list_` pointer for @ mention/context menus. Use a `QStackedWidget` for home/files/search. Update `updateRightPanelVisibility` to show for Room or Private when toggled. Keep panel width 260 and save splitter sizes on close.

- [ ] **Step 4: Run tests/build**

Run: `run_client_tests.ps1`, `build_client.ps1`; expected pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/ui/main_window* chatbox/client/chatbox.pro \
  chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): add right sidebar with members, profile and search"
```

---

### Task 9: 上滑加载、搜索跳转、移除“加载更早消息”

**Files:**
- Modify: `chatbox/client/ui/main_window.cpp`
- Modify: `chatbox/client/ui/main_window_chat.cpp`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/ui/main_window.h`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- `MainWindow::maybeLoadOlder()`: if `has_more_`, `!loading_older_history_`, context selected, scroll value <= 40 -> request older or `loadOlderFromLocal()`.
- `ScrollMode::kPreserveAnchor`: before prepend record `scrollBar()->maximum()`, after render set `value += newMax - oldMax`.
- `jumpToMessage(quint64 message_id)`: if message is loaded, scroll/highlight; else `requestHistory(kind,target,message_id+1)` and set `pending_jump_message_id_`; when the page arrives, scroll/highlight.
- Remove `load_more_action_`, `show_members_action_`, `more_menu_` and their UI tests; keep `updateRightPanelVisibility`.

- [ ] **Step 1: Write failing tests**

Source/behavior tests: scrollbar valueChanged calls `maybeLoadOlder`; no “加载更早消息” action; `kPreserveAnchor` exists and adjusts by height delta; duplicate scroll does not send two history frames; search result click sets `pending_jump_message_id_`.

- [ ] **Step 2: Run to verify failure**

Run: `run_client_tests.ps1`; expected failure.

- [ ] **Step 3: Implement infinite scroll and jump**

Change `main_window_net.cpp` older-page handler to use `kPreserveAnchor`; replace the old menu action. Guard stale responses with `currentContextMatches` and clear `pending_jump_message_id_` on context reset. Remove menu construction and update tests.

- [ ] **Step 4: Run tests/build**

Run: `run_client_tests.ps1`, `build_client.ps1`; expected pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/ui/main_window* chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): load older messages on scroll and jump to search hits"
```

---

### Task 10: 迁移演练、旧客户端兼容与验收

**Files:**
- Create: `chatbox/tools/test_groups_acceptance.ps1`
- Modify: `chatbox/client/README.md`
- Modify: `chatbox/README.md`
- Modify: `chatbox/docs/public-access.md`（仅当部署步骤变化）
- Test: manual + script

**Interfaces:**
- Script starts a staging server against a disposable MySQL schema, applies `schema.sql` + migration, runs two test clients (or a Python TCP client), and verifies:
  - new user auto-joins default group;
  - create/search/apply/approve/invite/roles/remove/disband;
  - default group cannot disband/leave;
  - history join cursor and unread counts;
  - 0.2.5 client can still text-chat in default group and read history;
  - dissolved group keeps local history and shows `status=dissolved`.

- [ ] **Step 1: Write the acceptance script**

Use a disposable database name (`chatbox_test_groups`), a temporary server config/port, and the existing server binary. Clean up the DB/schema at the end. Fail on any assertion.

- [ ] **Step 2: Run migration on a copy of production schema**

Take a `mysqldump --no-data` copy, apply migration, verify room 1 exists, rooms 2–12 are dissolved, all users are members, and `join_message_id=0` for migrated members. Expected: PASS.

- [ ] **Step 3: Run two-client acceptance**

Expected: group operations and unread correct; old 0.2.5 client still works in default group.

- [ ] **Step 4: Run full regression**

Run:
```
powershell -File chatbox/client/tests/run_client_tests.ps1
cmake --build chatbox/build
ctest --test-dir chatbox/build --output-on-failure
```
Expected: all pass.

- [ ] **Step 5: Commit and request review**

```bash
git add chatbox/tools/test_groups_acceptance.ps1 chatbox/client/README.md \
  chatbox/README.md chatbox/docs/public-access.md
git commit -m "test: add group migration and two-client acceptance"
```

Then request a whole-branch review before merge.

---

## Self-Review

- **Spec coverage:** Phase 1 covers groups, membership, roles, default group, join/invite/approve, disband cleanup for messages (file cleanup deferred to Phase 2), unread, left tabs, right panel B, search, infinite scroll, compatibility. File entry is a placeholder; Phase 2 covers transfer/cards/file manager/images; Phase 3 covers video.
- **Step granularity:** each task has failing test, implementation, verification, commit; DB integration is a script because the repo has no MySQL CI harness.
- **Type consistency:** `GroupInfo`/`GroupMemberInfo`/`GroupRequestInfo` and `GroupRole` are defined once in server and once in client (wire-compatible, same numeric values); protocol encoders are the single wire authority.
- **Review Focus:** five failure modes each have a task/test: Task 4 default group/migration, Task 3 revocation/cursor, Task 4 old client, Task 9 scroll anchor, Task 8 panel regression.
- **Non-goals:** no files/video in Phase 1; no HTTP service; no new third-party dependency.
