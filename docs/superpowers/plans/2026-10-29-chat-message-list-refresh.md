# Chat Message List Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render the conversation with centered time separators, grouped sender names/avatars, and natural-aspect image/video previews backed by sharper server thumbnails.

**Architecture:** Keep the existing `QTextBrowser` HTML renderer and nested-table bubble structure. Change the renderer's grouping/time decisions, add a media branch that reuses the existing thumbnail cache, enlarge and extend the Linux thumbnailer to video, and persist media dimensions in local SQLite.

**Tech Stack:** C++17, Qt 5.15 Widgets/Network/Sql, SQLite, Linux server with ffmpeg/ffprobe, CMake.

**Spec:** `docs/superpowers/specs/2026-10-29-chat-message-list-design.md`

## Global Constraints

- Client default C++17; Qt 5.15; no C++20 features.
- 4-space indentation, K&R braces, no tabs.
- Client never connects to MySQL; this change touches no SQL schema.
- Server stays Linux/CMake; thumbnail work must not block IO threads (it already runs in the file service path).
- Keep 2GB/file, 20GB/group, 5GB free-disk rules untouched.
- Preserve 0.2.5 text/history wire compatibility.
- All new behavior gets a test that fails first.
- Do not copy Telegram source; GPLv3 code is out of scope.

## Review Focus

- A file message with `thumb_available=true` but no cached thumbnail must fall back to the file card, not a broken image.
- A video from an older server has no thumbnail and must still be openable/downloadable.
- Messages with invalid timestamps must not crash the separator logic.
- A 5-minute boundary exactly at 300 seconds must produce a separator.
- Local history databases created before this change must gain the new columns without losing rows.

---

### Task 1: Centered time separators and message grouping

**Files:**
- Modify: `chatbox/client/core/message_renderer.h`
- Modify: `chatbox/client/core/message_renderer.cpp`
- Modify: `chatbox/client/ui/main_window_chat.cpp`
- Modify: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Produces:
  - `constexpr int kMessageGroupGapSeconds = 300;`
  - `bool shouldShowTimeSeparator(const ChatMessage* previous, const ChatMessage& current);`
  - `QString timeSeparatorLabel(const ChatMessage& message);`
  - `renderMessageFragment(..., bool include_time_separator, const QString& separator_label, bool show_sender_name = true, const AvatarSrcMap& avatar_srcs = {})`
- `shouldShowSenderName` returns true when the sender changes or a separator is due; false for the first message and recalled messages.

- [ ] **Step 1: Write failing tests**

In `test_client_core.cpp`, replace `rendererAddsDateSeparator` with:

```cpp
void TestClientCore::rendererAddsCenteredTimeSeparator() {
    const QDateTime first_local(QDate(2026, 10, 18), QTime(10, 0), Qt::LocalTime);
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
```

Replace `rendererKeepsAvatarOnEveryMessage` with:

```cpp
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
    const QString html = chatbox::client::renderConversationHtml(
        {first, second}, 99, nullptr, avatars);

    QCOMPARE(html.count(QStringLiteral("chatbox-avatar://11?v=2")), 1);
    QCOMPARE(html.count(QStringLiteral("OnlyOnceName")), 1);
    QVERIFY(html.contains(QStringLiteral("td width=\"36\"")));
}
```

Update the slot declarations and `rendererBubbleSpecUsesBorderAndExternalTimestamp`:
- Rename to `rendererBubbleSpecUsesBorderWithoutPerMessageTimestamp`.
- Assert the time appears before the bubble and no time appears after `</table></td></tr></table>`.
- Update `rendererFragmentMatchesFullRender` to pass the separator label and `true` for both messages.

- [ ] **Step 2: Run client tests and verify RED**

Run: `powershell -ExecutionPolicy Bypass -File .\tests\run_client_tests.ps1`
Expected: failures for the new separator/grouping assertions; old `rendererKeepsAvatar...` no longer compiles until the declaration is renamed.

- [ ] **Step 3: Implement**

In `message_renderer.cpp`:
- Add `kMessageGroupGapSeconds = 300`.
- `shouldShowTimeSeparator`: true when previous is null, either time invalid, local dates differ, delta < 0, or delta >= 300.
- `timeSeparatorLabel`: `HH:mm` for today, `MM-dd HH:mm` for another date (include year when different from the current year), `--` when invalid.
- `shouldShowSenderName`: false for null/recalled; true when sender differs; otherwise `shouldShowTimeSeparator`.
- Replace the date header div with the centered separator div.
- `messageMeta` returns only `发送中...` or `发送失败 + 重发`; return an empty string for `kSent` and skip the meta div when empty.
- `show_avatar = !message.recalled && show_sender_name`; when false and an avatar URL exists, emit `<td width="36"></td>` to keep alignment.
- In `renderConversationHtml`, compute `show_separator`, `label`, `show_name`, pass them to the fragment.
- In `main_window_chat.cpp::addIncomingMessage`, compute the same separator/label/name values and pass them to `renderMessageFragment`; keep the existing full re-render when the day changes.

- [ ] **Step 4: Run client tests and verify GREEN**

Run: `powershell -ExecutionPolicy Bypass -File .\tests\run_client_tests.ps1`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/core/message_renderer.h chatbox/client/core/message_renderer.cpp chatbox/client/ui/main_window_chat.cpp chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): group messages and show centered time separators"
```

---

### Task 2: Natural-aspect image and video previews

**Files:**
- Modify: `chatbox/client/core/message_renderer.cpp`
- Modify: `chatbox/client/ui/main_window_files.cpp`
- Modify: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- Consumes: `thumbnailCachePath(file_id)`, `saveThumbnail(file_id, bytes)`, `formatFileSize(size)`.
- Produces: media messages render `<img src="file:///...">` with explicit width/height and a `file-open:///message_id` link when the message has a server id.

- [ ] **Step 1: Write failing tests**

Add:

```cpp
void TestClientCore::rendererShowsLargeImagePreview() {
    const quint64 file_id = 990001;
    QImage image(320, 640, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    QVERIFY(buffer.open(QIODevice::WriteOnly));
    QVERIFY(image.save(&buffer, "JPG"));
    QVERIFY(chatbox::client::saveThumbnail(file_id, bytes));

    chatbox::client::ChatMessage message;
    message.message_id = 77;
    message.sender_id = 2;
    message.sender_name = "Bob";
    message.content = "[文件] photo.jpg";
    message.created_at_utc = chatbox::client::parseServerUtc("2026-10-18 12:00:00");
    message.type = chatbox::client::MessageType::kFile;
    message.file.file_id = file_id;
    message.file.name = "photo.jpg";
    message.file.size = 200000;
    message.file.mime = "image/jpeg";
    message.file.thumb_available = true;

    const QString html = chatbox::client::renderConversationHtml({message}, 1);
    QVERIFY(html.contains(QStringLiteral("file-open:///77")));
    QVERIFY(html.contains(QStringLiteral("width=\"240\" height=\"480\"")));
    QVERIFY(!html.contains(QStringLiteral("width=\"220\"")));
    QVERIFY(html.contains(QString::fromLatin1(chatbox::client::theme::kSurfaceStrong)));
    QFile::remove(chatbox::client::thumbnailCachePath(file_id));
}
```

Add `rendererShowsVideoPreviewWithPlayMarker` using a 320x180 thumbnail, `file.mime = "video/mp4"`, `duration_ms = 23000`; assert the HTML contains `▶ 00:23`, `width="320" height="180"`, and `file-open:///78`.

Add `rendererFallsBackToFileCardForVideoWithoutThumbnail`; assert no `<img`, contains the file name, and contains `file-open:///79`.

- [ ] **Step 2: Run tests and verify RED**

Run: `powershell -ExecutionPolicy Bypass -File .\tests\run_client_tests.ps1`
Expected: the new tests fail (220px image / no video preview).

- [ ] **Step 3: Implement**

In `message_renderer.cpp`:
- Add helpers:
  - `bool isMediaMessage(const ChatMessage&)`: file message, `file_id != 0`, `thumb_available`, MIME starts with `image/` or `video/`.
  - `QString formatDuration(quint32 ms)`: `mm:ss` (hours only when needed).
  - `QSize mediaDisplaySize(const QString& thumb_path)`: `QImageReader::size()`, fallback to `file.width/height`, then 320x180; scale with `Qt::KeepAspectRatio` into 360x480 but never upscale beyond the source.
- In the file branch, if `isMediaMessage` and `QFile::exists(thumbnailCachePath(file_id))`:
  - `preview = "<a href=\"file-open:///id\"><img src=\"file:///...\" width=\"W\" height=\"H\"></a>"`.
  - Caption: image `name · size`; video `▶ duration · name · size`.
  - Use `theme::kSurfaceStrong` as the bubble background and `cellpadding="4"` instead of 12.
  - If no thumbnail, keep the existing file card.
- In `messageBubbleColor`, return `theme::kSurfaceStrong` for media messages; otherwise keep own/other.
- In `main_window_files.cpp::requestMissingThumbnails`, accept `image/` and `video/`.

- [ ] **Step 4: Run tests and verify GREEN**

Run: `powershell -ExecutionPolicy Bypass -File .\tests\run_client_tests.ps1`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/core/message_renderer.cpp chatbox/client/ui/main_window_files.cpp chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): show large aspect-correct image and video previews"
```

---

### Task 3: Sharper server thumbnails and video first frames

**Files:**
- Modify: `chatbox/server/file/file_thumbnailer.cpp`
- Modify: `chatbox/server/file/file_thumbnailer.h`
- Modify: `chatbox/tests/test_file_thumbnail_logic.cpp`

**Interfaces:**
- `isThumbnailMime` accepts every `video/` MIME in addition to the existing image list.
- Thumbnail bounding box becomes 640x640; JPEG quality `-q:v 2`.

- [ ] **Step 1: Write failing tests**

Update `testMimeClassification`:

```cpp
CHECK_TRUE(chatbox::isThumbnailMime("video/mp4"));
CHECK_TRUE(chatbox::isThumbnailMime("video/webm"));
CHECK_TRUE(!chatbox::isThumbnailMime("application/pdf"));
```

Add `testLargeFitThumbnail`:

```cpp
chatbox::fitThumbnail(1920, 1080, 640, 640, width, height);
CHECK_EQ(width, 640u);
CHECK_EQ(height, 360u);
chatbox::fitThumbnail(1080, 1920, 640, 640, width, height);
CHECK_EQ(width, 360u);
CHECK_EQ(height, 640u);
```

Add `testVideoFixture` (skips when ffmpeg is absent):
- Create a 1-second 320x240 `mpeg4` AVI under `/tmp`.
- Run `FileThumbnailer::generate` with `video/x-msvideo`.
- Assert `ok`, `width == 320`, `height == 240`, `duration_ms > 0`, and the output starts with JPEG `FF D8`.

- [ ] **Step 2: Build and run server tests; verify RED**

On the Linux host:
```bash
cmake -S chatbox -B /tmp/chatbox-build
cmake --build /tmp/chatbox-build --target test_file_thumbnail_logic -j
/tmp/chatbox-build/test_file_thumbnail_logic
```
Expected: video classification and 640x640 fit assertions fail.

- [ ] **Step 3: Implement**

- `kThumbMaxWidth = 640`, `kThumbMaxHeight = 640`.
- `isThumbnailMime`: return true when `mime.rfind("video/", 0) == 0`.
- Change `-q:v 4` to `-q:v 2`.
- Update the header comment (video is no longer Phase 3).

- [ ] **Step 4: Rebuild and verify GREEN**

Same commands; expected `test_file_thumbnail_logic: all checks passed`.

- [ ] **Step 5: Commit**

```bash
git add chatbox/server/file/file_thumbnailer.cpp chatbox/server/file/file_thumbnailer.h chatbox/tests/test_file_thumbnail_logic.cpp
git commit -m "feat(server): generate 640px image and video thumbnails"
```

---

### Task 4: Persist media dimensions in local history

**Files:**
- Modify: `chatbox/client/core/history_store.cpp`
- Modify: `chatbox/client/tests/test_client_core.cpp`

**Interfaces:**
- `messages` gains `width`, `height`, `duration_ms` integer columns, default 0.
- `messageFromRow` restores them into `message.file.width/height/duration_ms`.

- [ ] **Step 1: Write failing test**

Add `historyStorePreservesFileMediaDimensions`:
- Open a `HistoryStore` in a `QTemporaryDir` for user 1.
- Save a file message with `file.width=1080`, `height=1920`, `duration_ms=23000`.
- Close, reopen, call `recent(kRoom, 3, 10)`, and assert the three values survive.

- [ ] **Step 2: Run tests and verify RED**

Run: `powershell -ExecutionPolicy Bypass -File .\tests\run_client_tests.ps1`
Expected: dimensions come back as 0.

- [ ] **Step 3: Implement**

- Bump `kSchemaVersion` to 3.
- Add `width`, `height`, `duration_ms` to `message_columns` with `INTEGER NOT NULL DEFAULT 0`.
- Extend `kSelectColumns`, `messageFromRow`, the INSERT column list, the upsert SET list, and the bind sequence.
- Keep the existing `thumb_path` behavior; `thumb_available` remains derived from a non-empty path.

- [ ] **Step 4: Run tests and verify GREEN**

Run: `powershell -ExecutionPolicy Bypass -File .\tests\run_client_tests.ps1`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add chatbox/client/core/history_store.cpp chatbox/client/tests/test_client_core.cpp
git commit -m "feat(client): persist file media dimensions in local history"
```

---

### Task 5: Full verification and release packaging

**Files:**
- Modify: `chatbox/client/main.cpp`
- Modify: `chatbox/client/resources/app.rc`
- Modify: `chatbox/client/README.md`

- [ ] **Step 1: Run the full client suite**

`powershell -ExecutionPolicy Bypass -File .\tests\run_client_tests.ps1`
Expected: all tests pass.

- [ ] **Step 2: Build and run the server suite**

On Linux, configure/build all tests and run `ctest --output-on-failure`.
Expected: all tests pass, including `file_thumbnail_logic`.

- [ ] **Step 3: Version bump**

Choose the next release version (0.4.0 for the UI refresh), update
`setApplicationVersion`, `FILEVERSION`, `PRODUCTVERSION`, the two version
strings in `app.rc`, and README paths/counts.

- [ ] **Step 4: Package and smoke test**

Run `package_client.ps1 -Version <version>`, verify FileVersion/ProductVersion,
launch with an isolated `--profile`, and check the window stays alive.

- [ ] **Step 5: Commit and tag**

Commit the release, tag `v<version>`, and push only after the user confirms the
release.

## Execution Notes

- Tasks 1–2 are client-only and can be verified on Windows.
- Task 3 requires the Linux build host; use a temporary build directory under
  `/tmp` and do not touch the production binary.
- Task 4 is independent of Task 3 and can run before it.
- Task 5 packaging/publishing is a side effect outside the worktree; ask before
  publishing.
