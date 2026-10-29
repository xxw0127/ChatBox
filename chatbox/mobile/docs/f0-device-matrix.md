# F0 后台行为矩阵

测试目标：确认 QtService + 前台服务 + 通知在真实设备上的行为边界。结果只代表下表设备，不外推到所有 ROM。

## 设备

| 项 | 值 |
| --- | --- |
| 机型 | Xiaomi 24117RK2CC |
| 系统 | Android 16（API 36，HyperOS） |
| 构建 | Debug APK，`com.chatbox.mobile` |
| 日期 | 2026-10-29 |

## 结果

| 场景 | 结果 | 证据/备注 |
| --- | --- | --- |
| APK 安装 | 通过 | 需开发者选项“USB 安装”，首次安装手机弹确认 |
| service 进程 | 通过 | `processName=com.chatbox.mobile:qt_service` |
| 前台服务 | 通过 | `isForeground=true`，`foregroundId=1000`，`types=0x00000200`（remoteMessaging） |
| 常驻通知 | 通过 | channel `chatbox_service`，id 1000 |
| 测试消息通知 | 通过 | channel `chatbox_messages`，id 1001；需授予 `POST_NOTIFICATIONS` |
| 锁屏 10 秒 | 通过 | 熄屏 10 秒后 service 仍 `isForeground=true` |
| 系统 `am kill` | 通过 | 前台服务进程未被杀死，service 仍在 |
| 从最近任务划掉 | **未通过** | 小米 HyperOS 划掉后整个 app 进程被杀；等 30 秒 `START_STICKY` 未拉起。通知 1000/1001 一并消失 |
| 重启手机 | **未通过** | receiver 已注册、`RECEIVE_BOOT_COMPLETED` 已授权，但解锁后 service 未启动；小米 HyperOS 默认关闭第三方 App 自启动，需要在系统里手动允许 |
| 省电模式 | 待测 | 开启省电模式后重复锁屏/通知 |
| 强行停止 app | 预期不能收 | Android 不允许 force-stop 后自动重启；需用户重新打开 |

## 手工步骤

划掉任务：

1. 打开 ChatBox，保持在前台。
2. 进入最近任务，把 ChatBox 卡片向上划掉。
3. 执行 `adb shell dumpsys activity services com.chatbox.mobile`，确认 `ChatBoxService` 仍存在且 `isForeground=true`。
4. 观察通知栏 id 1000 是否仍在。

重启手机：

1. `adb reboot` 或手动重启。
2. 不打开 ChatBox。
3. 等待 30 秒，执行 `adb shell dumpsys activity services com.chatbox.mobile`。
4. 如果 service 存在，记录为“开机自启成功”；如果不存在，记录 ROM/Android 版本限制，不视为阻塞。

小米开启自启动：

1. 设置 → 应用设置 → 应用管理 → ChatBox → **自启动**，打开。
2. 或在安全中心 → 应用管理 → 权限 → 自启动，允许 ChatBox。
3. 重新重启验证；仍不启动则记录该 ROM 限制。

测试通知（不依赖 UI）：

```powershell
adb shell am start-foreground-service -n com.chatbox.mobile/.ChatBoxService
adb shell dumpsys notification --noredact | Select-String "com.chatbox.mobile"
```

## 已知限制

- 自建服务端没有系统级推送；service 被系统杀死后，通知会中断，直到 service 重启或用户打开 app。
- **小米 HyperOS（本机 Android 16）从最近任务划掉 ChatBox 后，整个 app UID 被杀，`START_STICKY` 不会在 30 秒内拉起 service。** 这是 OEM 行为；同一套代码在其它 ROM 上可能不同。要获得“划掉也能收”的体验，需要厂商推送或系统白名单，不能靠前台服务保证。
- **小米 HyperOS 默认拦截第三方 App 开机自启。** `ChatBoxBootReceiver` 已注册、`RECEIVE_BOOT_COMPLETED` 已授权，但解锁后 service 未启动；需要在系统设置里给 ChatBox 打开“自启动”。这属于 OEM 白名单，不是代码缺陷。
- Android 15+ 对 `BOOT_COMPLETED` 启动前台服务有限制；开机自启是尽力而为。
- 国产 ROM 的“自启动/后台运行/电池优化”白名单只能引导用户手动设置。
- 本矩阵只有一台小米 Android 16 设备；Android 8/10/13/14 和华为/OPPO/vivo 需要后续补测。
