# ChatBox Android 底座设计（多设备会话 / 离线同步 / 前台推送 / Qt 6 工程）

日期：2026-10-29
状态：已通过（2026-10-29）

## 1. 背景与目标

ChatBox 目前是 Windows Qt 5.15 桌面客户端 + Linux C++17 服务端（epoll ET、MySQL、nginx TLS）。服务端在会话层是**单会话模型**：同一账号新登录会撤销旧 token 并踢掉旧连接；设计文档也把移动端列为非目标。

现在要补上 Android 客户端，定位与 QQ/Telegram 一致：人不在电脑旁时，掏出手机继续用同一个账号聊天。手机端最终要与桌面端功能大致对齐；本次先做**底座**，不做完整手机 UI。

“企业级”沿用 2026-10-21 公网部署设计的定义：**可运维、可恢复、有边界**，不是无限扩容或 99.9% SLA。当前实际用户是朋友几个人，但注册开放、APK 公开下载，所以底座要按多设备、可审计、可回滚来做。

## 2. 目标与非目标

### 目标

- **G1 多设备同时在线**：同一账号的 Windows 桌面和 Android 可同时登录，互不踢；在线状态按“是否还有设备在线”聚合。
- **G2 可恢复同步**：手机断网/被杀/重启后，按会话游标补齐消息；已读状态在多设备间同步；发送方其他设备能实时看到自己发出的消息。
- **G3 安卓后台通知**：锁屏和 app 在后台时能收到消息通知；划掉任务后尽量保活；开机自启为尽力而为。
- **G4 安卓工程底座**：Qt 6.8 LTS + QML 工程可构建、可安装、可登录、可 TLS 连接、可心跳、可收推送、可弹通知；网络与存储在独立 service 进程，UI 进程只做展示。
- **G5 桌面兼容**：现有 Qt 5.15 客户端功能不回归；旧版 0.4.0 在新服务端上仍能登录聊天；新版桌面只做多设备所需的最小适配。
- **G6 协议可演进**：新命令全部是追加式；用能力位协商，旧客户端不会收到看不懂的推送。
- **G7 可运维**：数据库迁移可重复执行、可回滚；有测试、日志、指标和验收脚本。

### 非目标

- 本次不做完整手机 UI：只做登录 + 会话列表 + 1:1 文本聊天骨架；群管理、文件、搜索、资料页、主题设置等在后续阶段做。
- 不做 iOS、Web、桌面端重写。
- 不做端到端加密、语音/视频、消息编辑、朋友圈/动态。
- 不接小米/华为/OPPO/vivo 厂商推送，不接 FCM；只留可插拔接口。
- 不上应用商店；第一版只发官网 APK。
- 不做内容审核、举报、封禁后台；开放注册的治理沿用现有每 IP 限流。
- 不做多机水平扩展、负载均衡、消息队列。
- 不改变现有 TLS 拓扑和 CA 信任模型。

## 3. 现状与约束

| 项目 | 现状 |
| --- | --- |
| 服务端 | Linux Ubuntu，C++17，epoll ET，IO 线程 + 业务线程池 |
| 数据库 | MySQL 8.4（部署机），业务账号只有 DML 权限 |
| TLS | nginx stream 终结 TLS，客户端用 `certs/ca.crt` 校验 IP SAN |
| 桌面客户端 | Qt 5.15.2 Widgets + 原生 Winsock + OpenSSL，回归 204 passed |
| 会话 | `sessions` 表按 token 存；登录时 `revokeSessionsForUser` 撤销该账号全部会话 |
| 在线路由 | `SessionManager` 每个 user 只保存一个 `SessionBinding` |
| 群在线成员 | `RoomManager` 每个群每个 user 只保存一个连接 |
| 消息 | 房间 `messages`、私聊 `private_messages`，消息 ID 按会话独立递增 |
| 已读 | `private_read_state` / `room_read_state` 按 user 存，已经是账号级 |
| 历史 | 房间历史支持 after/before；私聊历史只有 before；返回按 ID 升序 |
| 部署 | 宿舍笔记本 + SakuraFrp，服务不保证 24 小时在线 |
| 客户端 token | 7 天有效期，RESUME 复用同一个 token |

关键矛盾：

1. 手机一登录就会把电脑踢下线，无法“手机补位”。
2. 手机后台被杀后重连，需要知道每个会话缺了哪些消息；现有私聊历史没有 after 查询。
3. 桌面和手机同时在线时，服务端推送只发给一个连接，另一台设备收不到。
4. 一台设备读了消息，另一台设备的未读角标不会变。
5. 安卓没有系统级推送通道，自建服务端 + 国内 ROM 只能靠前台服务保活。

## 4. 方案总览

```text
┌──────────────────────────── Android app（同一 APK） ────────────────────────────┐
│                                                                                │
│  UI 进程（Qt Quick/QML）             service 进程（QtService，无界面）           │
│  - 登录页/会话页/通知点击跳转          - TLS 连接、心跳、重连                     │
│  - 只读快照与事件                     - 协议编解码、认证、token                  │
│  - 不直接碰 socket                    - SQLite 消息/会话/发件箱                   │
│         │                             - 同步游标、已读、通知                     │
│         └──── QLocalSocket + JSON ────┘                                         │
└───────────────────────────────────────┬────────────────────────────────────────┘
                                        │ TLS（CA 固定）
                                        ▼
                              nginx stream :9443
                                        │ PROXY v2
                                        ▼
                              chatbox_server :9100
                                        │
                                        ▼
                                     MySQL
                                        ▲
                                        │ TLS
                              Windows 桌面客户端（Qt 5.15）
```

设计要点：

- Android 采用**双进程**：service 持有唯一一条业务连接，UI 进程通过本地 socket 取数据和发命令。这样锁屏/划掉界面时连接仍在 service 进程，通知也由 service 发；UI 重开只需重连本地 socket，不会产生第二条服务端会话。
- 桌面端保持单进程架构，不引入 service；只增加多设备所需的新推送处理。
- 服务端从“user → 一个连接”改为“user → 多个连接”，但群成员、在线状态、已读仍然是 user 级。
- 消息同步以**每个会话的 last_message_id 游标**为准；控制状态以服务端数据库为准，重连时重新拉列表校正。

## 5. 关键设计决定

### 5.1 多设备会话模型

#### 设备身份

- 客户端首次启动生成随机 UUID v4 作为 `device_id`，持久化在安全存储里；不使用 IMEI、MAC、ANDROID_ID 等硬件标识。
- `device_name` 默认取手机型号（如 `Pixel 8`）或桌面主机名，最长 64 字节 UTF-8；允许后续在设置里改。
- `platform` 取 `android` / `windows`；`app_version` 取客户端版本。
- `session_id` 由服务端生成 UUID，用于会话列表和单会话注销；不暴露 token。

#### 数据表

`sessions` 增加：

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `session_id` | CHAR(36) | 服务端生成的会话 ID，唯一 |
| `device_id` | VARCHAR(36) | 客户端设备 ID；旧客户端为空 |
| `device_name` | VARCHAR(64) | 展示名 |
| `platform` | VARCHAR(16) | `android` / `windows` |
| `app_version` | VARCHAR(32) | 客户端版本 |
| `caps` | INT UNSIGNED | 客户端能力位 |
| `last_seen_at` | DATETIME | 最近一次登录/恢复/心跳刷新 |

`revoked` 语义扩展：

- `0` 有效
- `1` 被新登录替换（旧行为，保留）
- `2` 主动登出
- `3` 同设备重新登录替换
- `4` 被其他设备注销
- `5` 超过设备上限被淘汰

#### 登录与恢复

- **新客户端登录**：body 追加设备信息和能力位；服务端为本次登录生成 `session_id`，先注销同一 `(user_id, device_id)` 的有效会话（reason 3），再插入新会话。
- **旧客户端登录**：body 没有设备信息，按 `device_id = ''` 处理；只注销该账号下其他 `device_id = ''` 的有效会话，保持旧客户端之间的单会话行为，但**不影响手机或新桌面**。
- **恢复（RESUME）**：校验 token 后刷新 `last_seen_at` 和 `caps`；如果请求带设备信息且该会话仍是旧的 `device_id=''`，就地升级为新的设备信息（不强制重新登录）。如果同一个 token 已经从另一条连接恢复，关闭旧连接（resume takeover），这是重连去重，不是多设备冲突。
- **设备上限**：每个账号最多 5 个**新客户端**有效会话；超过时淘汰 `last_seen_at` 最旧的会话（reason 5），并关闭其连接。旧客户端会话（`device_id=''`）不计入这个上限，也不会被新客户端淘汰；它们只受自己的“旧客户端登录替换旧客户端会话”规则约束，并在 30 天后自然过期。这样旧 0.4.0 登录不会因为账号已有 5 台新设备而踢掉手机。
- **token 有效期**：从 7 天提高到 30 天；每次 RESUME 滑动续期到“现在 + 30 天”。注销/被踢立即失效。
- **last_seen 刷新**：登录/RESUME 时同步写库；连接期间不做“每个 PING 一次 DB 写”，而是由后台任务每 10 分钟对活跃会话做一次批量 touch，同时滑动 `expires_at`。这样不会在 IO 回调里做阻塞 DB 操作，长期在线的设备也不会因为 `last_seen_at` 太旧被误淘汰。
- **登出**：只注销当前会话；其他设备不受影响。
- **注销指定设备**：按 `session_id` 注销；如果目标在线，关闭其连接并返回 1010/1002 语义的错误。

#### 终止错误映射

关闭连接时按原因返回明确错误，客户端据此决定“清 token 回登录页”还是“短暂重连”：

| 原因 | 错误码 | 消息 | 客户端行为 |
| --- | --- | --- | --- |
| 同设备重新登录（reason 3） | 1010 | `session replaced` | 终止；不自动重连 |
| 被其他设备注销（reason 4） | 1002 | `session revoked` | 终止；清 token；回登录页 |
| 超过设备上限（reason 5） | 1011 | `too many devices` | 终止；清 token；回登录页并提示 |
| RESUME 抢占旧连接 | 1010 | `session taken over` | 旧连接终止；不自动重连；token 仍有效 |
| 旧客户端被新登录替换（reason 1） | 1010 | `kicked by new login` | 保持旧客户端现有行为 |

1002/1010/1011 都视为**终止错误**，service 不得用同一个 token 循环重连；只有网络断开、超时、服务端 2001 等才进入指数退避重连。

#### 在线状态

- `SessionManager` 从 `user → binding` 改为 `user → {connection_id → binding}`。
- 只有**第一条**会话加入时产生“上线”事件；只有**最后一条**会话移除时产生“离线”事件。中间设备增减不改变在线状态，避免好友列表闪烁。
- `presence_seq` 仍然按 user 递增，只在状态真正变化时分配。
- 好友列表、私聊会话列表、群成员列表的在线标记都用“该 user 是否还有任意会话在线”。

#### 会话管理协议

新增命令：

| 命令 | 值 | 方向 | 用途 |
| --- | --- | --- | --- |
| `kCmdSessionList` | 0x00D0 | C→S | 列出当前账号有效会话 |
| `kCmdSessionRevoke` | 0x00D1 | C→S | 注销指定 `session_id` |
| `kCmdReadStatePush` | 0x00D2 | S→C | 已读状态同步到同账号其他设备 |
| `kCmdSyncPull` | 0x00D3 | C→S | 按会话游标拉缺口消息 |
| `kCmdSyncResp` | 0x00D4 | S→C | 同步响应 |
| `kCmdMessageEchoPush` | 0x00D5 | S→C | 私聊消息回显给发送方其他设备 |

会话列表响应：

```text
u16 count
per session:
  string session_id
  string device_id
  string device_name
  string platform
  string app_version
  string created_at          // UTC 字符串，和现有时间格式一致
  string last_seen_at
  u8     current             // 是否当前连接
```

会话注销请求：`string session_id`。注销当前会话允许，但服务端先回响应再关闭连接；客户端也可以直接走 LOGOUT。

### 5.2 服务端在线路由与群成员

#### 会话路由

- `SessionManager` 增加：
  - `add(user_id, binding) -> became_online`
  - `removeConnection(connection_id) -> went_offline`
  - `removeSession(user_id, session_id) -> removed bindings`
  - `findAll(user_id) -> vector<SessionBinding>`
  - `findBySession(session_id) -> binding`
- `AuthResult` 不再返回单个 `old_binding`，改为返回需要关闭的连接列表；同设备替换、超限淘汰、指定注销都走同一套关闭逻辑。
- 推送路由统一为“按 user 找全部 session，再按 io_thread 分组，postMulticast”。

#### 群成员

`RoomManager` 从“每群每 user 一个连接”改为“每群每 user 多个连接”：

- `members` 以 `connection_id` 为键；另建 `user_id → connections` 索引。
- `snapshot` 按 user 去重，成员列表仍然一人一行；在线人数按 user 计数。
- `join` 幂等：同一连接重复加入直接成功；同一 user 的第二台设备加入不增加人数，也不触发人数上限。
- `leave`（退群）移除该 user 的全部连接；`removeConnection` 只移除当前连接。
- 群广播目标改为该群全部连接，而不是每 user 一个代表连接。

这样手机和电脑都在群里时，群消息、成员变更、文件通知都能到达两台设备；手机没加入的群不会收到广播，所以移动端登录后会像桌面一样对所有 active 群发 `kCmdJoinRoom`。

#### 推送路由改动清单

| 场景 | 现在 | 改成 |
| --- | --- | --- |
| 私聊消息 | 接收方单个连接 | 接收方全部 session；发送方其他 session 另发 echo |
| 群消息 | 群内每 user 一个连接 | 群内全部连接 |
| 好友申请/接受 | 目标 user 单个连接 | 目标 user 全部 session |
| 群通知/邀请/审批 | 目标 user 单个连接 | 目标 user 全部 session |
| 资料更新 | 目标连接列表 | 按 user 去重后全部 session |
| 文件通知 | 目标 user 单个连接 | 目标 user 全部 session |
| 在线状态 | 所有会话广播 | 保持，但排除该 user 自己的全部 session |

### 5.3 同步模型

#### 原则

- **服务端是唯一权威**：消息、已读、好友、群成员、资料都以 MySQL 为准。
- **消息按会话游标补齐**：客户端本地保存每个会话的 `last_message_id`；重连后只拉 `> last_message_id` 的消息。
- **控制状态重拉校正**：重连时重新拉群列表、私聊会话列表、好友列表、资料；不做全量事件日志。
- **幂等应用**：消息按 `(kind, target_id, message_id)` upsert；发送按 `client_msg_id` 幂等。
- **最近页校正**：重连时对每个有本地记录的会话额外拉最近一页（最多 50 条）做合并，用来修正离线期间发生的撤回/文件删除；超过最近一页的旧消息变更不保证离线校正（撤回窗口本身只有 2 分钟，接受这个边界）。

#### 同步顺序

客户端登录/恢复成功后：

1. 拉 `kCmdGroupList`、`kCmdPrivateConversations`、`kCmdFriendList`、自己的 `kCmdProfileGet`。
2. 对本地已有 `last_message_id > 0` 的会话发 `kCmdSyncPull`。
3. 对每个会话再拉最近一页（走现有 `kCmdHistory` 查询，不并进 sync pull），合并去重。
4. 进入在线状态，处理推送；UI 显示“同步中”直到第 2、3 步完成。

#### `kCmdSyncPull` 编码

请求：

```text
u16 conversation_count
u16 start_index          // 从第几个会话开始，首次为 0
per conversation:
  u8  kind          // 1 room, 2 private
  u64 target_id
  u64 last_message_id
```

响应：

```text
u16 conversation_count   // 原样回显，便于校验
u16 next_index           // 下次从哪个会话继续；全部处理完等于 conversation_count
u8  has_more             // 是否还有未处理的会话
per conversation:        // 只包含本次实际处理的会话
  u8  kind
  u64 target_id
  u16 message_count
  u8  has_more_messages  // 该会话自身是否还有缺口
  u64 next_after
  messages...                 // 复用历史消息编码
  u16 file_meta_count
  per meta:
    u16 message_index
    FileMeta...
```

- 服务端从 `start_index` 顺序处理会话；单会话单次最多 50 条，总响应预算 480 KiB（低于 512 KiB 出帧上限，留编码余量）。如果加入下一个会话会超预算，就停在当前会话，返回 `next_index` 和 `has_more=1`。
- 客户端收到 `has_more=1` 时，用同一个会话列表、`start_index=next_index` 再发一次；单个会话的 `has_more_messages=1` 时，用 `next_after` 再次拉该会话。
- 单会话补齐上限 500 条/轮；超过时先显示最新页，剩余缺口在用户打开会话时继续补。

- 服务端新增按 `id ASC LIMIT limit+1` 的查询：
  - `listRoomHistoryAfter(room_id, after_id, limit)`
  - `listPrivateHistoryAfter(user_id, peer_id, after_id, limit)`
- 现有 `listRoomHistory` 的 after 参数是“在窗口内取最新 N 条”，不适合补齐缺口，不能直接复用。
- 私聊历史没有 after，需要新增查询；房间查询也要新增 ASC 版本，避免改变现有历史分页语义。

#### 已读同步

- `kCmdPrivateMarkRead` / `kCmdGroupMarkRead` 更新数据库后，服务端向该 user 的**其他** session 发 `kCmdReadStatePush`。
- 历史页首次打开时服务端自动标记私聊已读的路径（`before_id == 0`）也要发同样的推送。
- 推送 body：

```text
u8  kind
u64 target_id
u64 last_read_message_id
u32 unread
```

- 其他设备收到后：更新该会话的 `last_read_message_id` 和未读数；如果当前正打开该会话，清角标；不重复向服务端发 mark read（避免回环）。

#### 消息回显

- 私聊发送成功后，服务端向发送方**除 origin 之外**的 session 发 `kCmdMessageEchoPush`，body 带 `kind=2`、`target_id=peer_id`、完整消息字段、`origin_device_id`。
- 群消息不额外发 echo：群广播已经覆盖发送方的其他已加入群的 session；客户端按 `message_id` 去重。
- 旧桌面（caps 不含 echo）不会收到 echo；手机发的私聊消息在旧桌面要等重新打开会话拉历史才可见。这是明确的兼容性降级。
- 新桌面处理 echo：如果当前会话匹配，追加到消息列表；否则更新会话摘要。发送方自己的消息不计未读。

#### 发件箱

- 移动端 service 把待发消息写入 SQLite `outbox`，带 `client_msg_id`；发送成功拿到服务端 `message_id` 后转为正式消息并删除 outbox 行。
- 断网/进程重启后，service 按 `client_msg_id` 重发；服务端现有 `uk_*_client` 唯一键保证幂等。
- 旧桌面的 outbox 仍只在内存，本次不改其行为。

### 5.4 Android 后台与推送

#### 双进程职责

- **service 进程**（`:qt_service`，`QtService`）：唯一持有服务端 TCP/TLS 连接；负责认证、心跳、重连、同步、SQLite、已读、通知、发件箱。无 UI。
- **UI 进程**（`QtActivity` + Qt Quick）：登录页、会话页、聊天页；通过 `QLocalSocket` 向 service 发命令、收快照和事件；不直接连服务端。
- 好处：划掉任务/旋转屏幕/UI 崩溃不会断连接；服务端只看到一个设备会话；通知在 service 里发，不依赖 UI 存活。

#### 前台服务

- service 继承 `org.qtproject.qt.android.bindings.QtService`，manifest 声明：

```xml
<service
    android:name=".ChatBoxService"
    android:process=":qt_service"
    android:exported="false"
    android:foregroundServiceType="remoteMessaging">
  <meta-data android:name="android.app.lib_name"
             android:value="-- %%INSERT_APP_LIB_NAME%% --"/>
  <meta-data android:name="android.app.arguments" android:value="-service"/>
  <meta-data android:name="android.app.background_running" android:value="true"/>
</service>
```

- `onCreate` 创建通知渠道并调用 `startForeground`；`onStartCommand` 返回 `START_STICKY`；`onTaskRemoved` 不停止；`onDestroy` 清理通知。
- 权限：`INTERNET`、`ACCESS_NETWORK_STATE`、`FOREGROUND_SERVICE`、`FOREGROUND_SERVICE_REMOTE_MESSAGING`、`POST_NOTIFICATIONS`、`RECEIVE_BOOT_COMPLETED`。
- 常驻通知单独一条低优先级“ChatBox 正在保持连接”，消息通知走另一条高优先级渠道；不要把每条消息都做成前台通知。
- Android 13+ 在 UI 首次启动时请求通知权限；拒绝时仍可聊天，但锁屏通知不可用，设置页给出说明。
- Android 14+ 必须声明 `foregroundServiceType`；使用 `remoteMessaging`，避免 `dataSync` 的时长限制。
- Android 15 对 `BOOT_COMPLETED` 启动前台服务有额外限制；开机自启按“尽力而为”实现：能启动就启动，捕获异常并记录，不承诺所有 ROM 都能自启。
- 电池优化：设置页提供“打开电池优化设置”的入口，引导用户手动把 ChatBox 设为不优化；不自动申请高风险权限。
- 国产 ROM 的“自启动/后台运行”白名单只能引导，无法代码保证。

#### 通知

- 每个会话一条通知，按会话 ID 分组；群消息可折叠成摘要。
- 默认显示“发送者/群名 + 内容预览”；设置里可改为“仅显示有新消息”。
- 如果 UI 正在前台且正打开该会话，service 不弹通知；UI 通过 `set_active` 告诉 service 当前会话。
- 点击通知打开 UI 并带上 `kind` / `target_id`；service 在 UI 连上后补发快照。
- 通知 ID 稳定映射到会话，避免重复堆叠；撤回/删除时撤销对应通知。

#### 推送接口

- 客户端定义 `NotificationSink` 接口，当前实现是 `AndroidNotificationSink`；未来厂商推送实现同一接口。
- 服务端定义 `PushGateway` 接口和 `NoopPushGateway`；在“目标 user 没有任何在线 session”时调用，当前只记录指标，不发送。将来接厂商通道时，在这里加 provider，不改业务层。
- 本次不注册厂商 token，不引入任何厂商 SDK。

### 5.5 Android 工程与共享核心

#### 目录

```text
chatbox/
  common/                 # 现有协议、群/文件编解码（继续共用）
  clientcore/             # 新增：两端共用的 C++ 核心
    transport/            # ITransport + 桌面原生实现 + Android QSslSocket 实现
    session/              # 登录、RESUME、设备信息、能力位、session 管理
    sync/                 # 会话游标、同步合并、去重
    store/                # SQLite schema / 迁移 / 会话与消息读写
    model/                # 消息、会话、好友、群 DTO
  mobile/
    CMakeLists.txt
    src/main.cpp          # -service 分支 / UI 分支
    qml/                  # 登录、连接状态、会话列表、1:1 文本聊天
    android/              # Manifest、ChatBoxService、通知、BootReceiver、Keystore
    certs/ca.crt
    build_android.ps1
    package_apk.ps1
  client/                 # 现有 Qt 5.15 桌面，不动 UI
```

- `clientcore` 先按“源码级共享”落地：移动端用 CMake 编译，桌面端后续通过 `.pri` 逐步接入；不强行把桌面 qmake 改成 CMake。
- 桌面端现有 `NetworkThread` 接口保持不变；迁移到 `clientcore::transport` 时只换实现。
- 移动端 service 使用 `clientcore` + Qt Core/Sql/Network；UI 进程只依赖 Qt Quick + 本地 IPC 客户端，不链接网络/存储核心。

#### 传输

- Android 用 `QSslSocket` 实现 `ITransport`，放在带事件循环的工作线程；由 Qt 负责 TLS 握手和证书链。
- 桌面继续用原生 Winsock + OpenSSL；满足现有“客户端原生 socket”约束。
- 两端共用 14 字节帧头、TLV body、心跳和重连策略。
- Android 的 CA 从 APK assets 读取，加载为 `QSslCertificate`；校验证书链和主机名/IP SAN。当前证书 SAN 是 `IP:121.5.168.189`，`QSslSocket::setPeerVerifyName` 用连接 host。
- Qt 6 Android 的 TLS 后端需要随包提供 OpenSSL 3（Qt 官方 `android_openssl` 方式）；在 F0 阶段验证 arm64 APK 能完成握手，不能假设系统自带。

#### 存储

- service 使用 Qt SQL `QSQLITE`，数据库放 app 私有目录；WAL 模式。
- 核心表：
  - `accounts`：user_id、username、nickname、avatar_version。
  - `conversations`：kind、target_id、title、last_message_id、last_content、last_time、unread。
  - `messages`：kind、target_id、message_id、client_msg_id、sender_id、nickname、content、created_at、reply 字段、recalled、msg_type、file_id。
  - `outbox`：client_msg_id、kind、target_id、content、created_at、state、retry_count。
  - `meta`：schema_version、last_sync_at、当前账号。
- 只落服务端已确认的消息；pending/outbox 单独存；撤回时清正文并置 recalled。
- 本地历史是服务端副本；重装/换机后从服务端恢复，不依赖本机备份。
- 默认 `android:allowBackup="false"`，避免 token 和聊天记录被系统云备份带走；卸载即清空。

#### 安全存储

- token 用 Android Keystore 的 AES-GCM 密钥加密后存 app 私有文件；密钥不出 Keystore。
- `device_id` 同样存安全存储；卸载重装会生成新设备 ID（旧会话可在会话列表里注销）。
- 桌面继续用现有 DPAPI 方案，不改。

#### 构建

- Qt 6.8 LTS（锁定具体 6.8.x 补丁）、JDK 17、NDK r26、Android SDK 35、build-tools 35。
- `minSdk 26`（Android 8.0），`targetSdk 35`；release 只出 `arm64-v8a`，debug 加 `x86_64` 供模拟器。
- `build_android.ps1` 负责：检查 Qt/JDK/NDK/SDK 环境 → `qt-cmake` 配置 → `cmake --build` → `androiddeployqt` 生成 APK → `apksigner` 签名 → 输出 SHA-256。
- 签名 keystore 放仓库外；release 构建从环境变量或本地配置文件读取密码，不提交仓库。
- 构建脚本必须能在干净机器上按 README 复现；Gradle 依赖版本锁定，避免每次构建漂移。

#### F3 UI 边界

F3 的 QML 只做能证明底座跑通的四块：

- **登录页**：用户名/密码、自动登录、错误提示；不显示服务器地址编辑。
- **会话列表**：私聊 + 群聊，显示标题、最后一条消息、时间、未读角标和连接状态；群聊可点进去看历史，但本次不做群管理。
- **1:1 文本聊天页**：打开会话、加载最近 50 条、发送文本、接收推送、失败重发、进入会话即标记已读。
- **设置占位页**：通知权限入口、电池优化入口、连接状态、退出登录；不做主题、本地存储管理、账号切换。

不在 F3：群创建/审批/成员管理、文件/图片、消息搜索、资料编辑、@提及、引用/撤回 UI、应用内更新。协议可以先支持这些，UI 在后续 M2 做。

#### F0 工具链验证（先于功能开发）

在写业务代码前，用一个最小 APK 验证：

1. Qt 6.8 Android arm64 能编译、安装、启动 QML。
2. `QSslSocket` 能用自带 CA 连上 `121.5.168.189:9443` 并完成登录帧收发。
3. `QtService` 独立进程能启动、保持前台服务、接收 C++ 消息并弹通知。
4. UI 进程和 service 进程能通过 `QLocalSocket` 双向通信。
5. 锁屏、划掉任务、重启手机后的行为符合预期；记录每台测试机的 ROM 表现。
6. 如果第 3/4 步在 Qt 6.8 上不可行，立即停下来改架构（备选：同进程前台服务 + `setQuitOnLastWindowClosed(false)`，或 Kotlin 只做通知桥），不要把风险带进 F1。

### 5.6 协议扩展

#### 能力位

登录/恢复 body 追加设备信息；响应追加服务端能力位。旧客户端忽略尾部字段。

```text
LOGIN request:
  string username
  string password
  u8     has_device_info      // 0/1；旧客户端没有这个字节
  [if 1]
    string device_id
    string device_name
    string platform
    string app_version
    u32    caps

LOGIN response:
  u64    user_id
  string nickname
  string token
  u32    server_caps
  u32    session_ttl_seconds
```

`RESUME` 同理：token 后追加 `has_device_info` 和设备信息；响应追加 `server_caps`、`session_ttl_seconds`。

能力位：

| 位 | 名称 | 含义 |
| --- | --- | --- |
| 0 | `kCapMultiDevice` | 支持多设备会话 |
| 1 | `kCapSyncPull` | 支持按会话游标同步 |
| 2 | `kCapReadStatePush` | 支持已读同步推送 |
| 3 | `kCapMessageEcho` | 支持私聊消息回显 |
| 4 | `kCapSessionManage` | 支持会话列表/注销 |
| 5 | `kCapDeviceMetadata` | 登录时上报设备信息 |

规则：

- 服务端只向具备对应能力位的 session 发送新推送；旧 session（caps=0）只收原有推送。
- 新客户端必须容忍未知命令/未知推送：记日志、忽略，不崩溃。
- 所有新命令都是追加值，不改现有帧头、不改现有命令编码。
- 协议版本号保持 1；能力位负责行为协商。

#### 兼容矩阵

| 客户端 | 服务端 | 行为 |
| --- | --- | --- |
| 旧 0.4.0 | 新 | 正常登录聊天；不再踢其他设备；收不到新推送；旧客户端之间仍单会话 |
| 新桌面 | 新 | 多设备、已读同步、私聊回显 |
| 新 Android | 新 | 多设备、同步、通知、会话管理 |
| 新 Android | 旧 | 不支持；服务端先升级，客户端版本检查提示 |

### 5.7 桌面端最小适配

本次桌面端只做“不回归 + 新推送可用 + 一个设备管理页”，不重做聊天 UI：

- 解析 LOGIN/RESUME 响应尾部能力位；服务端没有尾部字段时按 caps=0 处理。
- 处理 `kCmdReadStatePush`：更新私聊/群未读角标；如果当前会话匹配，清角标。
- 处理 `kCmdMessageEchoPush`：当前会话匹配时追加消息；否则更新会话摘要；自己的消息不计未读。
- 设置页新增“设备管理”：调用 `kCmdSessionList` 展示设备名、平台、最后活跃时间和“当前设备”标记；可注销其他设备，注销前二次确认；不显示 token。这是验收场景 6 的用户入口。
- 不再假设“登录会把别的连接踢掉”；连接被关闭时按现有断线逻辑处理。
- 保持现有 204 个 QtTest 全绿；新增上述协议解析/应用测试。

### 5.8 发布与更新

- 第一版只发官网 APK；应用内更新在手机 UI 第一版之后接，本次只准备：
  - `versionCode` 单调递增；`versionName` 语义化版本。
  - release APK 用固定 keystore 签名，`apksigner verify` 通过。
  - 更新清单 `https://<host>/updates/android/latest.json`，字段：`versionCode`、`versionName`、`apkUrl`、`sha256`、`size`、`notes`、`minSdk`。
  - 清单用 Ed25519 签名，公钥内置 APK；防止 CA/传输被攻破后推恶意包。
- 安装更新走 `FileProvider` + `ACTION_VIEW`；Android 8+ 需要用户允许“安装未知应用”。
- 更新失败保留旧版本；不自动清除数据。

## 6. 数据模型与迁移

### 6.1 sessions 迁移

用现有迁移脚本风格（按 `information_schema` 判断列是否存在，可重复执行）：

- 增加 `session_id`、`device_id`、`device_name`、`platform`、`app_version`、`caps`、`last_seen_at`。
- 为存量行回填 `session_id = UUID()`，`device_id = ''`，`last_seen_at = created_at`。
- 增加唯一键 `uk_session_id`，普通索引 `idx_user_device (user_id, device_id)`、`idx_user_seen (user_id, last_seen_at)`。
- `token_hash` 仍是主键；不改现有 token 校验路径。

### 6.2 其他表

- `private_read_state`、`room_read_state` 已经是 user 级，不改。
- 消息表不改；新增查询走现有索引：
  - `messages (room_id, id)` 已存在。
  - `private_messages (sender_id, receiver_id, id)`、`(receiver_id, sender_id, id)` 已存在。
- 不新增全量事件表；控制状态靠重拉列表校正。

### 6.3 清理

- 每次服务启动和每天一次清理：
  - 删除 `expires_at < NOW() - 30 days` 的 sessions。
  - 删除 `revoked != 0 AND last_seen_at < NOW() - 30 days` 的 sessions。
- 清理不删除用户、消息、好友关系。

### 6.4 迁移与回滚

- 上线顺序：备份数据库 → 执行迁移 → 部署新服务端 → 观察 → 发新客户端。
- 旧服务端二进制兼容新增列（SELECT 显式列，不受影响）；回滚只需停新服务、恢复旧二进制，不一定要回滚 schema。
- 若要完全回滚 schema，用备份恢复；迁移脚本本身不做 DROP。
- 迁移在低峰执行；sessions 表很小，锁表时间可忽略。

## 7. 安全与隐私

- **TLS**：继续用现有 CA 和 IP SAN；Android 内置 `ca.crt`，校验证书链和 host；不接受用户关闭校验。
- **token**：Android Keystore 加密；日志、崩溃信息、IPC 都不打印 token。
- **密码**：只在登录请求中出现一次；service 不落盘；UI 不持久化。
- **设备注销**：会话列表可看到设备名、平台、最后活跃时间；注销立即断连并撤销 token。
- **上限**：5 个设备，防止 token 无限累积；超限淘汰最旧设备并在服务端日志记录。
- **限流**：现有登录失败限流、注册限流、发消息令牌桶继续生效；多设备不绕过，按连接和 IP 计数，后续可加按 user 聚合。
- **APK**：release 固定签名；更新清单 Ed25519 签名；`apkUrl` 必须 HTTPS。
- **本地数据**：app 私有目录；`allowBackup=false`；卸载清空；不把聊天记录写到外部存储。
- **日志**：服务端记录 session_id/device_id 的哈希或截断值，不记录消息正文；客户端日志同样不记录正文和 token。
- **开放注册**：本次不解决内容审核/举报/封禁。公开推广前需要补隐私政策、应用备案和内容治理；底座阶段先沿用每 IP 限流并在文档中标注风险。
- **通知隐私**：默认显示内容预览；设置可切换为“仅提示有新消息”。

## 8. 观测与运维

服务端新增指标（沿用现有 `stats` 行）：

- `sessions_active`：当前有效会话数。
- `sessions_multi_user`：拥有 2 个以上会话的账号数。
- `session_revoked_total`：按 reason 计数。
- `sync_pull_total` / `sync_messages_total`：同步请求与补发消息数。
- `sync_lag_max`：单次同步最大缺口（条数）。
- `push_fanout_total`：多设备 fan-out 的帧数。
- `presence_transitions`：上线/离线转换次数。

日志：

- 登录成功：user_id、session_id、device_id、platform、ip。
- 会话注销：操作者 session、目标 session、reason。
- 同步失败/超时：user_id、会话数、错误码。
- 不记录消息正文、token、密码。

运维：

- 迁移、部署、日志查看全部走 SSH（沿用项目规则）。
- 服务端继续 systemd + logrotate；新增迁移脚本和回滚步骤写入 `chatbox/deploy/README`。
- 备份：上线前手工备份 `chatbox` 库；每日备份沿用现有策略。
- 服务器休眠/断网时手机通知不可用，这是部署约束，不是客户端 bug；文档写明。

## 9. 测试与验收

### 9.1 服务端

- `SessionManager` 单元测试：多绑定、首连上线、末连离线、同设备替换、指定 session 移除、presence_seq 只在地量变化时递增。
- DB 集成测试：会话插入/查询/注销/超限淘汰/滑动续期；`listRoomHistoryAfter`、`listPrivateHistoryAfter` 的 ASC 分页和 `has_more_messages`。
- 路由测试：私聊推送到达接收方全部 session；发送方其他 session 收到 echo；群推送到达群内全部连接；好友/群/资料/文件通知到达目标 user 全部 session。
- 兼容测试：旧协议 LOGIN/RESUME（无设备字段）仍成功；旧客户端登录不影响新客户端会话。
- 同步测试：多会话游标、重复消息去重、500 条上限、512 KiB 响应上限。
- 现有 CTest 5/5 保持通过。

### 9.2 桌面客户端

- 保持现有 204 个 QtTest 全绿。
- 新增：能力位解析、ReadStatePush 应用、MessageEchoPush 应用、旧响应无尾部字段的兼容、登录不再假设踢人。
- 手工：旧 0.4.0 与新服务端联调；新桌面与手机同时在线。

### 9.3 Android

- F0 工具链检查清单全部通过。
- 安装/启动/登录/1:1 文本收发/断线重连/杀进程/重启手机/锁屏通知。
- 通知权限拒绝、电池优化、后台限制三种场景。
- 本地 SQLite 迁移、outbox 重发、token 加密存储。
- UI 与 service 断开/重连（service 重启、UI 重启）。
- 至少覆盖 Android 8、10、13、14、15 各一台/模拟器；国产 ROM 至少一台（小米/华为/OPPO/vivo 中可得者）。

### 9.4 验收场景

1. 电脑和手机同账号同时登录，双方都不掉线；好友看到的是“在线”，不是两次上线/离线。
2. 手机给好友发私聊，好友的电脑和手机都实时收到；发送者电脑实时看到自己手机发的消息。
3. 电脑发消息，手机锁屏时收到通知；点通知打开对应会话。
4. 手机读了消息，电脑上的未读角标清零；反之亦然。
5. 手机关网 30 分钟后重连，补齐期间消息，无重复、顺序正确、未读数正确。
6. 电脑上注销手机会话，手机立即退出登录并提示“会话已被注销”。
7. 服务端重启，电脑和手机都自动重连，在线状态恢复正确。
8. 旧 0.4.0 桌面客户端在新服务端上登录、发消息、收消息正常，且不踢手机。
9. 手机划掉任务后，service 仍在（或按 ROM 限制尽力保持）；新消息仍能通知。
10. 手机重启后，已登录账号能自动恢复连接（在系统允许的前提下）。

### 9.5 性能边界

- 单账号 5 个会话；全局 500 连接（现有上限）。
- 单次同步响应 ≤ 512 KiB；单会话单次 ≤ 50 条；单会话补齐上限 500 条/轮。
- 多设备 fan-out 不改变 IO 线程模型；推送仍在 IO 线程 post，不做阻塞 DB 操作。
- 手机端冷启动到可登录界面 ≤ 3 秒（中端机）；service 常驻内存目标 ≤ 100 MB（Qt 应用实际可能更高，F0 记录基线）。

## 10. 风险与缓解

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| Qt 6 Android 工具链/服务进程不可用 | 底座无法按设计落地 | F0 先验证；失败则改同进程前台服务或 Kotlin 通知桥，F1 前决策 |
| `QSslSocket` 在 Android 上缺 OpenSSL | 无法 TLS | 随包 OpenSSL 3；F0 用真实服务端握手验证 |
| 国产 ROM 杀后台 | 收不到通知 | 前台服务 + START_STICKY + 开机自启 + 电池白名单引导；文档明确不保证 |
| Android 15 限制 BOOT_COMPLETED 启前台服务 | 开机不自启 | 捕获异常；首次打开 app 启动；设置页提示 |
| 服务端在宿舍笔记本，休眠/断网 | 手机通知中断 | 文档写明；不承诺 24/7；客户端显示连接横幅 |
| 旧桌面兼容回归 | 现有用户不可用 | 能力位 + 旧协议测试 + 先部署服务端再发客户端 |
| 协议两端漂移 | 消息丢失/解析错 | `common/` 单一编解码 + 兼容矩阵 + 双端测试 |
| 同步缺口/重复 | 消息错乱 | 游标 + message_id upsert + client_msg_id 幂等 + 最近页校正 |
| 开放注册被滥用 | 垃圾账号/流量 | 现有每 IP 限流；后续验证码/邀请；本次不扩 |
| 签名 key 丢失 | 无法更新 APK | keystore 离线备份；文档记录指纹；CI secret |
| 范围膨胀 | 底座迟迟不能验收 | F0-F4 分阶段；完整 UI 另立计划 |

## 11. 实施阶段

| 阶段 | 内容 | 退出条件 |
| --- | --- | --- |
| F0 工具链 spike | Qt 6.8 Android、QSslSocket、QtService、QLocalSocket、通知、锁屏/划掉/重启 | 6 项检查全过；记录测试机和 ROM |
| F1 服务端多设备 | sessions 迁移、SessionManager/RoomManager、登录/恢复、会话列表/注销、presence 聚合 | 多设备同时在线；旧客户端兼容；CTest 全绿 |
| F2 同步与桌面适配 | sync pull、after 查询、已读推送、私聊 echo、桌面最小适配 | 验收场景 1-8 通过；204 QtTest 全绿 |
| F3 Android 底座 | clientcore、service、IPC、SQLite/outbox、登录/会话列表/1:1 文本收发、通知、Keystore | 真机能登录、1:1 文本收发、锁屏通知、杀进程恢复 |
| F4 加固与验收 | 10 个验收场景、迁移/回滚、日志指标、构建脚本、文档 | 验收清单签字；APK 可复现构建 |
| 后续 M2 | 完整手机 UI 对齐桌面 | 另立设计/计划 |

工作量粗估：F0 2-3 天，F1 3-5 天，F2 3-5 天，F3 5-8 天，F4 2-3 天；真机兼容测试是主要不确定项。

实施计划拆成两份：

- 服务端多设备/同步/桌面适配：`docs/superpowers/plans/2026-10-29-chatbox-multidevice-server.md`
- Android 底座：`docs/superpowers/plans/2026-10-29-chatbox-android-foundation.md`

## 12. 后续 app 功能映射（确保底座够用）

| 桌面功能 | 手机落点 | 底座依赖 |
| --- | --- | --- |
| 登录/注册/自动登录 | 登录页 | token 安全存储、RESUME、会话管理 |
| 会话/群组列表 | 首页会话列表 | 私聊/群列表、未读、最后消息 |
| 群聊/私聊 | 聊天页 | 消息同步、发送 outbox、引用/撤回 |
| 历史分页 | 上滑加载 | 现有 history + after 查询 |
| 搜索 | 会话内搜索 | 现有 message search |
| @提及 | 输入面板 | 群成员列表 |
| 好友/申请/备注 | 联系人页 | 现有好友协议、多设备通知 |
| 资料/头像 | 我的/资料页 | 现有 profile/avatar |
| 文件 | 聊天附件/文件页 | 现有文件协议 + Android SAF |
| 未读/在线状态 | 角标/在线点 | 多设备聚合、read state |
| 明暗主题 | 跟随系统 | QML 主题 |
| 本地历史/离线只读 | 本地 SQLite | service store、连接状态 |
| 设置/通知/自启动 | 设置页 | FGS、通知权限、电池引导 |
| 应用内更新 | 设置页 | APK 签名、更新清单（后续） |

## 13. 设计者决定（可推翻）

以下是设计者按“企业级但不过度”做的默认选择；用户没有逐条确认，后续可以改：

1. 每账号最多 5 个**新客户端**有效会话，超限淘汰最旧；旧客户端会话不占这个上限；token 30 天滑动续期。
2. 会话 ID 用服务端 UUID；设备 ID 用客户端随机 UUID；旧会话 `device_id=''`，RESUME 带设备信息时就地升级。
3. Android 采用 service/UI 双进程；service 用 `QtService`，IPC 用 `QLocalSocket + 长度前缀 JSON`；服务端只看到 service 这一条连接。
4. 不做全量事件日志；消息按会话游标同步，控制状态重连重拉，撤回/删除只保证最近一页的离线校正。
5. 私聊发 echo，群聊靠群广播；不额外做群 echo。
6. Qt 6.8 LTS、minSdk 26、targetSdk 35、release 只出 arm64-v8a。
7. 包名 `com.chatbox.mobile`，应用名 `ChatBox`；签名 key 后续生成并离线备份。
8. 通知默认显示内容预览；常驻通知单独低优先级。
9. 登出只清 token，不自动清本地聊天记录；“清空本机记录”是单独操作。
10. 本次只准备 APK 签名和更新清单，不做应用内更新 UI。
11. 不接厂商推送；服务端只留 `PushGateway` no-op 接口。
12. F3 的手机 UI 只做登录、会话列表、1:1 文本聊天和设置占位；群管理/文件/搜索/资料页放 M2。
13. 桌面端加一个“设备管理”设置页，作为注销手机的用户入口；不重做聊天 UI。
14. `kCmdSyncPull` 支持 `start_index`/`next_index` 续传，服务端按 480 KiB 预算分批返回。
15. `last_seen_at` 由后台任务每 10 分钟批量刷新并滑动过期时间，不在 PING 回调里写库。
16. 1002/1010/1011 为终止错误；service 清 token 回登录页，不用同一个 token 循环重连。

## 14. 需要用户提供/确认

- release keystore 的存放位置和密码管理方式（不阻塞 F0-F3，阻塞首次公开发布）。
- 是否接受包名 `com.chatbox.mobile` 和默认应用名 `ChatBox`。
- 公开推广前是否要做应用备案/隐私政策/内容治理；本次底座不包含。
