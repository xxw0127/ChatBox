# ChatBox 客户端自动更新 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 启动时自动检查自有服务器的更新清单，用户一键下载、校验、替换便携客户端并重启；同时移除所有客户端服务器/IP/端口/TLS 配置入口。

**Architecture:** 固定服务器地址放进 `client_config.h`。`UpdateManifest` 负责无网络的版本/URL/哈希校验，`UpdateService` 用 QtNetwork 完成 HTTPS 清单和 ZIP 下载，`UpdateInstaller` 负责沙箱解压、校验必需文件、写入 PowerShell 更新助手并重启。更新助手只执行固定模板，等待旧进程退出、镜像替换便携目录、启动新版本并做 5 秒存活检查，失败时恢复备份。

**Tech Stack:** Qt 5.15 Widgets、C++17、QtNetwork、QtTest、Windows PowerShell 5.1、nginx HTTPS 443、现有 OpenSSL CA。

**Spec:** `docs/superpowers/specs/2026-10-27-client-auto-update-design.md`

## Global Constraints

- 平台：Windows 10/11 x64；Qt 5.15.2 MinGW；C++17。
- 生产服务器固定为 `121.5.168.189:9443`，TLS 必须校验包内 `certs/ca.crt`。
- 更新清单固定为 `https://121.5.168.189/updates/latest.json`。
- 更新 ZIP 必须校验 SHA-256；校验失败不得启动更新助手。
- 不静默安装、不强制升级；用户可以在提示中选择稍后。
- 用户数据目录 `%APPDATA%\ChatBox\ChatBox` 和本地 `history.sqlite` 不进入安装目录替换。
- 更新助手只执行应用生成的固定模板，不执行更新 ZIP 内的脚本。
- 不使用未注册的新第三方依赖；只增加 QtNetwork。
- 首次带更新器的版本是 0.2.4，之后用 0.2.5 做真实升级演练。

## Review Focus

- **恶意/损坏清单**：非法 URL、超大 JSON、错误 SHA 或低版本必须被拒绝；对应测试在 Task 2/3。
- **替换到一半失败**：复制失败、新进程闪退或目标目录不可写时必须回滚；对应测试在 Task 5。
- **路径含空格/中文**：安装目录和临时目录不能因 PowerShell 参数拼接失败；对应测试在 Task 5。
- **用户正在使用**：下载/提示/稍后不能阻塞登录和主界面；对应测试在 Task 4。
- **旧设置迁移**：删除服务器输入框后，旧 `frp-oil.com:15318` 和自定义地址不能把客户端重新带回旧/错误服务器；对应测试在 Task 1。

---

### Task 1: 固定服务器配置并删除服务器编辑 UI

**Files:**
- Create: `chatbox/client/core/client_config.h`
- Modify: `chatbox/client/ui/login_page.h/.cpp`
- Modify: `chatbox/client/ui/settings_dialog.h/.cpp`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window.cpp`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/ui/main_window_profile.cpp`
- Modify: `chatbox/client/core/app_settings.cpp`
- Modify: `chatbox/client/tests/test_client_core.cpp`
- Modify: `chatbox/client/README.md`

**Interfaces:**
- Produces:
  - `chatbox::client::kServerHost` (`const char*`, `"121.5.168.189"`)
  - `chatbox::client::kServerPort` (`quint16`, `9443`)
  - `chatbox::client::kServerTls` (`bool`, `true`)
  - `chatbox::client::kUpdateManifestUrl` (`const char*`, `"https://121.5.168.189/updates/latest.json"`)
- Consumes: existing `LoginPage::loginRequested` and `registerRequested` signal shapes; keep them unchanged for this task and emit the fixed constants.

- [ ] **Step 1: Write failing UI/config tests**

Add/extend tests in `test_client_core.cpp`:
- `loginPageHasNoServerSettings`: `findChild<QLineEdit*>("ServerHostEdit")`, `ServerPortSpin`, `ServerTlsCheck`, `AdvancedOptionsButton`, `AdvancedOptionsPanel` are all `nullptr`.
- `settingsDialogHasNoServerSettings`: same checks against `SettingsDialog`.
- `fixedProductionEndpointMatchesConfig`: source contains `121.5.168.189`, `9443`, and `kUpdateManifestUrl`.

Run: `powershell -ExecutionPolicy Bypass -File tests/run_client_tests.ps1`
Expected: FAIL because the UI and config do not exist yet.

- [ ] **Step 2: Add `client_config.h` and use fixed values**

Create the constants and replace these paths:
- `LoginPage` reads/emits `kServerHost`, `kServerPort`, `kServerTls`.
- `MainWindow::switchToAccount`, `onLoginRequested`, `loadSession` use constants; custom session host/port must not override them.
- `SettingsDialog` no longer saves `server/host`, `server/port`, `server/tls`.
- `LoginPage` removes the `AdvancedOptionsButton`, advanced panel, host edit, port spin, TLS checkbox, `setServerSettings`, and related tab-order wiring.
- `SettingsDialog` removes the server form fields; show a read-only `QLabel` with `121.5.168.189:9443` and keep all other settings.

- [ ] **Step 3: Update migration policy**

Extend `migrateServerEndpoint` so any saved `server/host`/`server/port` combination that is not the fixed production endpoint is replaced by the fixed endpoint, and clear `session` when it pointed elsewhere. Keep the old `frp-oil.com:15318` migration test and add a custom-host case.

- [ ] **Step 4: Run tests and build**

Run: `powershell -ExecutionPolicy Bypass -File tests/run_client_tests.ps1`
Expected: PASS.

Run: `powershell -ExecutionPolicy Bypass -File build_client.ps1`
Expected: exit 0.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/core/client_config.h chatbox/client/ui/login_page.* \
  chatbox/client/ui/settings_dialog.* chatbox/client/ui/main_window* \
  chatbox/client/core/app_settings.cpp chatbox/client/tests/test_client_core.cpp \
  chatbox/client/README.md
git commit -m "refactor(client): pin the production server endpoint"
```

---

### Task 2: Add update manifest/version core

**Files:**
- Create: `chatbox/client/core/update_manifest.h`
- Create: `chatbox/client/core/update_manifest.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/tests/tests.pro`
- Modify: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct UpdateManifest {
      QString version;
      QUrl url;
      QString sha256;
      QString notes;
      bool valid = false;
  };
  UpdateManifest parseUpdateManifest(const QByteArray& json, QString* error);
  bool isNewerVersion(const QString& current, const QString& remote);
  ```
- `parseUpdateManifest` rejects invalid JSON, schema != 1, missing fields, non-HTTPS URL, non-`121.5.168.189` host, SHA not 64 hex, and empty version.

- [ ] **Step 1: Write failing tests**

Add tests:
- valid manifest parses fields;
- same/lower version is not newer;
- invalid JSON, wrong schema, missing URL/SHA, HTTP URL, foreign host, malformed SHA are rejected with an error;
- a 65 KiB manifest is rejected by the HTTP layer in Task 3 (parser itself can accept bytes only up to 64 KiB when passed).

Run the test script; expect compile/test failure.

- [ ] **Step 2: Implement `client_config.h` URL constants and parser**

Use `QJsonDocument`, `QUrl`, `QVersionNumber`; no network calls. Keep the parser deterministic and side-effect free.

- [ ] **Step 3: Run tests and commit**

Run the test script; expect all pass.

```bash
git add chatbox/client/core/update_manifest.* chatbox/client/chatbox.pro \
  chatbox/client/tests/tests.pro chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): add update manifest validation"
```

---

### Task 3: Add QtNetwork update transport

**Files:**
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/tests/tests.pro`
- Modify: `chatbox/client/build_client.ps1`
- Modify: `chatbox/client/package_client.ps1`
- Create: `chatbox/client/core/update_service.h`
- Create: `chatbox/client/core/update_service.cpp`
- Modify: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class UpdateService : public QObject {
      Q_OBJECT
  public:
      explicit UpdateService(QObject* parent = nullptr);
      void checkForUpdates(const QString& manifest_url, const QString& ca_path);
      void downloadUpdate(const UpdateManifest& manifest, const QString& target_path);
  signals:
      void updateAvailable(const UpdateManifest& manifest);
      void upToDate();
      void checkFailed(const QString& reason);
      void downloadProgress(qint64 received, qint64 total);
      void downloadFinished(const QString& zip_path);
      void downloadFailed(const QString& reason);
  };
  ```
- Behavior:
  - Use `QNetworkAccessManager`.
  - Apply `QSslConfiguration` from `ca_path`; never disable verification.
  - Limit manifest response to 64 KiB.
  - Follow at most 3 HTTPS redirects, all to the fixed host.
  - Download to a temp path and verify SHA-256 before `downloadFinished`.
  - Abort and remove partial files on cancel/failure.

- [ ] **Step 1: Write failing transport tests**

Add tests with a small local HTTP test server only for success/failure plumbing, plus source guards:
- `updateServiceUsesPinnedCa` (source contains `QSslConfiguration` and `QSslCertificate`);
- `updateServiceRejectsOversizeManifest` (test parser/size guard);
- `updateServiceRemovesPartialDownload` using a temporary directory and a simulated failed write;
- SHA mismatch test through the hash helper.

Run tests; expect failure.

- [ ] **Step 2: Implement transport and hash verification**

Add `QT += network` to both project files. Copy `Qt5Network.dll` to `build/release` in `build_client.ps1` and include it in package validation. Use `QCryptographicHash::Sha256`.

- [ ] **Step 3: Run tests/build and commit**

Run tests, build, and package; verify `Qt5Network.dll` is present in the release folder.

```bash
git add chatbox/client/core/update_service.* chatbox/client/chatbox.pro \
  chatbox/client/tests/tests.pro chatbox/client/build_client.ps1 \
  chatbox/client/package_client.ps1 chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): add HTTPS update transport"
```

---

### Task 4: Add startup prompt and download UI

**Files:**
- Create: `chatbox/client/ui/update_dialog.h/.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window.cpp`
- Modify: `chatbox/client/ui/main_window_support.h`
- Modify: `chatbox/client/main.cpp`
- Modify: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- `UpdateDialog` has object names `UpdateDialog`, `UpdateNowButton`, `UpdateLaterButton`, `UpdateNotesLabel`, `UpdateProgressBar`.
- `MainWindow::checkForUpdates()` is called once via `QTimer::singleShot(1500, this, ...)` after startup.
- On `updateAvailable`, show dialog; on `UpdateNowButton`, start `UpdateService::downloadUpdate`; on progress update bar; on finish, launch installer and quit.
- `UpdateLaterButton` closes the dialog and does not re-prompt in this process.
- Network failures only log and never block login.

- [ ] **Step 1: Write failing UI tests**

Add:
- `updateDialogHasRequiredActions`
- `startupSchedulesOneUpdateCheck` source guard for `singleShot(1500`
- `updatePromptDoesNotBlockLogin` verifies dialog is non-modal / `show()` not `exec()`.

Run tests; expect failure.

- [ ] **Step 2: Implement dialog and startup hook**

Use plain Qt widgets and existing QSS tokens. Do not add network work to the UI thread beyond QNetworkAccessManager's async signals. Keep the prompt over the login or main window.

- [ ] **Step 3: Run tests/build and commit**

```bash
git add chatbox/client/ui/update_dialog.* chatbox/client/ui/main_window* \
  chatbox/client/main.cpp chatbox/client/chatbox.pro \
  chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): prompt for updates at startup"
```

---

### Task 5: Add updater helper with backup and rollback

**Files:**
- Create: `chatbox/client/core/update_installer.h/.cpp`
- Create: `chatbox/client/resources/update_helper.ps1`
- Modify: `chatbox/client/resources/resources.qrc`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/ui/main_window_update.cpp` (new, wired from Task 4)
- Modify: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  bool canInstallUpdates(QString* reason);
  bool extractUpdateZip(const QString& zip, const QString& staging,
                        QString* error);
  bool launchUpdateHelper(quint64 process_id, const QString& source_dir,
                          const QString& target_dir, const QString& executable,
                          QString* error);
  ```
- Helper accepts `-ProcessId`, `-SourceDir`, `-TargetDir`, `-Executable`; waits, backs up, mirrors, starts, checks 5 seconds, rolls back on failure.
- Extraction requires `chatbox.exe`, `Qt5Sql.dll`, `sqldrivers\qsqlite.dll`.

- [ ] **Step 1: Write failing safety tests**

Add:
- `updateHelperUsesWaitBackupRollback` source guard for `Wait-Process`, `Copy-Item`, `update-in-progress`, 5-second check;
- `updateInstallerRejectsIncompletePackage` builds a temp ZIP containing only `chatbox.exe` and verifies rejection;
- `updateInstallerUsesQuotedArguments` source guard for `QProcess::startDetached` argument list (not one concatenated command string);
- `updateExtractionAcceptsCompletePackage` with a temp ZIP created by `tar.exe`/PowerShell.

Run tests; expect failure.

- [ ] **Step 2: Implement extraction and helper**

Use `QProcess` with argument lists. Write the helper from `:/update_helper.ps1` to `%TEMP%\ChatBox\update\`; pass paths as separate args. Do not interpolate manifest values into script source.

- [ ] **Step 3: Implement failure result handoff**

Helper writes `%TEMP%\ChatBox\update-result.txt` on failure. On next startup, `MainWindow` reads and removes it once, then shows a warning. Add test for marker parsing.

- [ ] **Step 4: Run tests/build and commit**

```bash
git add chatbox/client/core/update_installer.* chatbox/client/resources/update_helper.ps1 \
  chatbox/client/resources/resources.qrc chatbox/client/ui/main_window_update.cpp \
  chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): install updates with rollback"
```

---

### Task 6: Publish update feed on server 443

**Files:**
- Create: `chatbox/deploy/nginx-update.conf`
- Create: `chatbox/tools/publish_update.ps1`
- Modify: `chatbox/deploy/setup_nginx.sh`
- Modify: `chatbox/client/README.md`
- Modify: `chatbox/docs/public-access.md`

**Interfaces:**
- `publish_update.ps1 -Version 0.2.5 -Notes "..."` finds `client/release/chatbox-<version>-win64.zip`, computes SHA-256, writes `latest.json`, uploads both over SSH to `/var/www/chatbox-updates`, and keeps a `releases/` history.
- `nginx-update.conf` serves `/updates/` from `/var/www/chatbox-updates` on `listen 443 ssl`, GET/HEAD only, no directory listing.

- [ ] **Step 1: Add failing deployment source guards**

Tests check `nginx-update.conf` contains `listen 443 ssl`, `root /var/www/chatbox-updates`, and no `autoindex on`; publish script contains `sha256`, `scp`, and `121.5.168.189`.

- [ ] **Step 2: Implement/install nginx update site**

Add config, enable via `/etc/nginx/sites-enabled`, open 443 in UFW, test `nginx -t`, reload, and serve a placeholder `latest.json`.

- [ ] **Step 3: Verify external HTTPS**

From the Windows machine:
```powershell
curl.exe -fsS https://121.5.168.189/updates/latest.json
```
Expected: JSON placeholder, TLS 1.3, cert SAN `121.5.168.189`.

- [ ] **Step 4: Commit**

```bash
git add chatbox/deploy/nginx-update.conf chatbox/tools/publish_update.ps1 \
  chatbox/deploy/setup_nginx.sh chatbox/client/README.md chatbox/docs/public-access.md
git commit -m "feat(deploy): publish client updates over HTTPS"
```

---

### Task 7: First updater release and real upgrade rehearsal

**Files:**
- Modify: `chatbox/client/main.cpp`
- Modify: `chatbox/client/package_client.ps1`
- Modify: `chatbox/client/installer/chatbox.iss`
- Modify: `chatbox/client/resources/app.rc`
- Modify: `chatbox/client/README.md`
- Modify: `chatbox/docs/public-access.md`

- [ ] **Step 1: Build and publish 0.2.4 manually**

Set version to `0.2.4`, build/package, upload GitHub Release and `latest.json` with `version=0.2.4`, install it manually on the test account, and confirm startup check says “已是最新版本” or no prompt.

- [ ] **Step 2: Build 0.2.5 and publish manifest**

Set version to `0.2.5`, make a trivial visible change (e.g. release notes/title), package, publish with `publish_update.ps1`, keep 0.2.4 installed.

- [ ] **Step 3: Perform the real one-click update**

Launch 0.2.4, accept the prompt, verify:
- progress downloads;
- SHA matches;
- helper replaces the folder;
- 0.2.5 starts and stays alive 5 seconds;
- local history and QSettings remain;
- no duplicate prompt on next startup.

- [ ] **Step 4: Failure rehearsal**

Temporarily publish a manifest with a wrong SHA or an incomplete ZIP, confirm the client reports failure and stays on 0.2.5/0.2.4 without launching the helper. Restore the good manifest.

- [ ] **Step 5: Commit/release**

Commit version/doc updates, tag `v0.2.5`, publish release and checksum.

---

## Self-Review

- **Spec coverage:** all spec sections map to Tasks 1–7; server 443, fixed endpoint, manifest, prompt, transport, rollback, release rehearsal, and tests are covered.
- **Step granularity:** tests and verification are separate from implementation; each task has a runnable test milestone and commit.
- **Type consistency:** `UpdateManifest`, `UpdateService`, `canInstallUpdates`, `extractUpdateZip`, and `launchUpdateHelper` are defined once and consumed by later tasks.
- **Review focus:** five likely failure modes each have tests in Tasks 1–5.
- **Non-goals:** no delta updates, no code signing, no silent install, no Linux/macOS replacement.
