# ChatBox 多群、群文件与右侧栏设计

日期：2026-10-28

状态：待确认（本文档不包含实现代码）

## 1. 目标

- 支持多群：创建、切换、邀请、按群号搜索申请、审批、解散、群主/管理员两级权限。
- 保留一个默认群，所有新注册用户自动加入；默认群不可解散、不可删除。
- 支持群聊和私聊发送任意类型文件，单文件上限 2GB，上传可中断续传。
- 文件作为特殊消息进入聊天记录；发送方卡片立即出现并显示进度，接收方在文件提交后看到卡片。
- 图片、视频、GIF 显示缩略图/封面并可在客户端预览；视频边下边播；其他文件显示文件卡片。
- 接收方点开/下载时才把文件保存到本地，保存位置在设置中可配置；已下载文件保留在本地。
- 右侧栏改为单列：群聊打开先显示成员，私聊打开先显示资料；消息搜索和文件管理都放在这一栏；文件管理是二级页。
- 删除“加载更早消息”菜单，改为上滑到顶自动加载更早消息。
- 群解散时删除服务器上的群消息和文件；成员本地聊天记录和已下载文件保留，群显示为“已解散”只读。

## 2. 非目标

- 不做端到端加密、病毒扫描、文件版本管理、跨群转发、离线发送。
- 不做自定义群头像/群公告/群文件回收站；群头像使用群名首字。
- 不做视频转码；只播放 Windows/Qt 当前后端能播放的编码，失败时提示并允许下载后用系统播放器打开。
- 不把文件写入 MySQL BLOB；文件落在服务器磁盘，MySQL 只存元数据。
- 不保证服务器磁盘故障后的文件恢复；现有备份只覆盖 MySQL（见 8.7）。
- 不支持移动端。
- 旧的 0.2.5 客户端不新增群/文件功能，但必须能继续在默认群发文字消息，并能看到文件消息的文字回退。

## 3. 产品行为

### 3.1 群模型

- 用户可以创建多个群；创建者成为群主。
- 群号：8 位数字，全局唯一，服务端生成；按群号精确搜索。
- 群名：1–32 个字符，允许重名。
- 成员上限：500 人/群。
- 每用户最多同时加入 20 个群（含自己创建的）；创建群限制 5 个/天。
- 角色：
  - 群主：改名、设/撤管理员、审批申请、移除成员、转让群主、解散群。
  - 管理员：改名、审批申请、移除普通成员。
  - 成员：邀请、发消息、发文件、退群。
- 邀请：成员搜索用户并发出邀请；对方接受后入群。邀请 20 次/天。
- 搜索加入：输入群号，看到群名/成员数/状态，填写申请说明；群主或管理员审批。申请 20 次/天。
- 退群：普通成员/管理员可退；群主必须先转让或解散。默认群不可退。
- 解散：仅群主；默认群不可解散。解散需二次确认。
- 群主转让：群主选择成员，原群主变为普通成员。
- 群列表：
  - 左栏顶部“会话 / 群组”两个 Tab。
  - “会话”列出私聊会话。
  - “群组”列出已加入的群；默认群置顶；显示成员数、未读数、最后一条消息摘要。
  - “群通知”入口显示待处理邀请/申请数量。
  - 已退出/已解散但本地有记录的群放在“已退出/已解散”折叠区，只读。
- 新用户注册时在同一个数据库事务里加入默认群。
- 默认群：
  - 使用现有 room 1，名称“群聊A”，固定群号。
  - `is_default=1`，不可解散、不可删除、不可退群。
  - 群主由服务端配置指定（迁移时默认取最早注册用户）；群主可设管理员。
  - 所有新用户自动加入，不经过审批。
  - 默认群成员上限不设限（`max_members=0`），保证任何新用户都能进入；普通群上限 500。
- 历史与文件可见范围：
  - 群聊历史从成员加入时刻开始；`room_members.join_message_id` 记录加入时的最大消息 ID，历史/搜索只返回该 ID 之后的消息。
  - 群文件对当前成员开放全部：新成员可以在文件管理里看到并下载加入之前上传的文件，但这些文件对应的聊天消息不会出现在历史里。
  - 退群/被移除后立即失去服务器历史、搜索和文件访问；本地已有记录和已下载文件保留。
  - 重新加入后历史从新的加入时刻开始，不恢复旧服务器历史；本地旧记录仍在。
- 未读：
  - 服务端权威。新增 `room_read_state(room_id,user_id,last_read_message_id)`。
  - `GROUP_LIST` 返回每个群的未读数（加入游标之后、last_read 之后的消息数）。
  - 新增 `GROUP_MARK_READ`；打开群、窗口聚焦且滚动到底部时上报。
  - 离线期间的消息在下次登录时计入未读；私聊沿用现有 `private_read_state`。
- 群数量限制只统计 active 群；已解散墓碑、待审批申请不占 20 群额度。

### 3.2 文件消息

- 任意文件类型；单文件 ≤ 2GB；每群文件总量 ≤ 20GB；服务器剩余磁盘 < 5GB 时拒绝新上传。
- 发送入口：输入框旁回形针、拖拽文件到聊天区、粘贴剪贴板图片。
- 发送方点击发送后：
  - 聊天记录里立即出现本地文件卡片，显示文件名、大小、进度、暂停/继续/取消。
  - 上传在后台分块进行，用户可以继续聊天、切换会话；退出客户端后任务保留，下次启动继续。
  - 上传完成并校验 SHA-256 后，服务端创建消息并广播；接收方此时看到文件卡片。
- 接收方点击文件卡片：
  - 图片：打开图片预览（适应窗口、缩放）。
  - GIF：聊天里显示静态缩略图，点击打开动图预览。
  - 视频：客户端内播放器边下边播，支持暂停、拖动进度。
  - 其他：下载到“文件保存位置”，完成后用系统默认程序打开；也可“另存为”。
  - 下载支持断点续传；已下载文件保留在本地。
- 文件卡片状态：
  - 发送方：上传中 / 已暂停 / 上传失败 / 已发送 / 已删除。
  - 接收方：未下载 / 下载中 / 已暂停 / 已下载 / 下载失败 / 已删除。
- 进度显示：卡片上显示百分比和已传/总大小；不做独立悬浮进度窗。
- 删除：
  - 群文件：上传者、群主、管理员可删除服务器文件；删除后服务器文件立即删除，所有客户端该消息变为“文件已删除”，本地已下载副本保留。
  - 私聊文件：仅上传者可删除服务器文件；双方都可以删除自己的本地副本。
  - 撤回文件消息等同删除服务器文件，并显示“消息已撤回”。
- 文件管理：
  - 右侧栏文件入口显示“文件 N 项 · 已用 X / 20GB”。
  - 二级页列出当前群/私聊的全部文件：文件名、大小、上传者、时间、状态、操作。
  - 操作：打开/下载、打开所在文件夹、删除服务器文件（有权限时）、删除本地副本。
  - 顶部有上传按钮和按文件名筛选的输入框。
  - 空状态提示“还没有文件”。
- 本地保存：
  - 设置新增“文件保存位置”，默认 `%USERPROFILE%\Downloads\ChatBox`。
  - 新下载使用新路径；已下载文件不移动；下载中的任务在原路径完成。
  - 路径不能位于客户端安装目录内；校验方式与本地聊天记录目录一致。
  - 文件名冲突时追加 ` (1)`、` (2)`；文件名做 Windows 非法字符/保留名清理，长度截断到 180 字符。
  - 未完成下载写 `<name>.part`；完成后校验 SHA-256 再改名。
  - 预览缩略图/视频流缓存放在 `%LOCALAPPDATA%\ChatBox\preview`，不占用用户保存目录；视频缓存 LRU 上限 5GB。

### 3.3 媒体预览

- 图片：聊天内缩略图最大 240×180，保持比例；点击打开预览窗。
- GIF：聊天内显示首帧缩略图；点击打开动图预览。
- 视频：聊天内显示服务端生成的封面 + 播放标记；点击打开客户端播放器。
- 视频播放器：播放/暂停、进度条、音量、全屏；关闭时释放缓存句柄。
- 不支持的编码：提示“当前系统无法播放该视频编码”，提供“下载后用系统播放器打开”。
- 缩略图生成：
  - 服务端在文件提交后用 ffmpeg 生成 JPEG 缩略图（图片/GIF 取首帧，视频取 1 秒或 10% 处一帧），并探测宽高/时长。
  - ffmpeg 失败时不阻塞文件消息，显示通用图标/封面。
  - 发送方在本地可先用原文件生成预览，缩略图到达后替换。

### 3.4 右侧栏（方案 B）

- 点击聊天头部 `⋯` 直接切换右侧栏，不再弹出菜单。
- 群聊：
  - 顶部：群名、成员数、群设置齿轮（群主/管理员可见）。
  - 搜索框固定在顶部。
  - 成员列表占满剩余高度，独立滚动；角色徽标；右键/双击行为沿用现有资料/私聊。
  - 底部固定文件入口：“文件 N 项 · 已用 X / 20GB ›”。
  - 文件入口打开二级文件管理页，顶部有返回箭头；返回成员列表。
  - 搜索框进入搜索模式：输入关键词，结果列表替换成员列表；点击结果跳到对应消息并退出搜索。
- 私聊：
  - 顶部：对方头像、昵称、在线状态（沿用现有能力）。
  - 资料卡：ID、签名、发消息/查看资料（打开现有 ProfileDialog）。
  - 搜索框固定在底部。
  - 文件入口在资料卡下方。
  - 文件管理二级页同上。
- 右侧栏宽度默认 260px，可拖动，保存到 QSettings；窗口过窄时自动收起。
- 原菜单项“消息搜索”“加载更早消息”“显示成员列表”全部移除。
- 上滑加载：
  - 聊天滚动条距顶部 ≤ 40px 时触发加载更早消息（每页 50 条）。
  - `has_more_`、`loading_older_` 防重复；请求带上当前会话和 `next_before_id_`。
  - 插入旧消息后按“新增高度差”调整滚动条，保持用户当前看到的消息不跳。
  - 无更多消息时不显示加载提示。
  - 离线模式从本地 SQLite 分页加载。
- 搜索跳转：
  - 点击搜索结果时，以 `message_id + 1` 作为 `before_id` 拉取一页（包含目标消息），渲染后滚动到该消息并短暂高亮。
  - 如果目标已被删除，提示“消息不存在”。

### 3.5 群设置与通知

- 群设置对话框（群主/管理员）：
  - 群资料：群名、群号（只读）、群主、创建时间、成员数。
  - 成员管理：列表、角色、移除、设为/取消管理员（仅群主）。
  - 申请/邀请：待审批申请、待接受邀请、历史记录。
  - 危险操作：转让群主、解散群（仅群主）。
- 创建群对话框：群名，创建后显示群号并自动进入。
- 加群对话框：输入群号，展示结果，填写申请说明。
- 邀请对话框：复用用户搜索，发送邀请。
- 群通知页：待处理申请/邀请，支持同意/拒绝；处理结果推送给对方。
- 群主解散后：
  - 在线成员收到 `GROUP_DISBANDED` 推送，群进入“已解散”只读状态。
  - 离线成员下次登录时从群列表拿到 `status=dissolved`，本地历史保留。
  - 服务器删除该群消息和文件；保留群行/成员关系作为墓碑，便于展示群名和“已解散”。
  - 不允许再发消息、发文件、邀请、申请。

## 4. 数据模型

### 4.1 MySQL

现有 `rooms` 表扩展为群：

```text
rooms:
  id, name, max_members, created_at,
  group_no VARCHAR(12) UNIQUE,
  owner_id BIGINT UNSIGNED,
  is_default TINYINT DEFAULT 0,
  status TINYINT DEFAULT 0,          -- 0 active, 1 dissolved
  dissolved_at DATETIME(3) NULL
```

新增：

```text
room_members:
  room_id, user_id, role TINYINT,     -- 0 member, 1 admin, 2 owner
  joined_at, join_message_id BIGINT UNSIGNED DEFAULT 0,
  PRIMARY KEY(room_id,user_id)

room_read_state:
  room_id, user_id, last_read_message_id BIGINT UNSIGNED,
  updated_at, PRIMARY KEY(room_id,user_id)

room_join_requests:
  id, room_id, user_id, message VARCHAR(200),
  status TINYINT,                     -- 0 pending, 1 approved, 2 rejected
  created_at, reviewed_by, reviewed_at

room_invites:
  id, room_id, inviter_id, invitee_id,
  status TINYINT,                     -- 0 pending, 1 accepted, 2 declined, 3 revoked
  created_at, responded_at

files:
  id, uploader_id, room_id NULL,
  private_sender_id NULL, private_receiver_id NULL,
  name VARCHAR(255), size BIGINT, mime VARCHAR(128),
  sha256 CHAR(64), storage_path VARCHAR(512),
  thumb_path VARCHAR(512) NULL,
  width INT NULL, height INT NULL, duration_ms INT NULL,
  status TINYINT,                     -- 0 uploading, 1 ready, 2 deleted, 3 failed
  created_at, deleted_at

file_uploads:
  id, uploader_id, room_id NULL,
  private_sender_id NULL, private_receiver_id NULL,
  name, size, mime, sha256,
  temp_path VARCHAR(512), offset BIGINT,
  status TINYINT,                     -- 0 active, 1 committed, 2 canceled, 3 expired
  created_at, updated_at
```

消息表：

- `messages` 已有 `msg_type`，增加 `file_id BIGINT NULL`。
- `private_messages` 增加 `msg_type TINYINT DEFAULT 0`、`file_id BIGINT NULL`。
- 文件消息的 `content` 存文字回退：`[文件] name` / `[图片] name` / `[视频] name`，保证旧客户端可读、搜索可命中。
- 文件消息的 `msg_type`：0 文本、1 文件、2 图片、3 视频、4 GIF。
- 不在 `reply_content` 中存文件内容；引用文件消息时显示回退文字。

迁移：

- 只保留 room 1；删除/停用其余 11 个预置房间（当前生产库为空，仍写迁移脚本以防其他环境有数据）。
- room 1 设置 `group_no`、`owner_id`、`is_default=1`、`status=0`。
- 为所有现有用户插入 room_members（role=0；配置的默认群主 role=2）。
- 新注册事务：创建用户 + 加入 room 1。
- 群号生成：8 位数字，从 10000001 起顺序分配，冲突重试；默认群固定 10000001。
- 所有新表/列必须有可重复执行的迁移脚本和回滚说明。

### 4.2 客户端 SQLite

`history.sqlite` 版本从 1 升到 2（需要真正的 `ALTER TABLE` 迁移）：

- `messages` 增加：`msg_type`、`file_id`、`file_name`、`file_size`、`mime`、`thumb_path`、`local_path`、`transfer_state`、`transfer_progress`。
- 新增 `pending_uploads`：`upload_id`、`local_message_id`、`source_path`、`offset`、`size`、`sha256`、`room_id/private_target`、`status`、`updated_at`。
- 新增 `files`：`file_id`、`message_id`、`local_path`、`sha256`、`state`、`thumb_path`、`stream_cache_path`。
- 新增 `groups` 缓存：`room_id`、`name`、`group_no`、`role`、`member_count`、`status`、`last_message`、`unread`、`is_default`。
- 会话/消息表已有 `target_id`，群聊继续使用 room_id；私聊保持现有结构。
- 本地数据库迁移失败时不得删除旧库；备份 `.bak` 后报错。

## 5. 协议

### 5.1 原则

- 保持 14 字节帧头、网络字节序、C++17、原生 Socket/TLS。
- 现有 `CHAT_SEND`/`PRIVATE_SEND`/`HISTORY`/`MESSAGE_SEARCH` 的字段顺序不变。
- 文件/群字段只在末尾追加；旧客户端读到自己需要的字段后忽略剩余字节。
- 新命令放在未使用区间 `0x00B0–0x00CF`；旧客户端收到未知命令直接忽略。
- 文件数据不走聊天主连接，避免 2GB 传输阻塞消息；文件连接单独建立，复用同一 TLS 和 CA。

### 5.2 群命令

```text
0x00B0 GROUP_LIST          -> 群列表（含角色、成员数、未读、status、is_default）
0x00B1 GROUP_CREATE        name -> room_id, group_no
0x00B2 GROUP_UPDATE        room_id, name
0x00B3 GROUP_DISBAND       room_id
0x00B4 GROUP_SEARCH        group_no -> 群信息 + 我的状态
0x00B5 GROUP_JOIN_REQUEST  group_no, message
0x00B6 GROUP_JOIN_RESPOND  request_id, accept
0x00B7 GROUP_INVITE        room_id, invitee_id
0x00B8 GROUP_INVITE_RESPOND invite_id, accept
0x00B9 GROUP_MEMBER_LIST   room_id -> 成员列表（角色、在线状态）
0x00BA GROUP_MEMBER_UPDATE room_id, user_id, action (remove/set_admin/unset_admin/transfer)
0x00BB GROUP_REQUEST_LIST  room_id -> 待处理申请/邀请
0x00BC GROUP_PUSH          created/updated/dissolved/member_joined/member_left/role_changed/request/invite
0x00BD GROUP_MARK_READ     room_id, last_read_message_id
```

### 5.3 文件命令（文件连接）

```text
0x00C0 FILE_AUTH           token -> user_id（只校验 session，不绑定/不踢主连接）
0x00C1 FILE_UPLOAD_INIT    kind, target_id, name, size, mime, sha256
                           -> upload_id, offset
0x00C2 FILE_UPLOAD_CHUNK   upload_id, offset, data -> ack_offset
0x00C3 FILE_UPLOAD_STATUS  upload_id -> offset, status
0x00C4 FILE_UPLOAD_COMMIT  upload_id -> message_id, file_id, created_at
0x00C5 FILE_UPLOAD_CANCEL  upload_id
0x00C6 FILE_OPEN           file_id, offset -> total_size, range_token
0x00C7 FILE_READ           file_id, offset, length -> 多个 FILE_CHUNK
0x00C8 FILE_CHUNK          file_id, offset, data
0x00C9 FILE_READ_ACK       file_id, bytes（下载信用窗口）
0x00CA FILE_DELETE         message_id
0x00CB FILE_INFO           file_id -> name,size,mime,sha256,thumb
0x00CC FILE_THUMB          file_id -> thumb bytes（或单独文件下载）
0x00CD FILE_LIST           kind, target_id, before_file_id, limit -> 文件列表 + 配额
```

- 文件连接是独立 TCP/TLS 连接，使用 `FILE_AUTH` 认证；不改变主连接的 SessionManager 绑定。
- 上传：每块 ≤ 60KiB，服务端按 offset 追加；每 1MiB 回一次 ack；客户端可流水线最多 8 块。
- 下载：客户端请求 offset/length；服务端连续发送 FILE_CHUNK，累计不超过 2MiB 或客户端信用；客户端每 1MiB 回 ACK。
- 断线：客户端重连后 `FILE_UPLOAD_STATUS`/`FILE_OPEN` 从已有 offset 继续；服务端以临时文件实际大小为准。
- 文件消息提交：`FILE_UPLOAD_COMMIT` 在一个数据库事务中写 `files`、`messages`/`private_messages`，然后广播普通消息推送（带文件元数据）。
- 旧客户端收到文件消息推送时读 `content` 回退文字，忽略末尾文件字段；不会崩溃。

### 5.4 消息字段追加与历史兼容

单条消息帧（`CHAT_PUSH`、`PRIVATE_PUSH`、发送响应）在现有字段末尾追加：

```text
u8  msg_type
u64 file_id
u16 file_name_len + bytes
u64 file_size
u16 mime_len + bytes
u8  thumb_available
u32 width
u32 height
u32 duration_ms
```

旧客户端只读自己需要的字段，忽略尾部，兼容。

**历史/搜索不能逐条追加**：`HISTORY_RESP`/`MESSAGE_SEARCH_RESP` 是 `count` 后跟定长记录，逐条尾部会被旧客户端当成下一条记录的开头。改为在**整页记录之后**追加一张元数据表：

```text
... count 条旧格式记录 ...
u16 meta_count
每条元数据（按 record_index 关联）：
  u16 record_index
  u8  msg_type
  u64 file_id
  u16 file_name_len + bytes
  u64 file_size
  u16 mime_len + bytes
  u8  thumb_available
  u32 width
  u32 height
  u32 duration_ms
```

- 0.2.5 客户端读完 `count` 条记录后不再读取尾部，天然忽略。
- 0.3.0 客户端在记录后检查剩余字节：有 `meta_count` 就按 index 合并；没有或为 0 则全部按文本处理。
- 文件消息在旧格式记录里的 `content` 是 `[文件] name` 回退，旧客户端不会显示空白。
- 群历史只返回 `join_message_id` 之后的消息；文件管理用 `FILE_LIST` 返回该群全部 ready 文件，不受加入时间限制。
- 搜索按加入游标过滤；点击搜索结果用 `before_id=message_id+1` 拉取目标页。

## 6. 服务端设计

### 6.1 网络与线程

- 保留 epoll ET IO 线程 + WorkerPool；聊天主连接行为不变。
- 文件连接：新增 `FILE_AUTH`，认证后把连接标记为 file connection；该连接的所有帧由同一个 worker 顺序处理。
- **每个用户最多一条文件连接**，在连接内按 `upload_id`/`file_id` 多路复用多个上传/下载上下文；客户端不建立四条文件连接。
- 文件 worker 不执行 MySQL 大查询；只有 init/commit/权限检查访问数据库，chunk 只读写磁盘。
- 文件连接维护 `QHash<transfer_id, FileTransferContext>`：upload_id/file_id、fd、offset、size、sha256 增量、权限、最近活动时间。
- 暂停的任务释放 fd、保留持久化 offset；10 分钟无活动关闭文件连接，恢复时重连。
- 主连接登出/被踢/被新登录接管时，服务端关闭该用户的文件连接并清理上下文。
- 连接数限制：`max_conn_per_ip` 至少预留主连接 + 文件连接两条；文件连接不计入“每用户一条会话”的绑定，但计入总连接数。
- 上传会话 7 天过期。
- 不把 2GB 读入内存；使用固定 64KiB 缓冲和 `read`/`write` 循环。
- 下载写缓冲受 2MiB 上限约束；使用 ACK 信用窗口，避免 IO 线程关闭连接。

### 6.2 存储

- 根目录：`/home/ubuntu/chatbox/files`，可通过配置覆盖。
- 临时：`files/tmp/<upload_id>.part`。
- 正式：`files/<file_id前两位>/<file_id>`（无扩展名，避免执行）；缩略图 `files/thumbs/<file_id>.jpg`。
- 权限：目录 0700，文件 0640，属主为服务用户；禁止 nginx 直接静态访问。
- 文件记录只存相对路径；所有访问通过 file_id 查表，绝不拼接客户端路径。
- 文件名只用于展示和本地保存；服务端存储不使用原文件名。

### 6.3 缩略图

- 安装 ffmpeg（apt），配置路径；服务端在 commit 后异步生成缩略图。
- 图片/GIF：取首帧；视频：取 1 秒或 10% 时长处一帧；输出 JPEG，最大 320×180。
- 同时用 ffprobe 获取宽高/时长；失败时字段为 0，缩略图标记不可用。
- 生成任务放入独立的小线程池，不占聊天 worker；同一文件只生成一次。
- ffmpeg 不可用时记录警告，文件仍可发送/下载。

### 6.4 权限

- 群文件：上传/下载/删除前检查 `room_members`；群已解散拒绝。
- 私聊文件：只允许 `private_sender_id`/`private_receiver_id`；不接受任意用户 ID。
- 删除：上传者、群主、管理员；私聊仅上传者。
- 群解散：仅群主；默认群拒绝。
- 文件连接每次 init/open 重新校验权限；不信任客户端缓存。
- 登录 token 校验使用 sessions 表/服务端现有 token 逻辑；文件连接不改变主连接绑定。

### 6.5 配额与清理

- 单文件 ≤ 2GB。
- 每群 ready 文件总大小 ≤ 20GB；上传 init 和 commit 都检查。
- 服务器剩余磁盘 < 5GB 时拒绝新上传（保留系统余量）。
- 私聊文件计入全局磁盘，不单独设群配额。
- 过期上传（7 天）和孤立临时文件由定时任务清理；删除文件时先删磁盘再标记 DB，失败记录日志并重试。
- 群解散：删除该群所有文件的磁盘文件和 `files` 行，删除 `messages` 行；保留 `rooms`/`room_members` 墓碑。
- 文件删除/撤回：删除磁盘文件，`files.status=deleted`，消息内容改为“文件已删除”，广播更新。

### 6.6 群成员与在线状态

- `room_members` 是权限权威；`RoomManager` 只负责在线 presence 和广播目标。
- 登录/重连后客户端拉取群列表并对所有群 JOIN；服务端加载持久成员关系。
- 入群（审批通过/接受邀请）时写入 `join_message_id = 当前该群最大消息 ID`；历史/搜索从该游标之后开始。
- 群文件列表/下载/Range 只检查当前成员关系，不检查加入时间；新成员可访问全部群文件。
- 退群/被移除：删除 `room_members` 行，立即拒绝历史/搜索/文件请求；本地已下载文件不受影响。
- 重新加入：重新写 `join_message_id`，旧服务器历史不恢复。
- 群成员列表 = 持久成员 + 在线状态；离线成员也显示。
- 群主/管理员操作后广播 `GROUP_PUSH`；被移除的成员收到推送并停止接收该群消息。
- 未读由 `room_read_state` 计算：`last_read_message_id` 之后的消息数；入群时初始化为 `join_message_id`。
- 默认群在服务启动时确保存在；room 1 不存在则报错，不自动删除。

### 6.7 备份与运维

- 现有 MySQL 备份保留；文件目录不进入现有备份。设计明确接受“磁盘损坏会丢文件”的风险。
- 新增磁盘使用监控/告警（低于 10% 或 5GB 记录 ERROR）。
- `files` 目录和 MySQL 元数据必须一起恢复；恢复流程写入部署文档。
- 新增 ffmpeg 依赖、`files` 目录创建和权限、systemd 配置更新。

## 7. 客户端设计

### 7.1 网络

- 新增 `FileTransferClient`：每用户一条独立 TLS 连接（复用现有 CA/服务器地址），`FILE_AUTH` 后在连接内多路复用上传/下载。
- 文件连接断线自动重连；上传/下载任务持久化在 SQLite，启动后恢复。
- 主连接继续负责聊天、群、历史、搜索；文件连接不阻塞主连接。
- 所有文件 ID/消息 ID 用 `quint64`；偏移/大小用 `qint64`，2GB 在范围内。

### 7.2 消息模型与渲染

- `ChatMessage` 增加 `MessageType` 和 `FileMeta`；文件消息在 `MessageStore` 中与文字消息同序。
- 本地待发送文件消息先插入 `MessageStore`（status=uploading），上传完成后用服务端 message_id 替换。
- `message_renderer` 增加：
  - 图片/GIF 缩略图 `<img>`（QTextDocument resource，最大 240×180）。
  - 视频封面 + 播放标记。
  - 文件卡片：类型图标、文件名、大小、状态/进度。
  - 上传/下载进度文本（百分比 + 已传/总大小），节流重绘。
- 每个消息仍保持一个顶层 `QTextTable`，不破坏 `ChatView` 的映射。
- 缩略图缓存：`FileThumbnailCache` 异步下载到 `%LOCALAPPDATA%\ChatBox\preview/thumbs`，加载完成后 `addResource` 并重绘当前会话。
- 点击文件消息由 `ChatView` 映射到 message_id，再交给 `FileController` 处理。
- 图片/GIF 预览窗：`QDialog` + `QLabel/QMovie`，支持缩放/适应窗口/保存。
- 视频播放器：`QDialog` + `QMediaPlayer` + `QVideoWidget`，播放本地代理 URL。
- 文件卡片右键：打开、另存为、打开所在文件夹、暂停/继续、取消、删除服务器文件（有权限）、删除本地副本。

### 7.3 上传/下载控制器

- `FileTransferManager`：
  - 维护队列；同一时间最多 2 个上传、2 个下载，其余排队。
  - 每个任务有状态机：queued/uploading/paused/verifying/downloading/failed/done。
  - 上传读取原文件，计算 SHA-256（可边传边算）；断点续传以服务端 offset 为准。
  - 下载写 `.part`，从已有文件大小恢复；完成后校验 SHA，改名。
  - 取消上传：通知服务端删除临时文件，删除本地待发送消息。
  - 取消下载：保留 `.part` 以便下次继续，或按用户选择删除。
- 任务持久化：`pending_uploads`/`files` 表；应用重启后自动恢复未完成任务。
- 上传源文件被移动/删除：任务标记失败，提示“原文件不存在，请重新选择”。
- 下载保存路径：设置中的“文件保存位置”；预览缓存单独目录。
- 文件名清理/冲突处理在保存层统一实现，单元测试覆盖。

### 7.4 视频本地代理

- `VideoStreamServer`：只监听 `127.0.0.1`，随机端口 + 每次会话随机 token。
- 处理 `GET /<token>/<file_id>`，解析 `Range`：
  - 本地已有完整文件 → 直接读本地文件。
  - 无缓存 → 通过文件连接请求所需 range，边写缓存边响应。
  - 支持 200/206、`Content-Range`、`Accept-Ranges`；不支持的请求返回 416/404。
- 缓存：`%LOCALAPPDATA%\ChatBox\preview/stream/<file_id>.part`；LRU 5GB；播放结束不立即删除，按限额清理。
- `QMediaPlayer` 只访问 localhost，不直接访问私有 CA 的服务器。
- 播放失败：显示错误，提供“下载完整文件”与“用系统播放器打开”。

### 7.5 右侧栏/群 UI

- `RightSidebar` 重构为单列容器：
  - 顶部 header（群名/成员数/齿轮，或私聊头像/昵称）。
  - `QStackedWidget`：主视图（成员/资料 + 搜索 + 文件入口）和文件管理页、搜索页。
  - 群聊：搜索框在顶部；成员列表；底部文件入口。
  - 私聊：资料卡；文件入口；搜索框在底部。
  - 文件页：返回、标题、配额、上传、筛选、列表。
  - 搜索页：输入框、结果列表、返回；点击结果跳转。
- `⋯` 按钮改为 `QToolButton` 点击 toggle，不再 `setMenu`。
- 群设置对话框、创建群、加群、邀请、群通知、文件管理均用独立 QDialog/QWidget。
- 左栏 `QTabWidget`：“会话 / 群组”；群组页含默认群、群列表、群通知、创建/搜索入口。
- 群切换：点击群行发送 JOIN（若未加入）并切换主聊天；所有已加入群在登录后统一 JOIN，以便未读/推送。
- 未读计数：服务端 `room_read_state` 权威；`GROUP_LIST` 下发未读数；打开群/聚焦且滚到底时发 `GROUP_MARK_READ`；群列表显示徽标；托盘/标题聚合沿用现有通知逻辑。离线期间的消息在下次登录计入未读。
- 搜索跳转：使用 `HISTORY` + `before_id=message_id+1` 拉取目标页，滚动并高亮。
- 无限滚动：滚动条 valueChanged + 40px 阈值；`loading_older_` 防重入；保存/恢复滚动锚点。

### 7.6 设置

- 新增“文件保存位置”行：路径、选择目录、打开目录；默认 Downloads/ChatBox。
- 路径校验：必须可写、不是安装目录、不是历史目录（可允许同一父目录但不同子目录）。
- 变更立即保存；不移动已有文件；显示说明。
- 现有“本地聊天记录”设置保持不变。

## 8. 上线、兼容与测试

### 8.1 版本与兼容

- 新客户端版本 0.3.0；通过现有自动更新发布。
- 服务端先上线：保留 ROOM_* 命令、默认群、文字消息兼容；再发客户端。
- 0.2.5 客户端继续使用默认群文字聊天；收到文件消息显示 `[文件] name`，不会崩溃。
- 旧客户端不显示新群/文件 UI；新群对其他客户端不可见。
- 协议版本保持 1；单条消息的新字段只在尾部追加；`HISTORY_RESP`/`MESSAGE_SEARCH_RESP` 使用整页之后的元数据表，不逐条插入，避免旧客户端把尾部当成下一条记录。

### 8.2 迁移步骤

1. 备份 MySQL；记录 room 1 消息数、用户数。
2. 执行 schema/migration；保留 room 1，停用其余预置房间。
3. 为现有用户插入默认群成员；配置默认群主。
4. 创建 `files` 目录和权限；安装 ffmpeg；更新 systemd/配置。
5. 部署服务端；验证旧客户端登录/默认群文字/历史/搜索。
6. 发布 0.3.0；验证新群、文件、右侧栏、上滑加载。
7. 观察磁盘、上传失败、群解散清理日志。

### 8.3 测试

- 协议：新命令编解码、单条消息尾部追加兼容、`HISTORY_RESP` 整页元数据表、旧客户端多消息历史解析、未知命令忽略、大小端。
- 群：创建/群号唯一/搜索/申请/审批/邀请/接受/角色/转让/退群/解散/默认群保护/成员上限/配额/加入游标/退群后拒绝访问/未读离线累计。
- 文件：2GB 上限（用小文件模拟长传）、断点续传（杀客户端/断网/服务端重启）、SHA 不匹配、取消、删除、权限、文件名清理、路径冲突。
- 媒体：JPEG/PNG/GIF 缩略图、视频封面、MP4 播放、Seek、 unsupported codec、缓存 LRU。
- UI：右侧栏群/私聊、搜索跳转、文件管理、上滑加载锚点、无“加载更早消息”菜单。
- 兼容：0.2.5 客户端默认群文字/文件回退。
- 迁移：空库/有数据库/默认群成员/历史保留。
- 安全：越权下载、路径穿越、过期 token、群解散后访问、私聊非参与者。
- 性能：2GB 上传内存占用、服务端 fd/线程、磁盘配额、下载 2MiB 背压。

### 8.4 验收

- 新用户注册后自动在默认群。
- 创建群得到 8 位群号；另一个用户搜索群号申请，管理员审批后入群；成员邀请可接受。
- 群主/管理员权限符合 3.1；默认群无法解散/退群。
- 群解散后服务器文件/消息清理，成员本地历史和已下载文件保留，群显示“已解散”。
- 2GB 文件上传中关闭客户端，重启后从断点继续；接收方下载中断后可继续。
- 文件卡片立即出现并显示进度；图片/GIF/视频缩略图/封面正确；视频在客户端边下边播。
- 文件保存位置可改；下载文件保留；文件管理可删除/打开/查看配额。
- 右侧栏 B：群聊先成员，私聊先资料；搜索在栏内；无“加载更早消息”；上滑加载不跳动。
- 0.2.5 客户端仍可在默认群发文字，文件消息显示回退文字。

## 9. 分阶段实施

1. **群基础**：schema/迁移、默认群、群命令、创建/搜索/申请/审批/邀请/角色/解散、左栏 Tab、右侧栏 B（文件入口占位）、上滑加载、搜索迁移。
2. **文件基础**：文件连接/协议、上传下载续传、文件卡片、本地保存设置、文件管理页、图片/GIF 预览、删除/配额/解散清理。
3. **视频与完善**：ffmpeg 缩略图、视频封面、本地流代理/播放器、缓存 LRU、性能/兼容/验收。

每个阶段独立可测试；阶段 2 依赖阶段 1 的群权限，阶段 3 依赖阶段 2 的文件传输。

## 10. 构建者已代决的默认值

以下是本文档替产品做的决定，实施前可被推翻：

- 群号 8 位、顺序分配、默认群 10000001；群名允许重名。
- 普通群成员上限 500；默认群不限成员（`max_members=0`）；每用户 20 个 active 群；建群 5/天；申请/邀请 20/天；已解散墓碑不计入 20 群。
- 默认群群主取最早注册用户，可在服务端配置覆盖；默认群不可退/不可解散。
- 群主转让后原群主变普通成员；管理员不能设管理员/转让/解散。
- 群历史从加入时刻开始（`join_message_id`）；群文件对当前成员开放全部；退群/被移除立即失去服务器访问，本地保留；重新加入从新游标开始。
- 未读服务端权威（`room_read_state`），离线期间累计；打开群/聚焦且滚到底时标记已读。
- 文件保存默认 `Downloads/ChatBox`；改名冲突追加序号；文件名截断 180 字符。
- 预览缩略图/视频缓存放 `%LOCALAPPDATA%\ChatBox\preview`；视频缓存 LRU 5GB。
- 群文件删除权限：上传者/群主/管理员；私聊仅上传者。
- 文件消息文字回退：`[文件] name` / `[图片] name` / `[视频] name`。
- 文件传输走独立 TCP/TLS 文件连接，每用户一条、连接内多路复用，不走 HTTP；服务端 ffmpeg 生成缩略图。
- 文件不进入现有备份；接受磁盘损坏丢文件的风险。
- 新客户端版本 0.3.0；服务端先上线，旧客户端默认群文字/历史兼容（整页元数据表）。

## 11. 需要你最后确认的默认值

以下默认值已写入设计；如果都接受，直接确认整份文档即可：

- 默认群群主指定为当前主账号（user_id=4），而不是“最早注册用户”。
- 普通群成员上限 500；默认群不限；每用户 20 个 active 群；建群 5/天。
- 文件保存默认目录 `Downloads\ChatBox`。
- 群文件删除后消息保留“文件已删除”，本地副本保留；私聊仅上传者能删服务器文件。
- 视频只保证 Windows 当前解码器支持的格式（MP4/H.264 等），不支持的提示下载，是否接受。
