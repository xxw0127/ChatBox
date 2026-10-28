# 文件传输与文件管理 Implementation Plan（Phase 2/3）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让群聊和私聊支持任意文件（单文件 ≤ 2GB、可暂停/断点续传），文件作为特殊消息跟随聊天记录，聊天内显示文件卡片和图片/GIF 缩略图，右侧栏提供群/私聊文件管理、上传、筛选、删除和配额；视频播放留到 Phase 3。

**Architecture:** 文件数据走独立 TCP/TLS 连接，与聊天主连接分离；同一用户的文件连接内按 `upload_id`/`file_id` 多路复用。服务端使用固定 64KiB 缓冲、临时文件 + SHA-256 校验、`files`/`file_uploads` 表和独立文件 worker，聊天 worker 只处理元数据。客户端新增 `FileTransferClient`（独立 TLS 连接）、`FileTransferManager`（队列/持久化/重启恢复）和文件卡片渲染；本地 SQLite 从 v1 升到 v2。

**Tech Stack:** C++17、Qt 5.15 Widgets、原生 Socket/TLS、MySQL 8.4（兼容 5.7 语法）、SQLite、OpenSSL、ffmpeg/ffprobe。

**Spec:** `chatbox/docs/superpowers/specs/2026-10-28-groups-files-panel-design.md`

## Global Constraints

- 单文件 ≤ 2GB；每群 ready 文件总量 ≤ 20GB；服务器剩余磁盘 < 5GB 拒绝上传。
- 任意文件类型；群文件对当前成员开放全部，不受 `join_message_id` 限制。
- 文件数据不走聊天主连接；每用户最多一条文件连接。
- 文件分块 ≤ 60KiB；上传每 1MiB 回 ack；下载信用窗口 ≤ 2MiB。
- 上传临时文件 `files/tmp/<upload_id>.part`，正式文件 `files/<file_id前两位>/<file_id>`，缩略图 `files/thumbs/<file_id>.jpg`；目录 0700、文件 0640；不使用原文件名作为服务器路径。
- 上传完成必须校验 SHA-256；下载完成写 `.part` 并校验后再改名；断点续传以服务端实际 offset 为准。
- 删除：群文件上传者/群主/管理员；私聊仅上传者。删除后服务器文件立即删除，消息显示“文件已删除”；本地已下载副本保留。
- 群解散删除该群文件和 `files` 行；保留 `rooms`/`room_members` 墓碑。
- 历史/搜索只返回 `join_message_id` 之后的消息；`FILE_LIST` 返回当前群全部 ready 文件。
- 历史/搜索协议在整页旧格式记录之后追加元数据表，禁止逐条追加文件字段。
- 旧 0.2.5 客户端收到文件消息时 `content` 为 `[文件] name`；忽略尾部元数据。
- 客户端文件保存默认 `%USERPROFILE%\Downloads\ChatBox`；不能位于安装目录；文件名清理/冲突后缀；未完成 `.part`。
- 预览缓存 `%LOCALAPPDATA%\ChatBox\preview`；视频 LRU 上限 5GB（Phase 3）。
- 现有协议版本保持 1；新命令放 `0x00C0–0x00CD`。
- 不修改 `chatbox/docs/ui-fix-prompts-kimi-k3.md`；不提交本地日志/构建产物。
- Phase 2 不实现视频播放；视频只显示通用封面/文件卡片，Phase 3 再做本地代理与播放器。

## Review Focus

- **2GB 断点续传/断线恢复**：客户端退出或网络断开后，上传从服务端 offset 继续，下载从 `.part` 继续；不能重复追加、不能把 2GB 读进内存。
- **权限撤销**：退群/被移除/群解散后，FILE_LIST/OPEN/READ/THUMB/DELETE 立即拒绝；旧连接不能继续下载。
- **删除与墓碑**：删除服务器文件后，所有客户端消息变“文件已删除”，本地副本保留；群解散同时清文件和消息。
- **旧客户端兼容**：0.2.5 的 HISTORY/CHAT_PUSH 解析不被尾部元数据破坏；文件消息显示 `[文件] name`。
- **配额与磁盘**：init 和 commit 都检查 20GB/5GB；并发上传不能突破配额；临时文件 7 天清理。
- **消息与文件原子性**：commit 事务失败不得留下可下载的孤儿文件；DB 成功但磁盘失败要回滚/标记。
- **UI 状态机**：上传/下载中/暂停/失败/完成/删除状态正确，切换会话不阻塞、进度节流、重启恢复。

---

### Task 1: MySQL 文件 schema、规则与本地 SQLite v2

**Files:**
- Create: `chatbox/server/file/file_types.h`
- Create: `chatbox/server/file/file_rules.h`
- Create: `chatbox/server/file/file_rules.cpp`
- Modify: `chatbox/deploy/schema.sql`
- Create: `chatbox/deploy/migrate_20261029_files.sql`
- Modify: `chatbox/CMakeLists.txt`
- Create: `chatbox/tests/test_file_rules.cpp`
- Modify: `chatbox/client/core/history_store.h/.cpp`
- Create: `chatbox/client/tests/test_history_migration.cpp`（或 `test_client_core.cpp` 内新增 slot）
- Test: `chatbox/tools/check_file_schema.ps1`

**Interfaces:**
- Produces:
  ```cpp
  constexpr uint64_t kMaxFileSize = 2ull * 1024 * 1024 * 1024;
  constexpr uint64_t kMaxGroupFileBytes = 20ull * 1024 * 1024 * 1024;
  constexpr uint64_t kMinFreeDiskBytes = 5ull * 1024 * 1024 * 1024;
  enum class FileKind : uint8_t { kRoom = 1, kPrivate = 2 };
  enum class FileStatus : uint8_t { kReady = 0, kDeleted = 1 };
  bool validFileSize(uint64_t size);
  bool validFileName(const std::string& name);
  std::string sanitizeFileName(const std::string& name); // Windows 非法字符/保留名/180 字符
  ```
- Schema: `files`, `file_uploads`; `messages`/`private_messages` add `msg_type TINYINT NOT NULL DEFAULT 0`, `file_id BIGINT UNSIGNED NOT NULL DEFAULT 0`; indexes for room/private/status and upload expiry.
- Local SQLite v2: `HistoryStore::schemaVersion()` returns 2; migration adds message columns and `pending_uploads`, `files`, `groups`; failure backs up `history.sqlite` to `.bak` and returns error without deleting.
- Migration one-time; document backup/rollback.

- [ ] **Step 1: Write failing rules tests**

```cpp
CHECK_TRUE(chatbox::validFileSize(0));
CHECK_TRUE(chatbox::validFileSize(chatbox::kMaxFileSize));
CHECK_TRUE(!chatbox::validFileSize(chatbox::kMaxFileSize + 1));
CHECK_EQ(chatbox::sanitizeFileName("a<b>c:d?.txt"), std::string("a_b_c_d_.txt"));
CHECK_EQ(chatbox::sanitizeFileName("CON"), std::string("_CON"));
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_file_rules`
Expected: compile failure.

- [ ] **Step 3: Add schema, migration and rules**

`files` fields: `id, uploader_id, kind, room_id, private_sender_id, private_receiver_id, message_id, name, size, mime, sha256, storage_path, thumb_path, thumb_available, width, height, duration_ms, status, created_at`.
`file_uploads` fields: `upload_id CHAR(36) PK, uploader_id, kind, room_id, private_sender_id, private_receiver_id, name, size, mime, sha256, temp_path, offset, status, created_at, updated_at, expires_at`.
All SQL prepared later; schema uses InnoDB/utf8mb4.

- [ ] **Step 4: Verify rules, schema guards and local migration**

Run:
```
cmake --build chatbox/build --target test_file_rules
ctest --test-dir chatbox/build -R file_rules --output-on-failure
powershell -File chatbox/tools/check_file_schema.ps1
powershell -File chatbox/client/tests/run_client_tests.ps1
```
Expected: PASS; v1 database reopens as v2 with old messages intact.

- [ ] **Step 5: Commit**

```bash
git add chatbox/server/file/file_types.h chatbox/server/file/file_rules.* \
  chatbox/deploy/schema.sql chatbox/deploy/migrate_20261029_files.sql \
  chatbox/CMakeLists.txt chatbox/tests/test_file_rules.cpp \
  chatbox/tools/check_file_schema.ps1 chatbox/client/core/history_store.* \
  chatbox/client/tests
git commit -m "feat(server): add file schema and local history v2"
```

---

### Task 2: 文件协议类型与编解码

**Files:**
- Create: `chatbox/common/file_types.h`
- Create: `chatbox/common/file_protocol.h`
- Create: `chatbox/common/file_protocol.cpp`
- Modify: `chatbox/common/protocol.h`
- Modify: `chatbox/CMakeLists.txt`
- Create: `chatbox/tests/test_file_protocol.cpp`

**Interfaces:**
- Commands:
  ```cpp
  kCmdFileAuth = 0x00C0, kCmdFileUploadInit = 0x00C1,
  kCmdFileUploadChunk = 0x00C2, kCmdFileUploadStatus = 0x00C3,
  kCmdFileUploadCommit = 0x00C4, kCmdFileUploadCancel = 0x00C5,
  kCmdFileOpen = 0x00C6, kCmdFileRead = 0x00C7,
  kCmdFileChunk = 0x00C8, kCmdFileReadAck = 0x00C9,
  kCmdFileDelete = 0x00CA, kCmdFileInfo = 0x00CB,
  kCmdFileThumb = 0x00CC, kCmdFileList = 0x00CD
  ```
- Structs:
  ```cpp
  struct FileMeta {
      uint64_t file_id = 0;
      std::string name;
      uint64_t size = 0;
      std::string mime;
      std::string sha256;
      bool thumb_available = false;
      uint32_t width = 0, height = 0, duration_ms = 0;
  };
  struct FileEntry : FileMeta {
      uint64_t uploader_id = 0;
      std::string uploader_name;
      uint64_t message_id = 0;
      std::string created_at;
      bool can_delete = false;
  };
  ```
- Encoders/decoders for upload init/chunk/status/commit/cancel, open/read/chunk/ack, delete/info/thumb/list.
- Message metadata append:
  ```cpp
  void appendFileMeta(ByteWriter& writer, const FileMeta& meta);
  bool readFileMetaIfPresent(ByteReader& reader, FileMeta& meta);
  std::string encodeFileMetaTable(const QVector<FileMeta>& metas, ...);
  bool decodeFileMetaTable(ByteReader& reader, ...);
  ```
- `content` fallback for file messages is `"[文件] " + name`.

- [ ] **Step 1: Write round-trip and truncation tests**

```cpp
const std::string body = encodeUploadInit(kind, target, "a.bin", 2ull*1024*1024*1024, "application/octet-stream", sha);
UploadInit init; CHECK_TRUE(decodeUploadInit(body, init));
CHECK_EQ(init.size, 2ull*1024*1024*1024);
CHECK_TRUE(!decodeUploadInit(body.substr(0, body.size()-1), init));
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_file_protocol`
Expected: compile failure.

- [ ] **Step 3: Implement protocol**

All strings `u16` length + UTF-8. `FILE_CHUNK` data is the remaining body after `file_id`/`offset`; encode as raw bytes, not a string. `FILE_UPLOAD_CHUNK` data is remaining bytes after `upload_id`/`offset`.

- [ ] **Step 4: Run tests and build**

Run:
```
cmake --build chatbox/build --target test_file_protocol chatbox_server
ctest --test-dir chatbox/build -R file_protocol --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add chatbox/common/file_* chatbox/common/protocol.h chatbox/CMakeLists.txt chatbox/tests/test_file_protocol.cpp
git commit -m "feat(common): add file transfer protocol"
```

---

### Task 3: 文件连接认证与独立文件 worker

**Files:**
- Create: `chatbox/server/file/file_connection_manager.h/.cpp`
- Create: `chatbox/server/file/file_service.h/.cpp`
- Create: `chatbox/server/file/file_worker_pool.h/.cpp`
- Modify: `chatbox/server/business/worker_pool.h/.cpp`
- Modify: `chatbox/server/main.cpp`
- Modify: `chatbox/server/config.h/.cpp`（`files_dir`、`file_workers`、`ffmpeg_path`、`ffprobe_path`）
- Modify: `chatbox/server/auth/session_manager.*`（仅校验 token 不绑定）
- Test: `chatbox/tests/test_file_auth.cpp`

**Interfaces:**
- `FileConnectionManager::authenticate(connection_id, token, user_id, error)` validates session; enforces one file connection per user; returns previous connection id to close.
- `FileWorkerPool::submit(connection_id, std::function<void()>)` dispatches by `connection_id % worker_count`, preserving order.
- `FileService::handle(const Frame&, const FileConnection&)` returns `FileResult { std::vector<Frame> responses; bool close; }`.
- `FILE_AUTH` response body: `u64 user_id`.
- File commands only; chat commands on a file connection return `1001`.
- `FILE_AUTH` failure `1002`; second connection replaces old (close old) or rejects per config; choose replace and log.

- [ ] **Step 1: Write failing auth tests**

Use fake session records/token hashes; assert valid token maps to user, invalid token returns 1002, second auth closes the first connection, chat command on file connection is rejected.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_file_auth`

- [ ] **Step 3: Implement manager, pool and dispatch**

`main.cpp`: `file_command = cmd >= kCmdFileAuth && cmd <= kCmdFileList`; if file command, submit to `file_pool`; do not require main-connection login. The file worker uses `io_thread_refs[io_thread_id]->post` to enqueue responses. Authenticate before any other file command.

- [ ] **Step 4: Run tests/build**

Run:
```
cmake --build chatbox/build --target test_file_auth chatbox_server
ctest --test-dir chatbox/build -R file_auth --output-on-failure
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add chatbox/server/file/file_connection_manager.* chatbox/server/file/file_service.* \
  chatbox/server/file/file_worker_pool.* chatbox/server/business chatbox/server/main.cpp \
  chatbox/server/config.* chatbox/server/auth/session_manager.* chatbox/tests/test_file_auth.cpp
git commit -m "feat(server): add authenticated file connection workers"
```

---

### Task 4: 上传生命周期（init/chunk/status/commit/cancel）

**Files:**
- Modify: `chatbox/server/file/file_service.cpp`
- Modify: `chatbox/server/db/mysql_connection.h/.cpp`（或新建 `mysql_connection_files.cpp`）
- Create: `chatbox/deploy` storage dirs only at runtime（代码创建）
- Create: `chatbox/tools/test_files_integration.py`
- Test: `chatbox/tests/test_file_upload_logic.cpp`

**Interfaces:**
- `FileService` methods:
  ```cpp
  bool initUpload(user_id, kind, target_id, name, size, mime, sha256, UploadState&);
  bool appendChunk(upload_id, user_id, offset, data, ack_offset, error);
  bool uploadStatus(upload_id, user_id, offset, status, error);
  bool commitUpload(upload_id, user_id, message_id, file_id, created_at, error);
  bool cancelUpload(upload_id, user_id, error);
  ```
- `UploadState` persisted in `file_uploads` + temp file; `commit` opens a transaction, verifies size/SHA-256, moves temp to final path, inserts `files` + message (`content = "[文件] name"`, `msg_type=1`, `file_id`), commits, then broadcasts.
- Group quota: `SELECT COALESCE(SUM(size),0) FROM files WHERE kind=1 AND room_id=? AND status=0 FOR UPDATE` inside commit transaction; disk free check `std::filesystem::space`.
- Commit idempotent: if upload already committed, return existing message/file id.

- [ ] **Step 1: Write failing logic tests**

Test size limit, SHA mismatch, quota exceeded, missing temp file, duplicate commit returns same ids, cancel removes temp row/file.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_file_upload_logic`

- [ ] **Step 3: Implement state machine and DB**

Use fixed 64KiB buffer; `open`/`pwrite`/`fsync`; no full-file read. Chunk offset must equal current offset; stale/duplicate chunk returns current offset. Ack every 1MiB.

- [ ] **Step 4: Run unit + integration**

Run:
```
cmake --build chatbox/build --target test_file_upload_logic chatbox_server
ctest --test-dir chatbox/build -R file_upload_logic --output-on-failure
python chatbox/tools/test_files_integration.py --host 127.0.0.1 --port 19100
```
Expected: integration uploads a small file, resumes after partial chunk, commits, verifies message/DB/disk, cancels another.

- [ ] **Step 5: Commit**

```bash
git add chatbox/server/file chatbox/server/db chatbox/tests/test_file_upload_logic.cpp \
  chatbox/tools/test_files_integration.py
git commit -m "feat(server): implement resumable uploads"
```

---

### Task 5: 下载、列表、信息、缩略图与删除

**Files:**
- Modify: `chatbox/server/file/file_service.cpp`
- Modify: `chatbox/server/db/mysql_connection_files.cpp`
- Modify: `chatbox/server/group/group_service.cpp`（解散时删除文件）
- Modify: `chatbox/server/room/room_service.cpp`（文件消息删除权限钩子）
- Test: `chatbox/tests/test_file_download_logic.cpp`

**Interfaces:**
- `FILE_OPEN` -> `u64 total_size, string range_token`.
- `FILE_READ` -> one or more `FILE_CHUNK` frames, `data` ≤ 60KiB, stop at 2MiB or credit.
- `FILE_READ_ACK` extends credit.
- `FILE_LIST` response: `u16 count, u32 total_count, u64 used_bytes, entries...` with `can_delete`.
- `FILE_INFO` and `FILE_THUMB` return metadata/bytes.
- `FILE_DELETE(message_id)` checks uploader/owner/admin (group) or uploader (private), deletes disk then marks `files.status=1`, updates message content `"文件已删除"`, broadcasts update.

- [ ] **Step 1: Write failing tests**

Test range read, membership revoked mid-download, private participant check, delete permission matrix, group quota usage, tombstone after delete.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_file_download_logic`

- [ ] **Step 3: Implement download/list/delete**

Reads use `pread` loops and `FILE_CHUNK`; no 2GB memory. `FILE_LIST` page size max 100, before_file_id cursor. `used_bytes` from files table. Delete disk first; on failure keep DB row and return error; on success update.

- [ ] **Step 4: Run tests/integration**

Run unit + integration download/resume/delete and group disband cleanup.

- [ ] **Step 5: Commit**

```bash
git add chatbox/server/file chatbox/server/db chatbox/server/group chatbox/server/room \
  chatbox/tests/test_file_download_logic.cpp chatbox/tools/test_files_integration.py
git commit -m "feat(server): implement file download, listing and deletion"
```

---

### Task 6: 文件消息元数据与旧客户端历史兼容

**Files:**
- Modify: `chatbox/server/room/room_service.cpp`
- Modify: `chatbox/server/private/private_service.cpp`
- Modify: `chatbox/server/history/history_service.cpp`
- Modify: `chatbox/common/protocol.h`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/core/chat_message.h`
- Create: `chatbox/tests/test_file_message_compat.cpp`

**Interfaces:**
- Single message frames append `msg_type/file_id/file_name/size/mime/thumb/width/height/duration` after existing fields.
- `HISTORY_RESP` / `MESSAGE_SEARCH_RESP`: after old records append `u16 meta_count` + per-record metadata keyed by `record_index`.
- Decoders: if trailing bytes remain, parse table; if absent/0, all text.
- `ChatMessage.msg_type` and `ChatMessage.file_meta`.
- FTS/DB message inserts set `msg_type=1`, `file_id`, content fallback.

- [ ] **Step 1: Write failing compatibility tests**

Test old-format payload decodes as text; new payload decodes metadata; 0.2.5 reader truncation ignores tail; meta table index mapping.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_file_message_compat`

- [ ] **Step 3: Implement append/table encoding**

Update send response/push/history/search; keep field order stable.

- [ ] **Step 4: Run tests/build**

Run unit; build server; client tests unchanged.

- [ ] **Step 5: Commit**

```bash
git add chatbox/server/room chatbox/server/private chatbox/server/history \
  chatbox/common/protocol.h chatbox/client/core/chat_message.h \
  chatbox/client/ui/main_window_net.cpp chatbox/tests/test_file_message_compat.cpp
git commit -m "feat: append file metadata with legacy history compatibility"
```

---

### Task 7: 缩略图/ffprobe、存储配置与运维

**Files:**
- Create: `chatbox/server/file/file_thumbnailer.h/.cpp`
- Modify: `chatbox/server/main.cpp`
- Modify: `chatbox/server/config.h/.cpp`
- Modify: `chatbox/server/file/file_service.cpp`
- Create: `chatbox/tools/install_files_runtime.sh`
- Create: `chatbox/docs/files-deployment.md`
- Test: `chatbox/tests/test_file_thumbnail_logic.cpp`

**Interfaces:**
- `Thumbnailer::enqueue(file_id, path, mime)` uses a small thread pool; output JPEG ≤320×180; sets width/height/duration; failures set zeros and `thumb_available=0`.
- Config: `files_dir` default `/home/ubuntu/chatbox/files`, `ffmpeg_path` default `/usr/bin/ffmpeg`, `ffprobe_path`.
- Startup creates dirs 0700, checks writability; missing ffmpeg logs WARN but does not fail.
- systemd `ReadWritePaths`/`ProtectSystem` updated; files dir excluded from nginx.

- [ ] **Step 1: Write failing tests**

Fake/missing ffmpeg path returns zeros; image fixture creates JPEG; video fixture (if available) probes duration; duplicate enqueue only once.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build chatbox/build --target test_file_thumbnail_logic`

- [ ] **Step 3: Implement thumbnailer and deployment script**

Use `fork`/`exec` or `posix_spawn` with timeout; never shell-concatenate paths.

- [ ] **Step 4: Verify on Linux test server**

Run `ffmpeg -version`, generate image/video fixtures, invoke thumbnailer, assert DB fields; run `install_files_runtime.sh` idempotently.

- [ ] **Step 5: Commit**

```bash
git add chatbox/server/file/file_thumbnailer.* chatbox/server/main.cpp chatbox/server/config.* \
  chatbox/server/file/file_service.cpp chatbox/tools/install_files_runtime.sh \
  chatbox/docs/files-deployment.md chatbox/tests/test_file_thumbnail_logic.cpp
git commit -m "feat(server): generate file thumbnails and document storage"
```

---

### Task 8: 客户端文件连接、任务持久化与恢复

**Files:**
- Create: `chatbox/client/core/file_transfer_client.h/.cpp`
- Create: `chatbox/client/core/file_transfer_manager.h/.cpp`
- Modify: `chatbox/client/core/history_store.h/.cpp`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/tests/tests.pro`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- `FileTransferClient` owns one TLS socket; `auth(token)`, `uploadInit`, `uploadChunk`, `uploadCommit`, `openRead`, `readAck`, `cancel`, signals for responses/errors; reconnect with backoff.
- `FileTransferManager`:
  ```cpp
  void enqueueUpload(const PendingUpload& task);
  void pauseUpload(const QString& upload_id);
  void resumeUpload(const QString& upload_id);
  void cancelUpload(const QString& upload_id);
  void requestDownload(quint64 file_id, const QString& save_path, bool open_after);
  ```
  max 2 uploads/2 downloads; statuses `queued/uploading/paused/verifying/downloading/failed/done`.
- SQLite `pending_uploads` and `files` rows; resume on login via `FILE_UPLOAD_STATUS`/`FILE_OPEN`.
- SHA-256 incremental via `QCryptographicHash`.

- [ ] **Step 1: Write failing tests**

Test task persistence/reload, resume offset, pause/cancel, SHA mismatch, download `.part` rename, path sanitize/collision, queue limit.

- [ ] **Step 2: Run to verify failure**

Run: `powershell -File chatbox/client/tests/run_client_tests.ps1`

- [ ] **Step 3: Implement client transport/manager**

Separate thread; do not block UI. Throttle progress signals to 200ms. Stop/cleanup on logout; keep tasks for next login.

- [ ] **Step 4: Run client tests/build**

Run: `run_client_tests.ps1`, `build_client.ps1`; expected pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/core/file_transfer_* chatbox/client/core/history_store.* \
  chatbox/client/ui/main_window_net.cpp chatbox/client/chatbox.pro \
  chatbox/client/tests
git commit -m "feat(client): add file transfer client and persistent tasks"
```

---

### Task 9: 文件消息模型、卡片与图片/GIF 缩略图

**Files:**
- Modify: `chatbox/client/core/chat_message.h`
- Modify: `chatbox/client/core/message_store.cpp`
- Modify: `chatbox/client/core/message_renderer.cpp`
- Modify: `chatbox/client/ui/chat_view.h/.cpp`
- Create: `chatbox/client/core/file_thumbnail_cache.h/.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- `ChatMessage` adds `msg_type`, `FileMeta file`.
- Renderer emits file card HTML with `file-action://open|save|pause|resume|cancel|delete|open-folder/<message_id>`; image/GIF `<img>` through `QTextDocument::addResource`.
- Progress text `"上传中 42% · 420KB/1.0MB"`; progress re-render throttled.
- Thumbnail cache path `%LOCALAPPDATA%/ChatBox/preview/thumbs/<file_id>.jpg`.

- [ ] **Step 1: Write failing tests**

Test text/file render distinction, fallback `[文件] name`, action URL mapping, thumbnail resource load, progress formatting.

- [ ] **Step 2: Run to verify failure**

Run: `run_client_tests.ps1`

- [ ] **Step 3: Implement model/renderer/cache**

Keep one top-level QTextTable per message; do not break `ChatView` mapping.

- [ ] **Step 4: Run tests/build**

Expected pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/core/chat_message.h chatbox/client/core/message_store.cpp \
  chatbox/client/core/message_renderer.cpp chatbox/client/core/file_thumbnail_cache.* \
  chatbox/client/ui/chat_view.* chatbox/client/chatbox.pro chatbox/client/tests
git commit -m "feat(client): render file cards and thumbnails"
```

---

### Task 10: 输入框回形针/拖拽/粘贴与右侧栏文件管理

**Files:**
- Modify: `chatbox/client/ui/main_window.cpp/.h`
- Modify: `chatbox/client/ui/main_window_groups.cpp`
- Create: `chatbox/client/ui/file_manager_page.h/.cpp`
- Create: `chatbox/client/ui/file_upload_dialog.h/.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Paperclip opens file dialog; drag/drop on chat view; clipboard image saved to temp then queued.
- Right panel group page file button -> file manager page; shows `文件 N 项 · 已用 X / 20GB`; upload/filter/list/open/save/delete/open-folder.
- Private file page same with no quota.
- Empty state `"还没有文件"`.
- After upload commit, card replaces pending local card; other clients receive push.

- [ ] **Step 1: Write failing UI/source tests**

Test object names `FilePaperclipButton`, `FileManagerPage`, `FileUploadButton`, `FileFilterEdit`, `FileQuotaLabel`; disabled for dissolved/offline; group vs private file button; upload triggers manager.

- [ ] **Step 2: Run to verify failure**

Run: `run_client_tests.ps1`

- [ ] **Step 3: Implement entry + manager page**

Keep right panel stack: home/files/search. File page back returns home.

- [ ] **Step 4: Run tests/build**

Expected pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/ui/main_window* chatbox/client/ui/file_manager_page.* \
  chatbox/client/ui/file_upload_dialog.* chatbox/client/chatbox.pro chatbox/client/tests
git commit -m "feat(client): add file upload entry and manager"
```

---

### Task 11: 文件保存位置、图片/GIF 预览与打开

**Files:**
- Modify: `chatbox/client/core/app_settings.h/.cpp`
- Modify: `chatbox/client/ui/settings_dialog.h/.cpp`
- Create: `chatbox/client/ui/image_preview_dialog.h/.cpp`
- Modify: `chatbox/client/ui/main_window_chat.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- `AppSettings::fileSaveDirectory()` default `QStandardPaths::DownloadLocation + "/ChatBox"`.
- Validation: writable, not under app dir, not equal history dir; conflict suffix ` (1)`; `.part` for incomplete.
- Image/GIF preview dialog: fit/actual size, save, open folder; GIF uses `QMovie`.
- File card open for non-image downloads then `QDesktopServices::openUrl`.

- [ ] **Step 1: Write failing tests**

Path validation, collision naming, reserved names, `.part` rename, image preview dialog object names.

- [ ] **Step 2: Run to verify failure**

Run: `run_client_tests.ps1`

- [ ] **Step 3: Implement settings/preview/open**

- [ ] **Step 4: Run tests/build**

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/core/app_settings.* chatbox/client/ui/settings_dialog.* \
  chatbox/client/ui/image_preview_dialog.* chatbox/client/ui/main_window_chat.cpp \
  chatbox/client/tests
git commit -m "feat(client): add file save settings and image preview"
```

---

### Task 12: 验收、权限回归、2GB 断点与 0.2.5 兼容

**Files:**
- Create: `chatbox/tools/test_files_acceptance.py`
- Modify: `chatbox/client/README.md`
- Modify: `chatbox/README.md`
- Modify: `chatbox/docs/files-deployment.md`
- Test: all

**Interfaces:**
- Acceptance script uses disposable DB/server: upload small/large sparse file, kill/restart client between chunks, verify SHA and message; download resume; revoke membership mid-transfer; delete file; group disband removes files; quota/disk checks; 0.2.5-compatible HISTORY/CHAT_PUSH parser smoke.
- Add a 2GB sparse file test that does not allocate 2GB RAM; verify offset/commit and SHA (may take minutes; mark slow).
- Update docs with server-first migration, backup/rollback, `files` dir, ffmpeg, quota.

- [ ] **Step 1: Write acceptance script**

Use Python TLS/protocol client and disposable schema. Fail on any assertion.

- [ ] **Step 2: Run on test server**

Expected: PASS; report sizes/timings.

- [ ] **Step 3: Run full regression**

```
run_client_tests.ps1
build_client.ps1
cmake --build build && ctest --test-dir build --output-on-failure
python tools/test_files_acceptance.py
```
Expected: all pass; 0.2.5 smoke passes.

- [ ] **Step 4: Commit and request review**

```bash
git add chatbox/tools/test_files_acceptance.py chatbox/client/README.md chatbox/README.md \
  chatbox/docs/files-deployment.md
git commit -m "test: add file transfer acceptance and deployment docs"
```

Then request a whole-branch review before merge/publish.

---

## Self-Review

- **Spec coverage:** file schema/quota, protocol, file connection, upload/download/resume, delete/permissions, file messages/legacy metadata, thumbnails, client cards/preview, file manager, save settings, acceptance. Video intentionally deferred to Phase 3.
- **Type consistency:** `FileMeta`, `FileEntry`, `FileKind`, `FileStatus`, `UploadState` defined once in Task 1/2 and reused; command IDs fixed.
- **Review Focus:** resume/2GB in Tasks 4/8/12; revoked access in 5/12; delete/tombstone in 5/7; legacy in 6/12; quota/disk in 4/12; atomic commit in 4/5; UI state in 9/10.
- **Non-goals:** video playback/proxy/LRU 5GB; HTTP file service; client DB encryption; virus scanning; group file recycle bin.
