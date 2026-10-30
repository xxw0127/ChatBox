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
| 1 | 桌面与手机同账号同时在线，互不踢下线 | 服务端通过；真机待部署 | VPS 新服务端 `test_multidevice_auth.py` PASS；生产仍是旧单会话服务端，未做双端真机测试 |
| 2 | 手机发私聊，桌面/手机实时收到并回复 | 部分通过 | 手机 ↔ TLS Python 客户端双向实时收发 PASS；桌面端 209 QtTest PASS。桌面+手机同账号需先部署新服务端 |
| 3 | 对方发消息，手机锁屏收到通知，点通知进入会话 | 通过 | 通知 id 2037 在锁屏状态存在；`am start ... --ei chatbox_kind 2 --el chatbox_target_id 37` 打开对应 ChatPage。MIUI 锁屏主题是否展示通知卡片取决于系统主题/锁屏通知开关 |
| 4 | 手机已读，桌面未读清除；反之亦然 | 服务端与单元测试通过；真机待部署 | 服务端 `test_multidevice_read_echo.py` PASS；桌面 read-state handler 209 QtTest PASS；手机 ChatPage 进入即 `mark_read`，本地未读清零并取消通知 |
| 5 | 手机离线 30 分钟后重连，补消息顺序正确、无重复 | 服务端与单元测试通过；真机待部署 | 服务端 `test_multidevice_sync.py` PASS（60 条消息 50+10，有序无重复）；ChatService `syncPullAppliesAfterCursor` PASS |
| 6 | 桌面注销手机会话，手机提示并回登录页 | 服务端通过；真机待部署 | 服务端 revoke 返回 1002 并由会话关闭；手机收到终止错误会清 token 回登录页（`terminalErrorClearsToken` PASS）。桌面设备管理页已实现 |
| 7 | 服务端重启，手机自动重连并恢复状态 | 部分通过 | 心跳 30 秒保活；生产旧服务端空闲断线后重连验证了重连路径，旧服务端 RESUME 语义导致一次 1002（已按终止错误回登录）。新服务端的 RESUME/多会话由服务端测试覆盖 |
| 8 | 旧 0.4.0 桌面登录，不踢新手机，双方收发正常 | 服务端通过；真机待部署 | `test_multidevice_auth.py` 的 legacy 场景 PASS；旧桌面仍用 legacy 单会话规则，不会踢新手机 |
| 9 | 手机杀 service / 厂商限制后的恢复 | 已知限制 | HyperOS 划掉最近任务会杀 UID，`START_STICKY` 30 秒内不拉起；通知在用户重新打开 App 或系统重启 service 前停止。见 `f0-device-matrix.md` |
| 10 | 手机重启后（允许自启动）自动恢复连接 | 已知限制 | MIUI 默认拦截 `BOOT_COMPLETED`；需用户手动允许“自启动”。允许后由 `ChatBoxBootReceiver` 拉起前台 service |

## 3. 已执行的自动化回归

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

## 4. 真机已确认的交互

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

## 5. 部署新服务端后的复测清单

1. `sudo mysql < chatbox/deploy/migrate_20261029_multidevice.sql`，重启新服务端（见 `chatbox/docs/multidevice-deployment.md`）。
2. 桌面 0.4.x 与手机同时登录同一账号，确认双方都不被踢；互发消息。
3. 桌面读过消息后，手机未读清零；手机读过消息后，桌面未读清零。
4. 手机飞行模式 30 分钟，期间桌面发 60 条消息；恢复网络后按顺序补齐、无重复。
5. 桌面“设备管理”注销手机；手机提示终止错误并回登录页。
6. 重启服务端，手机 30 秒内自动重连并回到在线。
7. 旧 0.4.0 桌面登录后，手机仍在线且能收发。

## 6. 已知限制

- 生产服务端在本次验收时仍是旧单会话版本；第 1/4/5/6/8 项的真机部分必须在部署新服务端后复测。
- 锁屏通知是否显示为卡片受 MIUI 主题和“锁屏通知”开关影响；通知本身已成功投递（dumpsys 可见），点击通知可进入会话。
- 无厂商推送：App 被系统彻底杀死后不会收到通知，直到用户重新打开或系统恢复 service。
- 本阶段只支持 1:1 文本；文件、群管理、搜索、资料页、应用内更新未实现。
