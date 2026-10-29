# Chat Message List Refresh — Design

**Date:** 2026-10-29
**Status:** approved in chat ("可以")
**Scope:** ChatBox 0.3.x client message list and server thumbnails

## Intent

Make the conversation view read like a familiar messenger instead of a
metadata-heavy table. The reference is the WeChat/Telegram pattern the user
showed: one centered time separator per run, the sender named once per run,
and media shown at its natural shape instead of a small stretched thumbnail.

## Goals

1. **Time**
   - Show one centered time separator when the conversation starts, when the
     calendar day changes, or when the gap from the previous message is at
     least 5 minutes.
   - Never print a timestamp under every bubble.
   - Keep "sending" and "failed / retry" status lines under the bubble.
2. **Names and grouping**
   - Incoming messages show `sender name + public ID` on the first message of a
     consecutive run.
   - Continuation messages from the same sender within 5 minutes hide the name
     and avatar and sit closer to the first message.
   - Own continuation messages also hide the repeated avatar.
   - Keep the avatar column width when it is hidden so bubbles stay aligned.
3. **Images and video**
   - Image and video file messages with a cached thumbnail render as a large
     preview instead of a coloured file bubble.
   - Preserve the source aspect ratio; cap the preview at 360x480 logical px.
   - Use a neutral surface for the preview frame, not the green own-bubble.
   - Video previews show the first frame, a play marker and duration; click
     opens the original file (image preview dialog for images, system player
     for video).
   - Files without a thumbnail keep the existing file card.
4. **Thumbnail quality**
   - Server thumbnails grow from 320x180 to a 640x640 bounding box.
   - Video MIME types are thumbnailed from their first frame.
5. **Persistence**
   - Local history keeps file width, height and duration so media layout
     survives a restart and offline reload.

## Non-goals

- Do not vendor or port Telegram Desktop/Web code. Those are GPLv3 complete
  clients, not a drop-in frontend; only interaction patterns are borrowed.
- No inline video playback in this pass; the original opens in the system
  player.
- No hover-only timestamps in this pass.
- No changes to the file size/quota rules, upload protocol, or MySQL schema.

## Decisions

| Decision | Value | Why |
| --- | --- | --- |
| Run gap | 300 seconds | Matches WeChat's 5-minute separator; one threshold controls both time and name grouping. |
| Preview cap | 360x480 | Noticeably larger than the old 220px, still fits a narrow window. |
| Thumbnail cap | 640x640 | Enough source pixels for the 360x480 preview and sharper text in screenshots. |
| Media surface | `theme::kSurfaceStrong` | Neutral frame in both light and dark themes; no green media blob. |
| Name frequency | first incoming message of a run | Avoids repeating names/IDs on every bubble. |
| Own avatar | first outgoing message of a run | Matches the grouping rule. |

## Acceptance

- Client test suite green, including new renderer/history tests.
- Server test suite green, including video thumbnail generation.
- Visual smoke: time separators centered, grouped avatars hidden, media
  previews at the correct aspect ratio.
- No protocol or database migration required beyond local SQLite additive
  columns.
