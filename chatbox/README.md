# ChatBox 服务端：TCP + MySQL + 大厅/广播

Linux C++17 服务端：epoll ET、Acceptor + 多 IO 线程、二进制帧、心跳超时、PING/PONG、MySQL 8.4 账号体系、房间与群聊广播。

## 依赖

```bash
sudo apt-get install -y cmake g++ libmysqlclient-dev libargon2-dev libssl-dev
```

## 文件传输（Phase 2）

已有数据库升级到文件版本时执行：

```bash
sudo mysql < deploy/migrate_20261029_files.sql
sudo ./tools/install_files_runtime.sh /home/ubuntu/chatbox/files
```

文件存放在 `/home/ubuntu/chatbox/files`（默认 0700），图片/GIF 缩略图依赖
`ffmpeg`/`ffprobe`；缺少时服务仍启动，只是不生成缩略图。启动参数可加
`--files-dir`、`--ffmpeg-path`、`--ffprobe-path`。完整部署、备份、回滚和配额说明见
[`docs/files-deployment.md`](docs/files-deployment.md)。

## 初始化 MySQL 8.4

```bash
sudo mysql < deploy/schema.sql
```

已有数据库升级到私聊未读版本时，再执行一次旧消息已读初始化：

```bash
sudo mysql < deploy/migrate_unread.sql
```

已有数据库升级到引用/撤回版本时：

```bash
sudo mysql < deploy/migrate_message_features.sql
```

已有数据库升级到好友系统时：

```bash
sudo mysql < deploy/migrate_friends.sql
```

本次服务端好友备注版本需要再次执行该脚本；脚本使用
`CREATE TABLE IF NOT EXISTS`，重复执行不会删除已有好友关系。

已有数据库升级到用户资料与头像版本时：

```bash
sudo mysql < deploy/migrate_profiles.sql
```

脚本按列是否存在逐项判断，可重复执行；它为 `users` 增加
`signature VARCHAR(128)`、`avatar MEDIUMBLOB`、`avatar_version INT UNSIGNED`，
已有用户默认空签名、无头像、版本 0。生产库已于 2026-10-19 执行并校验。

已有数据库升级到多设备会话版本时：

```bash
sudo mysql < deploy/migrate_20261029_multidevice.sql
```

脚本为 `sessions` 增加 `session_id`、设备信息、`caps`、`last_seen_at`，
回填存量会话并增加索引；可重复执行，不删除已有列。完整上线/回滚步骤见
[`docs/multidevice-deployment.md`](docs/multidevice-deployment.md)。

创建应用账号（密码换成自己的强密码）：

```sql
CREATE USER 'chatbox'@'127.0.0.1' IDENTIFIED BY 'your-strong-password';
CREATE USER 'chatbox'@'localhost' IDENTIFIED BY 'your-strong-password';
GRANT SELECT, INSERT, UPDATE, DELETE ON chatbox.* TO 'chatbox'@'127.0.0.1';
GRANT SELECT, INSERT, UPDATE, DELETE ON chatbox.* TO 'chatbox'@'localhost';
FLUSH PRIVILEGES;
```

配置密码文件：

```bash
cp conf/chatbox.env.example conf/chatbox.env
chmod 600 conf/chatbox.env
# 编辑 conf/chatbox.env，写入 CHATBOX_DB_PASSWORD=...
```

`tools/run_server.sh` 会自动加载该文件并导出环境变量。

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
(cd build && ctest --output-on-failure)
```

## 运行

TLS 代理部署（nginx 终结 TLS、服务端只监听回环）：

```bash
chmod +x tools/run_server.sh
./tools/run_server.sh --listen 127.0.0.1 --port 9100 --io-threads 4 \
    --worker-threads 4 --max-connections 500 --max-conn-per-ip 50 \
    --proxy-protocol --idle-timeout 90
```

局域网直连调试（不经过 nginx）时可使用：

```bash
./tools/run_server.sh --listen 0.0.0.0 --port 9100 --max-conn-per-ip 20
```

日志默认写入 `logs/chatbox.log`。停止：

```bash
kill -TERM <pid>
```

## 加固与 systemd

默认限制：

- 登录失败：IP + 用户名 5 次/分钟，超限返回 `1005`
- 发消息：每连接 10 条/秒，突发 20，超限返回 `1005`
- 全局连接：500
- 单 IP 连接：50；服务端在 PROXY v2 解析后按真实 IP 计数，经过 nginx/FRP 时也必须保留 `--max-conn-per-ip 50`，不能改回 `0`

安装 systemd 与日志轮转：

```bash
sudo cp deploy/chatbox.service /etc/systemd/system/chatbox.service
sudo cp deploy/chatbox.logrotate /etc/logrotate.d/chatbox
sudo systemctl daemon-reload
sudo systemctl enable --now chatbox
sudo systemctl status chatbox --no-pager
```

日志轮转通过 `SIGUSR1` 让进程重新打开日志文件，配置位于 `deploy/chatbox.logrotate`。

## 监控

服务端每 10 秒输出一行 `stats` 日志，可直接用 `tail -f` 或日志采集器观察：

```text
INFO stats conn_total=1 conn_active=0 auth_ok=1 auth_fail=0 msg_in=2
     frames_out=2 protocol_errors=0 rate_limited=0 db_errors=1
     bcast_drop=0 rss_kb=17976 fds=24
```

字段：

- `conn_total` / `conn_active`：累计接受连接数 / 当前活跃连接数
- `auth_ok` / `auth_fail`：登录或 RESUME 成功 / 登录失败次数
- `msg_in`：进入业务处理的应用请求数（不含 PING）
- `frames_out`：成功入发送队列的响应/推送帧数（广播按每个接收者计数）
- `protocol_errors`：帧解析或 PROXY 头错误
- `rate_limited`：登录限流和消息令牌桶拒绝次数
- `db_errors`：MySQL 连接丢失/重连失败和 SQL 执行错误（重复键除外）
- `bcast_drop`：广播时目标连接写缓冲满被断开次数
- `rss_kb` / `fds`：进程 RSS（KiB）和打开的文件描述符数

MySQL 连接在每个业务请求前通过 `mysql_ping` 检查；连接丢失会自动关闭并重连，worker 不会因为 MySQL 重启而永久失效。

## TLS / nginx

拓扑：

```text
局域网: Qt 客户端 --TLS 9443--> nginx stream --PROXY + 明文--> 127.0.0.1:9100
公网:   Qt 客户端 --TLS frp-oil.com:15318--> SakuraFrp 节点 --TCP--> frpc
        --PROXY v2--> nginx 127.0.0.1:9444 --TLS 终结 --PROXY + 明文--> 127.0.0.1:9100
```

证书与 nginx：

```bash
sudo deploy/gen_certs.sh
sudo cp deploy/nginx-chat-stream.conf /etc/nginx/stream.d/chat.conf
sudo nginx -t && sudo systemctl reload nginx
```

- 服务端证书 SAN：`IP:192.168.2.240`、`DNS:chatbox.local`、`DNS:frp-oil.com`
- 服务器 LAN 地址固定为 `192.168.2.240/24`（NetworkManager 静态配置），不再依赖 DHCP 租约
- CA：`/etc/chatbox/certs/ca.crt`，客户端副本在 `client/certs/ca.crt`
- `deploy/gen_certs.sh` 只在 CA 缺失时生成 CA；正常运行只重签服务器证书并保留原 CA。**不要删除或重新生成 `ca.crt`/`ca.key`**，否则所有现有客户端都会拒绝连接；只换服务器证书时应复用原 CA，并备份证书目录
- nginx 开启 `proxy_protocol on`；9443 为局域网监听，9444 仅监听 `127.0.0.1` 供 frpc 使用
- 服务端必须使用 `--proxy-protocol`，否则无法读取真实 IP；公开入口使用 PROXY v2
- 客户端默认 `9443 + TLS`，并校验 CA 与 IP/域名 SAN；公网入口为 `frp-oil.com:15318`

### 固定服务器地址

服务器通过 NetworkManager 使用静态地址（连接名以现场为准）：

```bash
sudo nmcli connection modify <连接名> \
    ipv4.method manual \
    ipv4.addresses 192.168.2.240/24 \
    ipv4.gateway 192.168.2.1 \
    ipv4.dns "192.168.2.1 192.168.1.1" \
    ipv4.dad-timeout 2000
sudo nmcli connection up <连接名>
```

改地址后必须重签服务器证书（复用原 CA），并同步客户端的 `server/host`、`session/host` 和 SSH 别名。

## 公网部署与备份

公网入口由本机 `frpc` 连接 SakuraFrp 免费 TCP 隧道，再由 nginx `127.0.0.1:9444` 终结 TLS。客户端使用现有 portable ZIP，不重新打包；首次连接填写 `frp-oil.com:15318` 并勾选 TLS，之后 QSettings 会记住。局域网 `192.168.2.240:9443` 保持可用。面向朋友的说明见 `docs/public-access.md`。

常用检查：

```bash
systemctl status chatbox nginx mysql sakurafrp
journalctl -u sakurafrp -n 100 --no-pager
ss -ltnp | grep -E ':(9443|9444|9100|3306)\b'
```

每日备份由 `chatbox-backup.timer` 在 03:15 触发（睡眠时 `Persistent=true` 会在唤醒后补跑），保留 7 天，文件在 `/var/backups/chatbox/`，权限 `600 root:root`。备份使用专用账号 `chatbox_backup@localhost`，凭据在 `/etc/chatbox/backup.cnf`（`600 root:root`）。恢复演练：

```bash
latest=$(ls -1t /var/backups/chatbox/chatbox-*.sql.gz | head -1)
sudo mysql -e 'DROP DATABASE IF EXISTS chatbox_restore_test; CREATE DATABASE chatbox_restore_test;'
gunzip -c "$latest" | sudo mysql chatbox_restore_test
sudo mysql -e 'DROP DATABASE chatbox_restore_test;'
```

SakuraFrp 免费额度约 5 GiB/月、10 Mbps；额度耗尽、节点维护或本机断网时公网不可用，局域网不受影响。**UFW 未启用，合盖/睡眠和 Wi-Fi 省电未修改**；笔记本合盖或睡眠时公网会断开，唤醒后 frpc 自动重连，因此不承诺 24 小时在线。不要分享访问 token、隧道 ID 或 CA 私钥。

撤销公网路径（不影响局域网）：

```bash
sudo systemctl disable --now sakurafrp
# 从 /etc/nginx/stream.d/chat.conf 删除或注释 127.0.0.1:9444 的 server 块
sudo nginx -t && sudo systemctl reload nginx
```

撤销时不要删除 CA 或重签证书；`192.168.2.240:9443` 继续使用原证书。

## 协议

固定 14 字节大端帧头：

| 偏移 | 长度 | 字段 |
|---|---|---|
| 0 | 2 | magic = 0x4348 |
| 2 | 1 | version = 1 |
| 3 | 1 | flags |
| 4 | 2 | cmd |
| 6 | 4 | seq |
| 10 | 4 | body_len |

命令：

| cmd | 名称 | 请求体 | 成功响应体 |
|---|---|---|---|
| 0x0001 | REGISTER | username, password, nickname | user_id (u64) |
| 0x0002 | LOGIN | username, password | user_id (u64), nickname, token |
| 0x0003 | LOGOUT | 空 | 空 |
| 0x0004 | RESUME | token | user_id (u64), nickname |
| 0x0010 | ROOM_LIST | 空 | count (u16) + [room_id u32, name, max u32, online u32] |
| 0x0011 | JOIN_ROOM | room_id (u32) | 空 |
| 0x0012 | LEAVE_ROOM | room_id (u32) | 空 |
| 0x0013 | ROOM_MEMBERS | room_id (u32) | room_id u32, count u16, [user_id u64, nickname] |
| 0x0020 | CHAT_SEND | room_id u32, client_msg_id, content, [reply_to_id u64, reply_sender_name, reply_content] | message_id (u64), created_at, [reply_to_id u64, reply_sender_name, reply_content] |
| 0x0021 | CHAT_SEND_RESP | — | 服务端响应 |
| 0x0022 | CHAT_PUSH | — | room_id u32, message_id u64, sender_id u64, nickname, content, created_at, [reply_to_id u64, reply_sender_name, reply_content] |
| 0x0030 | PRIVATE_SEND | to_user_id u64, client_msg_id, content, [reply_to_id u64, reply_sender_name, reply_content] | message_id (u64), created_at, [reply_to_id u64, reply_sender_name, reply_content] |
| 0x0031 | PRIVATE_SEND_RESP | — | 服务端响应 |
| 0x0032 | PRIVATE_PUSH | — | from_user_id u64, message_id u64, nickname, content, created_at, [reply_to_id u64, reply_sender_name, reply_content] |
| 0x0033 | PRIVATE_CONVERSATIONS | 空 | count u16 + [peer_id u64, nickname, last_msg_id u64, last_content, last_time, online u8, unread u32] |
| 0x0034 | PRIVATE_MARK_READ | peer_id u64, message_id u64 | 空 |
| 0x0040 | PING | 任意 | 原样返回，flags 置响应位 |
| 0x0050 | HISTORY | kind u8 (1房间/2私聊), target_id u64, before_id u64, limit u16 | 见下方 HISTORY_RESP |
| 0x0051 | HISTORY_RESP | — | kind u8, target_id u64, count u16, has_more u8, next_before_id u64 + 消息记录 |
| 0x0060 | USER_SEARCH | keyword（用户名、昵称或用户ID） | count u16 + [user_id u64, username, nickname, online u8] |
| 0x0070 | MESSAGE_RECALL | kind u8 (1房间/2私聊), target_id u64, message_id u64 | kind u8, target_id u64, message_id u64 |
| 0x0080 | MESSAGE_SEARCH | kind u8, target_id u64, keyword, limit u16 | count u16 + [message_id u64, sender_id u64, nickname, content, created_at, recalled u8] |
| 0x0090 | FRIEND_REQUEST | target_id u64 | 空 |
| 0x0091 | FRIEND_RESPOND | requester_id u64, accept u8 | 空 |
| 0x0092 | FRIEND_LIST | 空 | count u16 + [user_id u64, username, nickname, online u8, remark, avatar_version u32] |
| 0x0093 | FRIEND_REQUESTS | 空 | count u16 + [requester_id u64, username, nickname, created_at] |
| 0x0094 | FRIEND_REMOVE | friend_id u64 | 空 |
| 0x0095 | FRIEND_REQUEST_PUSH | — | from_id u64, username, nickname |
| 0x0096 | FRIEND_ACCEPT_PUSH | — | friend_id u64, username, nickname, online u8 |
| 0x0097 | FRIEND_SET_REMARK | friend_id u64, remark | 空；空字符串表示清除备注 |
| 0x00A0 | PROFILE_GET | target_id u64 | target_id u64, username, nickname, signature, avatar_version u32, avatar bytes, online u8, relationship u8, remark |
| 0x00A1 | PROFILE_UPDATE | nickname, signature, avatar_action u8 (0 不变/1 设置/2 清除), [avatar bytes] | nickname, signature, avatar_version u32 |
| 0x00A2 | AVATAR_GET | target_id u64 | target_id u64, avatar_version u32, avatar bytes |
| 0x00A3 | PROFILE_UPDATE_PUSH | — | user_id u64, nickname, avatar_version u32 |
| 0x00FF | ERROR | — | code (u16), message |

字符串为 `u16 长度 + UTF-8 字节`，整数均为大端。

用户资料命令（`0x00A0–0x00A3`）：

- `PROFILE_GET` 的 `relationship`：`0` 自己、`1` 好友、`2` 无关系、`3` 我已发送申请、`4` 对方已向我发送申请；`remark` 是查看者自己的好友备注，非好友为空。
- `PROFILE_UPDATE` 只修改调用者自己的资料。`avatar_action=1` 时请求体追加头像字节，`2` 表示清除；昵称/签名/头像在同一次更新中保存，头像设置或清除都会让 `avatar_version` 递增。
- `AVATAR_GET` 返回的版本与字节来自同一条记录；头像为空时返回空字节和当前版本，客户端据此缓存“无头像”。
- `PROFILE_UPDATE_PUSH` 只推送给在线好友，包含新昵称和头像版本，不包含签名和头像字节；客户端按版本按需拉取头像。
- 头像只接受 JPEG（`FF D8 FF`）或 PNG（`89 50 4E 47`）文件头，最大 48 KiB；客户端选择图片后居中裁成正方形、缩放到 256×256、JPEG quality 85，超过 48 KiB 时依次降到 quality 75/65 和 192×192、128×128，仍超限则提示失败。无头像或解码失败时显示彩色首字母头像。
- 用户名只用于登录、不可修改；昵称只用于展示、可修改且不要求唯一。资料页把用户名显示为 `@username`，`UID` 继续显示为 `U00000278`。
- 昵称和签名按 Unicode code point 计数：昵称 1–32 个字符，签名 0–128 个字符，首尾空白会被去除。服务端和客户端使用同一计数规则，昵称读取缓冲区已扩大到 129 字节，避免中文昵称被截断。

用户身份标识固定为 `U + 8 位十进制 user_id`，例如 `U00000278`；数据库主键不可变，因此该标识全局唯一且稳定。用户搜索支持 `U00000278`、`278`、用户名或昵称。好友备注按 `owner_id + friend_id` 保存在服务端 `friend_remarks` 表，只有备注所有者能看到，好友本人看不到该备注。

`HISTORY_RESP` 的每条消息记录：`id u64, sender_id u64, nickname, content, created_at, reply_to_id u64, reply_sender_name, reply_content, recalled u8`。`before_id=0` 表示最新一页，`next_before_id` 用于继续向上翻页。

## 联调

> 当前正式服务 `9100` 只监听回环并由 nginx 代理，且开启了 PROXY protocol。
> 下面的原始 TCP 工具请对无代理的测试实例执行，例如
> `./tools/run_server.sh --listen 127.0.0.1 --port 9102 --max-conn-per-ip 0`。

```bash
python3 tools/tcp_client.py --host 127.0.0.1 --port 9102
python3 tools/auth_client.py --host 127.0.0.1 --port 9100
python3 tools/auth_concurrency_test.py --host 127.0.0.1 --port 9100 --connections 8
python3 tools/security_checks.py --host 127.0.0.1 --port 9100 --room-id 1
python3 tools/connection_limit_test.py --host 127.0.0.1 --port 9100 --connections 25 --expected 20
python3 tools/room_client.py --host 127.0.0.1 --port 9100
python3 tools/room_cleanup_test.py --host 127.0.0.1 --port 9100 --room-id 11
python3 tools/private_history_client.py --host 127.0.0.1 --port 9100
python3 tools/user_search_client.py --host 127.0.0.1 --port 9102
python3 tools/message_features_client.py --host 127.0.0.1 --port 9102
python3 tools/friend_client.py --host 127.0.0.1 --port 9102
python3 tools/profile_client.py --host 127.0.0.1 --port 9102
python3 tools/db_reconnect_probe.py --host 127.0.0.1 --port 9102
python3 tools/tls_smoke.py --host 192.168.2.240 --port 9443 --ca client/certs/ca.crt
```

> 200 客户端 / 1 万连接测试会触发默认单 IP 20 限制，需要启动一个
> `--max-conn-per-ip 0` 的测试实例，或分散源 IP。

## Qt 客户端

Windows Qt 5.15.2 客户端位于 `client/`，构建与运行见 `client/README.md`：

```powershell
cd D:\OpenCode_WorkSpace\chatbox\client
.\build_client.ps1
.\build\release\chatbox.exe
```

客户端核心测试：

```powershell
cd D:\OpenCode_WorkSpace\chatbox\client\tests
.\run_client_tests.ps1
```

> 客户端 Outbox 只在进程内保存，应用退出后未发送/失败消息会丢失。

客户端便携分发包：

```powershell
cd D:\OpenCode_WorkSpace\chatbox\client
.\package_client.ps1
# 输出 release\chatbox-0.2.5-win64.zip
```

已支持登录/注册、房间、群聊、私聊、用户搜索、服务端好友申请/列表/备注、个人主页（查看他人/编辑自己）、头像上传与清除、昵称/签名修改与在线好友推送、引用回复、@提及、消息撤回、消息搜索、消息发送状态与失败重发、聊天气泡与本地时间、连接横幅、私聊未读角标与托盘提醒、历史分页、Token 自动登录、设置页、客户端日志、退出登录、心跳和断线重连；界面为浅色 Telegram 风格三栏布局。

个人资料入口：左栏底部自己的资料栏、好友列表头像/右键「查看资料」、群成员右键、私聊顶部「资料」按钮、用户搜索结果和好友申请对话框。好友主页优先显示备注；无头像或清除头像后回退彩色首字母；上传/清除头像会递增服务端版本。

## 当前范围

已完成：TCP 框架、MySQL 8.4 连接与自动重连、注册/登录/登出/Token 恢复、单会话踢下线、Argon2id 密码哈希、单大厅、好友申请/接受/删除、服务端好友列表与好友备注、不可变用户身份标识、用户资料 `PROFILE_GET`/`PROFILE_UPDATE`/`AVATAR_GET`/`PROFILE_UPDATE_PUSH`、昵称与签名编辑、头像上传/清除与版本缓存、Unicode 昵称计数与读取缓冲区修复、好友列表 `avatar_version`、群聊消息落库与广播、重复 `client_msg_id` 幂等、私聊发送/推送/离线持久化、最近会话列表、私聊未读数与提醒、用户搜索、引用回复、@提及、2 分钟撤回、消息搜索、房间/私聊历史游标分页、Qt 客户端 MVP、个人主页与头像 UI、Token 自动登录、登录失败限流、消息令牌桶、全局/单 IP 连接上限、systemd 常驻、日志轮转、10 秒 stats 监控、客户端 OpenSSL TLS、nginx stream TLS 终结、PROXY protocol 真实 IP、客户端日志/设置页/图标/版本/便携 ZIP。

验证记录：200 个客户端加入同一房间，发送 10 条消息，2000 次推送 0 失败；私聊历史 `limit=2` 连续翻页通过；断线、踢下线、登出后房间成员均被清理；登录失败第 6 次返回 1005；消息突发 30 条时 20 条通过、10 条返回 1005；单 IP 25 个连接时 20 个通过；TLS 1.3 登录成功，服务端日志记录真实客户端 IP；`9100` 外部不可直连，`9443` 正常。MySQL 连接被 `KILL` 后旧服务持续返回 2001，新服务自动重连并恢复；生产 MySQL 服务重启后，`9443` TLS 登录和房间列表成功，日志出现 `mysql connection lost` / `mysql connection restored` 和 `stats` 行。用户搜索按用户名/昵称/用户ID通过，未知关键词返回空；客户端重启/退出登录后服务端好友和备注保留；设置项持久化；客户端日志写入且不含密码/Token；便携 ZIP 可解压运行。房间/私聊引用快照、撤回推送、消息搜索均通过 `message_features_client.py`；好友申请/推送/接受/双方列表/删除、好友备注所有者隔离、清除备注、公开展示ID搜索均通过 `friend_client.py`。用户资料 GET/UPDATE/AVATAR/PUSH、昵称/签名校验、32 个中文字符昵称、头像上传/清除版本递增、在线好友推送和离线不推送均通过 `profile_client.py`。2026-10-19 生产部署后服务端 CTest 5/5、QtTest 27/27、9102 的 `profile_client.py`/`friend_client.py`/`room_client.py` 全部通过；`chatbox`、`nginx`、`mysql` 均为 active，`127.0.0.1:9100` 仅回环监听，`9443` TLS smoke 通过，当前客户端自动恢复生产会话并在服务重启后 2 秒内重连、RESUME 成功。

2026-10-19 全量改名验证（chatroom → chatbox 统一）：部署目录 `~/桌面/0917/chatbox`、systemd 服务 `chatbox.service`、证书目录 `/etc/chatbox/certs`、日志 `logs/chatbox.log`、MySQL 库与用户 `chatbox`。切换前逐表比对行数一致（users 556 / sessions 628 / messages 183 / private_messages 52 / friendships 17 / friend_requests 30 / friend_remarks 2 / private_read_state 14 / rooms 12），切换后服务端 CTest 5/5、`openssl verify` 链校验通过且 CA 指纹与旧目录完全一致（客户端 `ca.crt` 无需更新）、9102 临时实例 `auth_client.py` 全部通过（注册/重复注册/错误口令/登录/踢下线/恢复/登出，均写入改名后的 `chatbox` 库、无 ERROR），生产客户端停机 23 秒后自动重连并 RESUME 成功。回滚点：`~/backup/chatroom-db-cutover-*.sql`、旧库 `chatroom`、旧证书目录 `/etc/chatroom/certs`、旧单元文件 `/etc/systemd/system/chatroom.service`（已禁用）。

未完成：IP 展示（本轮明确留到以后，不预埋 UI）、TLS 证书自动续期、离线推送、监控告警、24 小时稳定性测试。
