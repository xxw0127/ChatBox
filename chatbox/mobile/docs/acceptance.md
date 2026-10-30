# ChatBox Android 验收记录

日期：2026-10-29
设备：Xiaomi 24117RK2CC（HyperOS，Android 16 / API 36）
APK：`com.chatbox.mobile` 0.1.0，arm64-v8a，Debug 签名
测试账号：`PhoneTest`（真机） + `SenderTest`（TLS Python 客户端）

## 1. 环境与构建

| 项目 | 结果 |
| --- | --- |
| Qt | 6.8.3 Android arm64-v8a |
| minSdk / targetSdk | 28 / 35 |
| `build_android.ps1` | APK 构建、安装成功 |
| 前台 service | `com.chatbox.mobile:qt_service`，`isForeground=true`，通知 id 1000 |
| Keystore | token 密文在 `shared_prefs/chatbox_secure.xml`，明文不落盘；登出后只删除 token，保留 device_id |
| IPC | UI 通过 `QLocalSocket + 长度前缀 JSON` 连接 service，断线 1.5 秒重试 |

## 2. 场景结果（设计 §9.4）

| # | 场景 | 结果 | 证据 / 限制 |
| --- | --- | --- | --- |
| 1 | 桌面与手机同账号同时在线，互不踢下线 | 通过（手机 + 第二客户端） | 生产新服务端：手机 `PhoneTest` 与 TLS Python 第二设备同时在线，双方都未掉线；桌面同账号未重复测试，服务端机制相同 |
| 2 | 手机发私聊，桌面/手机实时收到并回复 | 通过（手机 + 第二客户端） | 第二设备发消息，手机收到 echo；手机回复，第二设备收到 echo。桌面端 209 QtTest PASS |
| 3 | 对方发消息，手机锁屏收到通知，点通知进入会话 | 通过 | 通知 id 2037 在锁屏状态存在；`am start ... --ei chatbox_kind 2 --el chatbox_target_id 37` 打开对应 ChatPage。MIUI 锁屏主题是否展示通知卡片取决于系统主题/锁屏通知开关 |
| 4 | 手机已读，桌面未读清除；反之亦然 | 服务端与单元测试通过 | 服务端 `test_multidevice_read_echo.py` PASS；桌面 read-state handler 209 QtTest PASS；手机 ChatPage 进入即 `mark_read`，本地未读清零并取消通知。双真机已读未单独复测 |
| 5 | 手机离线 30 分钟后重连，补消息顺序正确、无重复 | 通过 | 生产：手机关机状态下，第二设备连续发 40 条 1988 字节消息（约 80 KB）；手机重连后 sync 一次补齐，cursor 69，40 条全部落库、无重复（覆盖旧 64 KB 限制回归） |
| 6 | 桌面注销手机会话，手机提示并回登录页 | 服务端通过；真机待复测 | 服务端 revoke 返回 1002 并由会话关闭；手机收到终止错误会清 token 回登录页（`terminalErrorClearsToken` PASS）。桌面设备管理页已实现 |
| 7 | 服务端重启，手机自动重连并恢复状态 | 通过 | 生产 12:51 重启服务，手机 1.5 秒内 `reconnecting → authenticating → online`，无需重新登录 |
| 8 | 旧 0.4.0 桌面登录，不踢新手机，双方收发正常 | 服务端通过；真机待复测 | `test_multidevice_auth.py` 的 legacy 场景 PASS；旧桌面仍用 legacy 单会话规则，不会踢新手机 |
| 9 | 手机杀 service / 厂商限制后的恢复 | 已知限制 | HyperOS 划掉最近任务会杀 UID，`START_STICKY` 30 秒内不拉起；通知在用户重新打开 App 或系统重启 service 前停止。见 `f0-device-matrix.md` |
| 10 | 手机重启后（允许自启动）自动恢复连接 | 已知限制 | MIUI 默认拦截 `BOOT_COMPLETED`；需用户手动允许“自启动”。允许后由 `ChatBoxBootReceiver` 拉起前台 service |

## 3. 生产部署记录（2026-10-30 12:50 UTC+8）

- 备份：`/home/ubuntu/backups/chatbox-20261030-125012-pre-multidevice.sql`（54 KB）
- 旧二进制：`/home/ubuntu/backups/chatbox_server-20261030-125012-pre-multidevice`
- 迁移：`deploy/migrate_20261029_multidevice.sql`，`sessions` 已具备 `session_id/device_id/last_seen_at`，64 条存量会话全部回填
- 新二进制：`/home/ubuntu/chatbox/build/chatbox_server`（1130648 bytes，含 multi-device）
- systemd `chatbox` active，监听 `127.0.0.1:9100`；重启后手机 1.5 秒内 RESUME 成功
- 回滚：停服务 → 恢复备份二进制 → 启动；schema 为增量，旧二进制可直接运行

## 4. 已执行的自动化回归

```text
服务端（VPS 新代码）
  CTest                         18/18 PASS
  test_multidevice_auth.py      ALL MULTI-DEVICE AUTH CHECKS PASSED
  test_multidevice_fanout.py    ALL MULTI-DEVICE FANOUT CHECKS PASSED
  test_multidevice_read_echo.py ALL READ/ECHO CHECKS PASSED
  test_multidevice_sync.py      ALL SYNC CHECKS PASSED
  test_multidevice_caps.py      ALL CAPABILITY CHECKS PASSED

桌面客户端
  client_tests.exe              209 passed, 0 failed

clientcore（Qt 6）
  clientcore_tests              47 checks passed（SessionClient/MobileStore/SecureStore/IPC/ChatService）
```

## 5. 真机已确认的交互

- 登录成功后进入会话列表；会话按最后消息时间显示标题、摘要、未读数。
- 点击会话进入聊天页；输入框获得焦点、键盘弹出；回车发送。
- 对端实时收到手机消息；对端发来的消息在聊天页实时出现。
- 聊天页打开时，同一会话的新消息不弹通知；返回列表后恢复通知。
- 进入聊天页会取消该会话的通知并清本地未读。
- 强制停止后重新打开，Keystore 中的 token 触发 RESUME，直接进入会话列表。
- 设置页显示账号、连接状态、通知/电池优化入口；退出登录清 token 并回登录页。
- 通知栏点击（或带 extras 启动 `ChatBoxActivity`）可定位到对应会话。
- 全新安装/清除应用数据后登录：会话摘要先出现，打开会话自动拉取最近 50 条 history，不再显示空聊天页。
- 账号切换：A 退出后登录 B，B 看不到 A 的本地消息/未读/outbox；切回 A 时重新从服务端拉取历史。

## 6. 剩余复测清单

1. 桌面 0.4.x 与手机同时登录**同一账号**，确认双方都不被踢；互发消息（生产已支持，本次用第二客户端代测）。
2. 桌面读过消息后，手机未读清零；手机读过消息后，桌面未读清零。
3. 桌面“设备管理”注销手机；手机提示终止错误并回登录页。
4. 旧 0.4.0 桌面登录后，手机仍在线且能收发。
5. 飞行模式 30 分钟 + 弱网重连（本次已用 40 条大消息验证 80 KB 补同步）。

## 7. 已知限制

- 生产服务端已于 2026-10-30 12:51 部署多设备版本；上面 5 项是仍需人工用桌面客户端完成的交叉验证。
- 锁屏通知是否显示为卡片受 MIUI 主题和“锁屏通知”开关影响；通知本身已成功投递（dumpsys 可见），点击通知可进入会话。
- 无厂商推送：App 被系统彻底杀死后不会收到通知，直到用户重新打开或系统恢复 service。
- 本阶段只支持 1:1 文本；文件、群管理、搜索、资料页、应用内更新未实现。
