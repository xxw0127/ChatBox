# ChatBox 聊天体验与在线状态实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在独立工作线上完成消息头像/ID、`@` 提及面板、气泡垂直居中、搜索跳转高亮、移除悬浮操作条、在线状态实时推送六项改动，且不改数据库 schema、不破坏旧客户端。

**Architecture:** 客户端以 `message_renderer`（纯 HTML）→ `ChatView`（文档/资源/绘制）→ `MainWindow`（数据与交互）三层扩展；`@` 面板拆成纯函数 + 独立弹层控件；服务端新增一条 `PRESENCE_PUSH` 推送，复用现有 io 线程 `postMulticast` 广播机制。

**Tech Stack:** C++17、Qt 5.15 Widgets（MinGW 8.1）、原生 Socket/TLS、自定义二进制协议（大端 + TLV）、Linux epoll 服务端（CMake/CTest）。

**Spec:** `chatbox/docs/superpowers/specs/2026-10-28-chat-ux-presence-design.md`

## Global Constraints

- C++17；4 空格缩进；K&R；类型用 `<cstdint>` 固定宽度。
- 客户端 UI 只在主线程；网络在 `NetworkThread`，通过信号槽回主线程。
- 协议字段一律网络字节序，用 `ByteWriter/ByteReader`；禁止直接发结构体。
- 不改 MySQL / 本地 SQLite schema；既有帧字段顺序不变，只追加新命令。
- 数据库只由服务端访问；客户端不直连。
- 服务端业务层不出现平台宏；IO 回调不做阻塞操作（含查库）。
- 每个任务结束必须：客户端测试全绿（基线 147 passed / 0 failed），或服务端对应测试通过。
- 所有新增中文文案使用 `QString::fromUtf8` / 源码 UTF-8 字面量，与现有文件一致。

## Review Focus

1. **超长 CJK/emoji 消息 + 头像列**：气泡仍受 72% 宽度上限约束，头像不被挤掉，文字完整换行。
2. **无头像 / 头像迟到**：首字母头像先出现，服务端头像到达后替换，不出现破图或半个空列。
3. **`@` 的误触发**：邮箱 `a@b.com`、`@` 前无空白、光标在 `@` 之前都不弹面板；IME 组合输入不崩溃。
4. **跳转到旧消息时又来新消息**：可见页不出现中间断档，不误发已读回执，「回到最新」能恢复。
5. **presence 乱序 / 重连**：同一用户快速上下线后，客户端最终状态与服务器一致，断线缓存不覆盖重连后的权威列表。

---

### Task 1: 删除悬浮操作条

**Files:**
- Delete: `chatbox/client/ui/message_action_bar.h`
- Delete: `chatbox/client/ui/message_action_bar.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/tests/tests.pro`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window.cpp`
- Modify: `chatbox/client/ui/main_window_chat.cpp`
- Modify: `chatbox/client/ui/chat_view.h`
- Modify: `chatbox/client/ui/chat_view.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: 现有 `ChatView::messageMenuRequested`、`MainWindow::showMessageMenu`。
- Produces: 无新接口；`ChatView` 不再有 hover 信号，`MainWindow` 不再有 `message_action_bar_`。

- [ ] **Step 1: 更新测试，先让旧行为失败**

在 `test_client_core.cpp`：
- 删除 `void messageActionBarHasSingleSoftShadow();`、`void messageActionBarIsKeyboardReachable();`、`void chatViewHoverReentersSameMessage();` 三个声明与实现。
- 删除 `#include "ui/message_action_bar.h"`。
- 新增 `void messageMenuKeepsReplyAndRecall();`，断言 `readClientSource("ui/main_window_chat.cpp")` 的 `showMessageMenu` 段落同时包含 `"回复"`、`"撤回"`、`"重发"`、`"复制"`。

- [ ] **Step 2: 记录删除前基线**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 全部 PASS，记录总数（基线 147）。这一步确认新增的右键菜单测试在重构前就成立。

- [ ] **Step 3: 删除实现与所有引用**

- 删除两个 `message_action_bar.*` 文件；从 `chatbox.pro` / `tests.pro` 移除条目。
- `main_window.h`：删 `class MessageActionBar;`、`showMessageActions`、`hideMessageActionBar`、`hideMessageActionsUnlessHovered`、`action_bar_message_id_`、`message_action_bar_`。
- `main_window.cpp`：删 135–170 行的 hover/action bar 连接、453–454 行创建；滚动条回调只保留 `chat_view_->clearHoverState()` 删除后的空回调则整体删掉。
- `main_window_chat.cpp`：删 `showMessageActions`、`hideMessageActionBar`、`hideMessageActionsUnlessHovered` 三个函数；删 `selectRoom`/`selectPrivate`/`resetContext` 里对它们的调用。
- `chat_view.h/cpp`：删 `hoveredMessage`/`hoverCleared` 信号、`clearHoverState`、`hovered_id_`、`mouseMoveEvent`/`mousePressEvent`/`leaveEvent` 的 hover 逻辑、viewport 的 Leave 事件过滤器。
- `grep -rn "MessageActionBar\|hoveredMessage\|hoverCleared\|clearHoverState\|hideMessageActionBar" chatbox/client` 应只剩无引用。

- [ ] **Step 4: 全量测试与客户端构建**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 全部 PASS（数量 = 147 - 3 + 1）。
Run: `cd chatbox/client; .\build_client.ps1`
Expected: `Build complete: ...\chatbox.exe`

- [ ] **Step 5: 提交**

```bash
git add -A chatbox/client
git commit -m "refactor(client): remove hover message action bar"
```

---

### Task 2: 渲染器头像列、昵称 + ID、气泡行高

**Files:**
- Modify: `chatbox/client/core/message_renderer.h`
- Modify: `chatbox/client/core/message_renderer.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: `publicUserId()`（`core/identity.h`）、`theme::kMuted`、`theme::kSurfaceSoft`。
- Produces:
  ```cpp
  using AvatarSrcMap = QHash<quint64, QString>;
  QString renderMessageFragment(const chatbox::client::ChatMessage& message,
                                quint64 self_user_id,
                                const AvatarSrcMap& avatar_srcs,
                                bool include_date_header,
                                const QString& date_label,
                                bool show_sender_name = true);
  QString renderConversationHtml(const QVector<chatbox::client::ChatMessage>& messages,
                                 quint64 self_user_id,
                                 const AvatarSrcMap& avatar_srcs,
                                 QVector<MessageKey>* rendered_messages = nullptr);
  ```
- HTML 结构：顶层 `<table cellspacing=0 cellpadding=4>` 为消息范围；一个单元格放头像 `<img src="{src}" width="36" height="36">`，另一个单元格放「昵称 + ID」行和原来的 1px 包裹表 + 气泡表。对方头像在左、自己在右。正文 `line-height: 110%`。

- [ ] **Step 1: 写失败测试**

在 `test_client_core.cpp` 增加并声明：
- `rendererShowsAvatarAndPublicId`：来消息（sender 10001、昵称「李硬」、`avatar_srcs{10001, "chatbox-avatar://10001?v=3"}`），断言 HTML 含该 src、`U00010001`、`width="36"`。
- `rendererOwnMessageHidesNameAndId`：outgoing 消息断言含头像 src、不含昵称、不含 `U000`。
- `rendererKeepsSenderGrouping`：同发送者 1 分钟内两条消息，第一条含昵称，第二条不含，但两条都含头像 src。
- `rendererRecalledMessageHidesNameAndId`：recalled 消息含头像、不含昵称/ID。
- `bubbleTextIsVerticallyCentered`：渲染一条 outgoing 单行消息（气泡内只有正文），用 `QTextDocument::drawContents` 画到 `QImage`，统计黑色文字像素与气泡白色区域，断言 `abs(top_gap - bottom_gap) <= 1`。

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 编译失败（`renderMessageFragment` 参数数量不匹配）。

- [ ] **Step 3: 实现渲染结构**

- `senderLabel` 保留；新增 `senderIdentity(message)` 返回 `昵称  U00010001`（昵称为空时用 ID）。
- `renderMessageFragment` 按上面的 HTML 结构输出；头像 src 从 `avatar_srcs` 取，缺失时不输出 `<img>`（调用方负责提供兜底）。
- 昵称 + ID 行仅在 `!outgoing && show_sender_name && !recalled` 时输出；放在气泡外、包裹表上方。
- 正文 `line-height` 从 `150%` 改 `110%`。
- `messageNeedsWidthCap` 的 `width="72%"` 加在顶层消息表上。
- 更新 `renderConversationHtml` 与所有调用点（先让编译通过，MainWindow 的调用在 Task 4 接头像）。

- [ ] **Step 4: 运行测试并修到全绿**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 新增 5 个测试 PASS，旧渲染测试按新结构更新后 PASS。
Run: `cd chatbox/client; .\build_client.ps1`
Expected: 构建成功（调用点先传空 `AvatarSrcMap`，Task 4 再接头像）。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/core/message_renderer.* chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): render avatars and public ids in messages"
```

---

### Task 3: ChatView 头像资源与气泡定位

**Files:**
- Modify: `chatbox/client/ui/chat_view.h`
- Modify: `chatbox/client/ui/chat_view.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: Task 2 的 HTML（头像 `img` + 两层表）。
- Produces:
  ```cpp
  void setConversationHtml(const QString& html,
                           const QVector<chatbox::client::MessageKey>& message_keys,
                           const QHash<QString, QImage>& avatar_images);
  void appendFragment(const QString& html,
                      const chatbox::client::MessageKey& key,
                      const QHash<QString, QImage>& avatar_images);
  ```
- `MessageRange` 增加 `QTextTable* bubble_wrapper`（1px 外框表）与 `QTextTable* bubble`（真正气泡表）。

- [ ] **Step 1: 写失败测试**

- `chatViewRegistersAvatarResources`：HTML 含 `<img src="chatbox-avatar://7?v=1">`，传入 `{url, image}`；断言 `document()->resource(QTextDocument::ImageResource, QUrl(url))` 是有效 `QImage`。
- `chatViewBubbleRectExcludesAvatar`：渲染一条来消息（带头像），断言
  `messageOuterBubbleRect(id).left() > 40`（36px 头像 + 内边距在左侧）且
  `messageBubbleRect(id).width() < messageOuterBubbleRect(id).width()`。
- 更新 `messageBubblesHaveRoundedCorners` / `messageBubblesDrawRightEdge`：圆角遮罩应只覆盖气泡外框，不覆盖头像。

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 编译失败（新参数）。

- [ ] **Step 3: 实现**

- `setConversationHtml` / `appendFragment` 在 `setHtml` / `insertHtml` **之前** 遍历 `avatar_images`，调用 `document()->addResource(QTextDocument::ImageResource, QUrl(url), image)`。
- `buildMessageRanges` / append 路径：顶层表 → 扫描每个 cell，找到第一个内部 `QTextTable`（头像 cell 里只有图片，没有表），即包裹表；包裹表 cell(0,0) 内的第一个表即气泡表。
- `outerBubbleRect` 用 `bubble_wrapper`；`messageBubbleRect` 用 `bubble`；`paintRoundedBubbleCorners` 用 `bubble_wrapper`。

- [ ] **Step 4: 测试**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 全部 PASS。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/ui/chat_view.*
git commit -m "feat(client): inject avatar resources and map bubbles with avatar columns"
```

---

### Task 4: MainWindow 提供头像并异步刷新

**Files:**
- Modify: `chatbox/client/core/avatar_cache.h`
- Modify: `chatbox/client/core/avatar_cache.cpp`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window_chat.cpp`
- Modify: `chatbox/client/ui/main_window_profile.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  quint32 AvatarCache::version(quint64 user_id) const; // 0 = 无缓存/无头像
  // main_window.h，MainWindow 类之前：
  struct MessageAvatars {
      chatbox::client::AvatarSrcMap srcs;
      QHash<QString, QImage> images;
  };
  MessageAvatars MainWindow::messageAvatars(const QVector<chatbox::client::ChatMessage>& messages) const;
  void MainWindow::requestMissingMessageAvatars(const QVector<chatbox::client::ChatMessage>& messages);
  ```
- URL 规则：有缓存版本 → `chatbox-avatar://<id>?v=<version>`；否则 `chatbox-avatar://<id>?v=0`，图片用 `fallbackAvatar(name, id, 36)`。

- [ ] **Step 1: 写失败测试**

- `avatarCacheExposesVersion`：`store(7, 3, image)` 后 `version(7) == 3`；未知用户返回 0。
- `messageAvatarsUseFallbackForUnknownSender`：纯函数化的 URL 构造放 `core/message_renderer.h` 里：
  ```cpp
  QString avatarSrc(quint64 user_id, quint32 version);
  ```
  断言未知版本生成 `...?v=0`。
- `mainWindowRequestsAvatarForMessageSender`：源码检查 `main_window_chat.cpp` 的 `renderCurrentConversation` 段落包含 `requestMissingMessageAvatars(`。

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: `avatarCacheExposesVersion` 编译失败。

- [ ] **Step 3: 实现**

- `AvatarCache::version()` 返回 `entries_[id].loaded ? version : 0`（无条目返回 0）。
- `messageAvatars`：遍历消息，按 `sender_id` 去重；`avatar_cache_.lookup` 有图则 `circularAvatar(image, 36)`，否则 `fallbackAvatar(sender_name, sender_id, 36)`；同时填 `srcs`。
- `renderCurrentConversation` 与 `addIncomingMessage` 使用该结构调用新签名。
- `requestMissingMessageAvatars`：对 `!avatar_cache_.contains(sender_id)` 且非自己的发送者调用 `requestPeerAvatar(sender_id)`。
- `onProfileAvatarReceived`：头像入库后，若该 user_id 在当前 `message_store_.messages()` 里，调用 `renderCurrentConversation(ScrollMode::kPreserve)`（已有 `refreshMemberListAvatar` 保留）。

- [ ] **Step 4: 测试 + 构建**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 全部 PASS。
Run: `cd chatbox/client; .\build_client.ps1`
Expected: 构建成功。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client
git commit -m "feat(client): resolve message avatars from cache with fallback"
```

---

### Task 5: `@` 词法解析

**Files:**
- Create: `chatbox/client/core/mention_query.h`
- Create: `chatbox/client/core/mention_query.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/tests/tests.pro`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct MentionToken {
      bool active = false;
      int start = -1;   // '@' 在文本中的下标
      QString query;    // '@' 之后到光标的文本
  };
  MentionToken mentionTokenAt(const QString& text, int cursor);
  ```

- [ ] **Step 1: 写失败测试**

- `mentionTokenDetectsAtStart`：`mentionTokenAt("@", 1)` → active、start=0、query 为空。
- `mentionTokenRejectsEmail`：`mentionTokenAt("a@b.com", 7)` → inactive。
- `mentionTokenStopsAtWhitespace`：`mentionTokenAt("@李硬 你好", 3)` → active query「李硬」；光标在空格后 → inactive。
- `mentionTokenNeedsWhitespaceBeforeAt`：`mentionTokenAt("你好@李", 4)` → inactive。
- `mentionTokenIgnoresCursorBeforeAt`：`mentionTokenAt("@李", 0)` → inactive。

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 编译失败（`mention_query.h` 不存在）。

- [ ] **Step 3: 实现**

规则：从 `cursor-1` 向左找最近的 `@`；`@` 前必须是文本开头或空白；`@` 与光标之间不能有空白；返回该段。

- [ ] **Step 4: 测试**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 5 个新测试 PASS。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/core/mention_query.* chatbox/client/chatbox.pro chatbox/client/tests/tests.pro chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): parse mention tokens in the composer"
```

---

### Task 6: `@` 弹层控件

**Files:**
- Create: `chatbox/client/ui/mention_popup.h`
- Create: `chatbox/client/ui/mention_popup.cpp`
- Create: `chatbox/client/ui/mention_item_delegate.h`
- Create: `chatbox/client/ui/mention_item_delegate.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/tests/tests.pro`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct MentionCandidate {
      quint64 user_id = 0;   // 0 = 所有人
      QString name;
      QString public_id;
      bool online = false;
      QPixmap avatar;
  };
  class MentionPopup : public QFrame {
      Q_OBJECT
  public:
      explicit MentionPopup(QWidget* parent = nullptr);
      void setCandidates(const QVector<MentionCandidate>& candidates, const QString& query);
      void showAt(const QPoint& global_pos);
      void moveSelection(int delta);
      bool hasSelection() const;
      MentionCandidate selectedCandidate() const;
      void activateSelected();
  signals:
      void chosen(const MentionCandidate& candidate);
      void dismissed();
  };
  ```

- [ ] **Step 1: 写失败测试**

- `mentionPopupFiltersByNameAndId`：候选「李硬/U00000007」「何填/U00000009」，query「何」只剩何填；query「00000007」只剩李硬。
- `mentionPopupKeyboardSelectionEmitsChosen`：向下两次 + `activateSelected()`，`QSignalSpy` 收到第二项。
- `mentionPopupAllEntryStaysFirst`：query 空时第一项 `user_id == 0`、`name == "所有人"`。

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 编译失败（类不存在）。

- [ ] **Step 3: 实现**

- 弹层：聊天页子控件 `QFrame`，`setWindowFlags(Qt::Widget)`、`Qt::NoFocus`、
  `WA_ShowWithoutActivating`，`QListWidget` + 自定义 delegate；宽度 280px，
  高度按行数夹到 6 行；`Qt::WA_DeleteOnClose` 不开（复用实例）。
- `showAt(global_pos)`：`move(parentWidget()->mapFromGlobal(global_pos))`，
  由 `MainWindow` 负责边界夹取；面板不抢焦点，输入框继续接收键盘。
- delegate 画 28px 圆形头像（有 `avatar` 用图，否则首字母）、昵称、`public_id`、在线圆点；选中行用 `theme::kSelectionBg`。
- `setCandidates` 按 `name.contains(query, Qt::CaseInsensitive) || public_id.contains(query, Qt::CaseInsensitive)` 过滤；「所有人」始终保留并置顶。
- 键盘选择移动 `currentRow` 并保证可见。

- [ ] **Step 4: 测试**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 3 个新测试 PASS。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/ui/mention_* chatbox/client/chatbox.pro chatbox/client/tests/tests.pro chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): add mention popup widget and delegate"
```

---

### Task 7: 接线 `@` 面板

**Files:**
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window.cpp`
- Modify: `chatbox/client/ui/main_window_chat.cpp`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: Task 5 `mentionTokenAt`、Task 6 `MentionPopup`。
- Produces:
  ```cpp
  void MainWindow::openMentionPopup(const QString& query);
  void MainWindow::closeMentionPopup();
  void MainWindow::insertMentionChoice(const MentionCandidate& candidate);
  QVector<MentionCandidate> MainWindow::mentionCandidates() const;
  bool MainWindow::eventFilter(QObject* watched, QEvent* event) override; // 扩展
  ```

- [ ] **Step 1: 写失败测试**

MainWindow 的弹层接线涉及网络与房间上下文，按仓库现有做法用源码契约测试：
- `mentionButtonOpensMentionPopup`：`main_window.cpp` 含
  `connect(mention_button_, &QPushButton::clicked, this, &MainWindow::insertMention)`，
  且 `insertMention` 段含 `openMentionPopup(`。
- `typingAtOpensMentionPopup`：`main_window.cpp` 的 `input_edit_` `textChanged`
  连接指向 `updateMentionPopup`，且该函数段含 `mentionTokenAt(` 与 `openMentionPopup(`。
- `privateChatClosesMentionPopup`：`selectPrivate` 段含 `closeMentionPopup()`。
- `atAllNotificationMatchesEveryone`：`main_window_net.cpp` 的 mention 判定含 `"@所有人"`。
- 行为验证由 Task 6 的弹层测试 + Task 13 的人工核对覆盖。

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 新测试失败。

- [ ] **Step 3: 实现**

- `insertMention()` 改为 `openMentionPopup(QString())`，不再 `QMenu`。
- `input_edit_` 的 `textChanged` 调用 `mentionTokenAt(input_edit_->toPlainText(), cursor position)`；active 且房间上下文 → `openMentionPopup(token.query)`；否则关闭。保存 `mention_token_start_`。
- 事件过滤器拦截 `KeyPress`：弹层可见时 `Up/Down/Enter/Tab/Esc` 交给弹层并 `return true`；其余继续原逻辑。
- `insertMentionChoice`：用 `QTextCursor` 选中 `[mention_token_start_, cursor)`，`insertText("@" + name + " ")`。
- 弹层位置：`input_edit_->cursorRect()` → `mapToGlobal`，优先放光标上方，空间不足放下方；`QApplication::screenAt` 做边界夹取。
- 候选：第一项「所有人」；其后遍历 `member_list_`，排除自己，头像用 `memberAvatarIcon(...).pixmap(28,28)`。
- `@所有人` 通知：`main_window_net.cpp` 中 `content.contains("@" + nickname_)` 改为 `content.contains("@" + nickname_) || content.contains("@所有人")`。
- 断线/切换会话/清空输入 → `closeMentionPopup()`。

- [ ] **Step 4: 测试 + 构建**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 全部 PASS。
Run: `cd chatbox/client; .\build_client.ps1`
Expected: 构建成功。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client
git commit -m "feat(client): open mention popup from the @ key and button"
```

---

### Task 8: 搜索跳转信号与 ChatView 高亮

**Files:**
- Modify: `chatbox/client/ui/message_search_dialog.h`
- Modify: `chatbox/client/ui/message_search_dialog.cpp`
- Modify: `chatbox/client/ui/chat_view.h`
- Modify: `chatbox/client/ui/chat_view.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  // MessageSearchDialog
  signals: void jumpRequested(quint64 message_id);
  // ChatView
  void scrollToMessage(quint64 message_id, bool flash);
  quint64 highlightedMessageId() const;
  ```

- [ ] **Step 1: 写失败测试**

- `messageSearchEmitsJumpOnClick`：`setResults` 两条结果，`QTest::mouseClick` 第一行，`QSignalSpy` 收到对应 `message_id`。
- `chatViewScrollsToMessageAndFlashes`：渲染 60 条消息、视口 240px，`scrollToMessage(5, true)` 后 `verticalScrollBar()->value() > 0` 且 `highlightedMessageId() == 5`；`QTRY_VERIFY` 2 秒后归零。

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 编译失败（新信号/方法不存在）。

- [ ] **Step 3: 实现**

- 对话框：结果项存 `Qt::UserRole` 的 message_id；`itemClicked`/`itemActivated` 都连到 `emitJump(item)`；`setResults` 后 `setEnabled(true)`。
- ChatView：
  - `scrollToMessage` 找到 `MessageRange`，用 `bubble_wrapper` 的 viewport rect 计算 `bar->setValue(bar->value() + rect.center().y() - viewport()->height()/2)`（越界自动夹取）。
  - `flash_id_` + `QTimer::singleShot(1800)` 清除并 `bubble_overlay_->update()`。
  - `paintRoundedBubbleCorners` 在画完边框后，若 `range.key.message_id == flash_id_`，用 `QColor(theme::kAccent)` alpha 40 填充圆角矩形，再画 2px accent 边框。

- [ ] **Step 4: 测试**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 全部 PASS。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client/ui/message_search_dialog.* chatbox/client/ui/chat_view.* chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): add search result jump and message flash"
```

---

### Task 9: MainWindow 定位模式与「回到最新」

**Files:**
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window_chat.cpp`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/resources/style.qss`
- Modify: `chatbox/client/resources/style-dark.qss`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: Task 8 `MessageSearchDialog::jumpRequested`、`ChatView::scrollToMessage`。
- Produces:
  ```cpp
  void MainWindow::jumpToMessage(quint64 message_id);
  void MainWindow::exitJumpMode(bool reload_latest);
  void MainWindow::positionJumpLatestButton();
  bool jump_mode_ = false;
  quint64 pending_jump_id_ = 0;
  quint64 jump_context_target_ = 0;
  int jump_context_kind_ = 0; // 1 room, 2 private
  QPushButton* jump_latest_button_ = nullptr;
  ```

- [ ] **Step 1: 写失败测试**

定位模式跨网络分页，行为测试在 Task 8 的 `ChatView` 层已覆盖；MainWindow 状态机按仓库做法用源码契约测试：
- `jumpModeSkipsLiveAppend`：`addIncomingMessage` 段含 `if (jump_mode_)` 且其后 `return`。
- `jumpModeShowsReturnButton`：`jumpToMessage` 段含 `jump_latest_button_->show()`。
- `jumpLatestRestoresBottom`：`jump_latest_button_` 的 `clicked` 连接指向 `exitJumpMode(true)`。
- `switchConversationExitsJumpMode`：`selectRoom` 与 `selectPrivate` 段都含 `exitJumpMode(false)`。
- `jumpRequestUsesTargetPlusOne`：`requestHistory` 调用参数含 `message_id + 1` 或 `jump_target + 1`。

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 新测试失败。

- [ ] **Step 3: 实现**

- 按钮：`new QPushButton("回到最新 ↓", chat_view_)`，objectName `JumpLatestButton`，`hide()`，`raise()`；`eventFilter` 监听 `chat_view_` 的 `Resize` 调用 `positionJumpLatestButton()`（右下角 16px 边距）。QSS 给圆角、阴影边框、accent 文字。
- `jumpToMessage`：
  - 目标已在 `message_store_`：`chat_view_->scrollToMessage(id, true)`，不进入定位模式。
  - 否则记录 `pending_jump_id_`、`jump_context_*`，`jump_mode_ = true`，显示按钮；在线 `requestHistory(kind, target, id + 1)`；离线读 `history_store_.before(context, target, id + 1, 50)` 后走同一段渲染。
- `HistoryResp`：`pending_jump_id_ != 0` 优先于 `loading_older_history_`：`replaceHistory(page)` → `renderCurrentConversation(kBottom)` → 找到目标则 `scrollToMessage(id, true)`，否则状态栏「消息不存在」并 `exitJumpMode(false)`。
- `exitJumpMode(true)`：在线 `requestHistory(kind, target, 0)`；离线 `history_store_.recent(..., 50)` → `replaceHistory` → `renderCurrentConversation(kBottom)`；`jump_mode_ = false`，隐藏按钮。
- `addIncomingMessage`：`if (jump_mode_) { message_store_.appendIncoming(message); return; }`（先落库，不渲染）。
- 私聊推送的 `sendMarkRead`：`jump_mode_` 为真时跳过。
- `onSendClicked`：`if (jump_mode_) exitJumpMode(true);` 再走原发送流程。
- `onDisconnected` / `enterOfflineMode` / `selectRoom` / `selectPrivate` / `resetContext`：`exitJumpMode(false)`。
- 搜索对话框：`jumpRequested` 连到 `jumpToMessage`；`selectRoom`/`selectPrivate` 里 `message_search_dialog_->close()`。

- [ ] **Step 4: 测试 + 构建**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 全部 PASS。
Run: `cd chatbox/client; .\build_client.ps1`
Expected: 构建成功。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client
git commit -m "feat(client): jump to searched message with return-to-latest mode"
```

---

### Task 10: presence 协议与帧构造

**Files:**
- Modify: `chatbox/common/protocol.h`
- Create: `chatbox/server/presence/presence_service.h`
- Create: `chatbox/server/presence/presence_service.cpp`
- Modify: `chatbox/CMakeLists.txt`
- Create: `chatbox/tests/test_presence.cpp`

**Interfaces:**
- Produces:
  ```cpp
  // common/protocol.h
  kCmdPresencePush = 0x00A4,
  // server/presence/presence_service.h
  struct PresenceEntry {
      uint64_t user_id = 0;
      bool online = false;
      uint64_t seq = 0;
  };
  Frame makePresencePushFrame(const std::vector<PresenceEntry>& entries);
  ```
- 帧体：`u16 count` + 每条 `{u64 user_id, u8 online, u64 seq}`；`header.flags = 0`（推送，非响应）。

- [ ] **Step 1: 写失败测试**

`chatbox/tests/test_presence.cpp`：
- `CHECK_EQ(frame.header.cmd, kCmdPresencePush)`
- `CHECK_EQ(frame.header.flags, 0)`
- 用 `ByteReader` 读回 count=2、第一条 `(7,1,100)`、第二条 `(9,0,101)`。
- 空列表 → count=0。

- [ ] **Step 2: 运行确认失败**

```powershell
& "D:\Qt\Tools\mingw810_64\bin\g++.exe" -std=c++17 -Wall -Wextra -I chatbox `
  chatbox\tests\test_presence.cpp chatbox\common\protocol.cpp chatbox\server\presence\presence_service.cpp `
  -o "$env:TEMP\test_presence.exe"
```
Expected: 编译失败（文件不存在）。

- [ ] **Step 3: 实现**

- `protocol.h` 加命令常量（放在 `kCmdProfileUpdatePush` 之后，注释说明避开群组线区间）。
- `presence_service.cpp` 用 `ByteWriter` 编码；`count` 转 `uint16_t` 前夹到 65535。
- CMake：把 `presence_service.cpp` 加进 `chatbox_server` 源；新增
  ```cmake
  add_executable(test_presence tests/test_presence.cpp server/presence/presence_service.cpp)
  target_link_libraries(test_presence PRIVATE chatbox_common)
  add_test(NAME presence COMMAND test_presence)
  ```

- [ ] **Step 4: 运行测试**

```powershell
& "D:\Qt\Tools\mingw810_64\bin\g++.exe" -std=c++17 -Wall -Wextra -I chatbox `
  chatbox\tests\test_presence.cpp chatbox\common\protocol.cpp chatbox\server\presence\presence_service.cpp `
  -o "$env:TEMP\test_presence.exe"
& "$env:TEMP\test_presence.exe"
```
Expected: 退出码 0，无 CHECK 失败。

- [ ] **Step 5: 提交**

```bash
git add chatbox/common/protocol.h chatbox/server/presence chatbox/tests/test_presence.cpp chatbox/CMakeLists.txt
git commit -m "feat(server): add presence push frame"
```

---

### Task 11: 服务端在线事件接线

**Files:**
- Modify: `chatbox/server/auth/session_manager.h`
- Modify: `chatbox/server/auth/session_manager.cpp`
- Modify: `chatbox/server/auth/auth_service.h`
- Modify: `chatbox/server/auth/auth_service.cpp`
- Modify: `chatbox/server/main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  struct SessionTarget { uint64_t connection_id = 0; int io_thread_id = -1; };
  bool SessionManager::bind(uint64_t user_id, const SessionBinding& binding,
                            SessionBinding& old_binding, uint64_t* presence_seq);
  bool SessionManager::unbind(uint64_t user_id, uint64_t connection_id,
                              uint64_t* presence_seq);
  uint64_t SessionManager::unbindByConnection(uint64_t connection_id,
                                              uint64_t* presence_seq);
  std::vector<SessionTarget> SessionManager::targets() const;
  // AuthResult
  uint64_t presence_seq = 0;
  ```
- `presence_seq` 由 `SessionManager` 内部 `std::atomic<uint64_t>` 在锁内自增；`unbind` 只在 connection_id 匹配时返回 true 并写 seq，否则返回 false、seq=0。

- [ ] **Step 1: 确认验证边界**

`SessionManager` 依赖 POSIX `connection.h`，Windows 本机无法编译该文件；本任务不伪造本地单测。可验证的只有：`main.cpp` / `SessionManager` 的代码评审、Task 10 的帧编码测试、以及 Task 12 的客户端消费测试。若 `chatroom-dev` 可达，Step 5 的 Linux 构建 + CTest 是本任务唯一的自动验证。

- [ ] **Step 2: 改 SessionManager**

- 加 `std::atomic<uint64_t> presence_seq_{0};`；`nextPresenceSeq()` 返回 `++presence_seq_`。
- `bind`：无论新增还是替换，都写 `*presence_seq = nextPresenceSeq()`（在锁内）。
- `unbind`：签名改 `bool`；只有 `connection_id` 匹配才 erase、自增 seq、返回 true。
- `unbindByConnection`：改为返回 `uint64_t`；只有真正 erase 时返回 user_id 并写 seq。
- `targets()`：锁内把 `bindings_` 的 connection_id/io_thread_id 复制出来。

- [ ] **Step 3: 改 AuthService**

- `handleLogin`/`handleResume` 的 `sessions_.bind(...)` 传 `&result.presence_seq`。
- `handleLogout` 的 `sessions_.unbind(...)` 传 `&result.presence_seq`，返回值忽略。
- 不改变响应内容。

- [ ] **Step 4: 改 main.cpp**

- 顶部匿名命名空间加：
  ```cpp
  void broadcastPresence(const chatbox::SessionManager& sessions,
                         const std::vector<chatbox::IoThread*>& io_threads,
                         const chatbox::PresenceEntry& entry,
                         uint64_t exclude_connection_id);
  ```
  实现：`sessions.targets()` → 跳过 exclude → 按 io_thread 分组 → `postMulticast(group, encodeFrame(makePresencePushFrame({entry})))`。
- `frame_handler` 捕获 `&sessions`。
- auth 分支：`result.bind_session && result.presence_seq != 0` → online；`result.clear_session && result.presence_seq != 0` → offline（`user_id` 用连接捕获值）。
- io 线程关闭回调：`const uint64_t user_id = sessions.unbindByConnection(connection_id, &seq);`，`user_id != 0` → offline。

- [ ] **Step 5: 能连上开发机时构建并跑 CTest**

```bash
ssh chatroom-dev "rm -rf ~/chatbox-build && cmake -S ~/chatbox-wip/chatbox -B ~/chatbox-build -DCMAKE_BUILD_TYPE=Release && cmake --build ~/chatbox-build -j4 && cd ~/chatbox-build && ctest --output-on-failure"
```
Expected: `100% tests passed`。
若 SSH 不可达：记录为未验证项，不得声称服务端已编译通过。

- [ ] **Step 6: 提交**

```bash
git add chatbox/server chatbox/common
git commit -m "feat(server): broadcast presence on login, logout and disconnect"
```

---

### Task 12: 客户端在线状态应用

**Files:**
- Create: `chatbox/client/core/presence_book.h`
- Create: `chatbox/client/core/presence_book.cpp`
- Modify: `chatbox/client/chatbox.pro`
- Modify: `chatbox/client/tests/tests.pro`
- Modify: `chatbox/client/ui/main_window.h`
- Modify: `chatbox/client/ui/main_window_net.cpp`
- Modify: `chatbox/client/ui/main_window_contacts.cpp`
- Modify: `chatbox/client/ui/main_window_profile.cpp`
- Test: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  ```cpp
  class PresenceBook {
  public:
      bool apply(quint64 user_id, bool online, quint64 seq); // false = 丢弃
      bool online(quint64 user_id, bool fallback) const;
      void clear();
      quint64 seq(quint64 user_id) const;
  };
  void MainWindow::applyPresenceUpdate(quint64 user_id, bool online, quint64 seq);
  ```

- [ ] **Step 1: 写失败测试**

- `presenceBookAppliesNewerSequence`：apply(7,true,10) → online true；apply(7,false,11) → false。
- `presenceBookIgnoresStaleSequence`：apply(7,true,10) 后 apply(7,false,9) 返回 false 且仍 online。
- `presenceBookClearsOnDisconnect`：clear 后 `seq(7)==0`。
- `presenceOverridesFriendListResponse`：源码检查 `kCmdFriendList` 段落用 `presence_book_.online(friend_id, online != 0)`。

- [ ] **Step 2: 运行确认失败**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 编译失败。

- [ ] **Step 3: 实现**

- `PresenceBook`：`QHash<quint64,quint64> seq_; QHash<quint64,bool> online_;`。
- 客户端 `kCmdPresencePush`：读 count，循环读 `(user_id, online, seq)`；自己跳过；`applyPresenceUpdate`。
- `applyPresenceUpdate`：`if (!presence_book_.apply(...)) return;`
  只有 `friend_views_.contains(user_id)` 时才更新 `friend_views_[user_id].online` 并
  `refreshFriendListView()`（未知用户只记入 book，绝不能凭空插入好友行）；
  若资料弹窗正打开该用户，更新 `profile_dialog_profile_.online` 并重新 `setProfile`。
- 列表响应：`kCmdFriendList` 用 `presence_book_.online(friend_id, online != 0)`；`kCmdPrivateConversations` 同样；`kCmdProfileGet` 同样。
- `onDisconnected`、`enterOfflineMode`、`clearSession`、`finishLogout`：`presence_book_.clear()`。
- 协议常量已在 Task 10 加好；客户端编译只用 `common/protocol.h`。

- [ ] **Step 4: 测试 + 构建**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: 全部 PASS。
Run: `cd chatbox/client; .\build_client.ps1`
Expected: 构建成功。

- [ ] **Step 5: 提交**

```bash
git add chatbox/client
git commit -m "feat(client): apply realtime presence updates"
```

---

### Task 13: 视觉核对、文档与最终验证

**Files:**
- Modify: `chatbox/client/README.md`（消息头像、`@` 面板、搜索跳转）
- Modify: `chatbox/client/screenshots/README.md`
- Add: `chatbox/client/screenshots/ui-chat-avatars.png`
- Add: `chatbox/client/screenshots/ui-mention-popup.png`
- Delete: `chatbox/client/screenshots/ui-message-actions.png`

- [ ] **Step 1: 用真实渲染器导出预览图**

在 `%TEMP%\chatbox-preview` 建一个临时 qmake 工程，链接最终版
`core/message_renderer.cpp`、`core/message_time.cpp`、`core/ui_theme.cpp`、
`ui/chat_view.cpp`、`ui/mention_popup.cpp`，导出：
- 两条消息（一条带缓存头像、一条首字母头像）的 `ui-chat-avatars.png`；
- 打开 `MentionPopup`（3 个候选）并 `grab()` 的 `ui-mention-popup.png`；
- 搜索跳转高亮的 `ui-search-highlight.png`（可选）。
用 `read` 工具查看 PNG，确认：头像圆形、昵称/ID 位置正确、文字垂直居中、气泡无破角、面板没有截断。

- [ ] **Step 2: 人工核对清单**

- 超长中文消息：气泡不超过约 72% 宽，头像仍完整。
- 单行 / 三行 / 含引用：文字上下间距差 ≤ 1px。
- `@` 面板：键盘上下、Enter、Esc、鼠标点击各走一遍。
- 搜索跳转：跳到最旧一条后右上角「回到最新」可用。
- 断开服务器：好友列表状态清空，重连后恢复。

- [ ] **Step 3: 全量测试**

Run: `cd chatbox/client/tests; .\run_client_tests.ps1`
Expected: `Totals: N passed, 0 failed`。
Run: `cd chatbox/client; .\build_client.ps1`
Expected: 构建成功。
服务端：执行 Task 11 Step 5；若 SSH 不可达，在最终报告里明确标注「服务端未编译验证」。

- [ ] **Step 4: 更新文档并提交**

- `README.md` 的功能清单补：消息头像与 ID、输入 `@` 提及、搜索跳转、实时在线状态；删除悬浮操作条相关描述。
- `screenshots/README.md` 用新图替换 `ui-message-actions.png` 行。

```bash
git add chatbox/client/README.md chatbox/client/screenshots
git commit -m "docs(client): document avatars, mentions and search jump"
```

- [ ] **Step 5: 分支收尾**

```bash
git status --short
git log --oneline main..HEAD
```
Expected: 工作区干净；提交序列与上面任务一一对应。

---

## 自检记录

- **Spec 覆盖**：2.1 → Task 2/3/4；2.2 → Task 5/6/7；2.3 → Task 2；2.4 → Task 8/9；2.5 → Task 1；2.6 → Task 10/11/12；验证 → Task 13。
- **类型一致性**：`AvatarSrcMap`（renderer）与 `MessageAvatars`（MainWindow）分工明确；`MentionToken`/`MentionCandidate` 在 Task 5/6 定义，Task 7 只消费；`PresenceEntry`/`SessionTarget` 在 Task 10/11 定义，Task 12 只消费协议常量。
- **已知未验证**：`chatroom-dev` 当前 SSH 超时，Task 11 的 Linux 构建 / CTest 可能在本次无法执行；届时按计划记录，不伪造结果。
