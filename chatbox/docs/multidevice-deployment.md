# ChatBox 多设备部署与回滚

日期：2026-10-29
适用版本：服务端 `migrate_20261029_multidevice.sql` + 0.4.x 桌面客户端 + Android 底座

## 1. 上线顺序

1. 备份生产库：

   ```bash
   mysqldump --single-transaction --routines --triggers chatbox \
     > ~/backups/chatbox-$(date +%Y%m%d-%H%M%S).sql
   ```

2. 执行迁移（可重复执行）：

   ```bash
   sudo mysql < chatbox/deploy/migrate_20261029_multidevice.sql
   ```

3. 部署新服务端二进制并重启：

   ```bash
   sudo systemctl stop chatbox
   # 用 scp/rsync over ssh 更新 /home/ubuntu/chatbox/build/chatbox_server
   sudo systemctl start chatbox
   sudo systemctl status chatbox --no-pager
   ```

4. 用旧 0.4.0 桌面客户端登录，确认能收发消息；再用新桌面/Android 同时登录，确认互不踢下线。

5. 观察 `logs/chatbox.log` 的 `stats` 行，重点看 `db_errors`、`bcast_drop`、`rate_limited`。

## 2. 迁移做了什么

`migrate_20261029_multidevice.sql` 只做增量：

- `sessions` 增加 `session_id`、`device_id`、`device_name`、`platform`、`app_version`、`caps`、`last_seen_at`。
- 回填存量会话的 `session_id`、`last_seen_at`。
- 增加 `uk_session_id`、`idx_user_device`、`idx_user_seen`。
- 不删除任何列、不重命名表、不修改消息表。

脚本按 `information_schema` 判断，可重复执行。

## 3. 兼容性

| 客户端 | 行为 |
| --- | --- |
| 旧 0.4.0 | 仍能登录、收发原有消息；不发送设备信息；不接收新推送；旧客户端之间仍按 legacy 规则替换会话；不会踢新手机 |
| 新桌面 | 发送设备信息与能力位；接收已读同步/私聊回显；支持设备管理 |
| Android 底座 | 发送设备信息与能力位；支持 session list/revoke、sync pull、已读、回显 |

服务端登录响应尾部追加 `server_caps` 和 `session_ttl_seconds`；旧客户端只读前面的字段，忽略尾部。

## 4. 回滚

服务端代码回滚：

```bash
sudo systemctl stop chatbox
# 恢复旧 chatbox_server
sudo systemctl start chatbox
```

schema 为增量，旧二进制不读取新增列，可以保留；如果要完全回滚：

```bash
sudo mysql chatbox < ~/backups/chatbox-YYYYMMDD-HHMMSS.sql
```

回滚会丢失迁移后产生的 session 元数据，但消息、用户、好友、群数据不受影响。

## 5. 测试实例（不碰生产）

VPS 上已建立隔离目录和测试库：

```text
源码：/home/ubuntu/chatbox-foundation
库：  chatbox_foundation
端口：127.0.0.1:19100
```

启动：

```bash
set -a; . ~/chatbox/conf/chatbox.env; set +a
cd ~/chatbox-foundation
nohup ./build-test/chatbox_server --listen 127.0.0.1 --port 19100 \
  --db-name chatbox_foundation --io-threads 2 --worker-threads 2 \
  --max-connections 100 --max-conn-per-ip 20 --idle-timeout 90 \
  --log-file ~/chatbox-foundation/test-server.log > test-server.out 2>&1 &
```

停止：`pkill -f 'chatbox-foundation/build-test/chatbox_server'`（只匹配测试实例）。

## 6. 验证命令

```bash
cd ~/chatbox-foundation
python3 tools/test_multidevice_auth.py --host 127.0.0.1 --port 19100
python3 tools/test_multidevice_fanout.py --host 127.0.0.1 --port 19100
python3 tools/test_multidevice_read_echo.py --host 127.0.0.1 --port 19100
python3 tools/test_multidevice_sync.py --host 127.0.0.1 --port 19100
python3 tools/test_multidevice_caps.py --host 127.0.0.1 --port 19100
```

服务端 CTest：

```bash
cd ~/chatbox-foundation/build-test
ctest --output-on-failure
```

桌面客户端：

```powershell
cd chatbox\client\tests
.\run_client_tests.ps1
```

## 7. 已知边界

- 旧桌面不会实时收到手机发出的私聊消息，需要重新打开会话拉历史；这是未带能力位的预期降级。
- 服务器不保证 24 小时在线；重启/休眠期间手机通知不可用。
- 小米 HyperOS 从最近任务划掉 App 会杀进程，`START_STICKY` 不会在 30 秒内拉起；开机自启默认被系统拦截，需要在系统设置里允许“自启动”。这是 OEM 行为，不是服务端问题。
- 多设备消息同步以服务端 MySQL 为准；本地历史仍是本机副本。

## 8. 生产部署记录（2026-10-30 12:51 UTC+8）

- 备份：`/home/ubuntu/backups/chatbox-20261030-125012-pre-multidevice.sql`
- 旧二进制：`/home/ubuntu/backups/chatbox_server-20261030-125012-pre-multidevice`
- 迁移后 `sessions` 的 64 条存量会话全部回填 `session_id`，无 NULL
- 新二进制：`/home/ubuntu/chatbox/build/chatbox_server`，systemd `chatbox` active，监听 127.0.0.1:9100
- 验证：服务重启后手机 1.5 秒内 RESUME 成功；同账号第二客户端与手机同时在线；双向 echo；离线期间 40 条 ×1988B（约 80 KB）消息重连后一次补齐