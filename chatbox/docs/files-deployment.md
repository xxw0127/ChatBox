# 文件传输部署说明（Phase 2）

## 1. 顺序

1. 停写：通知客户端暂停发送文件；聊天服务可继续运行。
2. 备份数据库（必须）：
   ```bash
   mysqldump --single-transaction --routines --triggers chatbox \
     > /home/ubuntu/backups/chatbox-$(date +%Y%m%d-%H%M%S)-pre-files.sql
   ```
3. 备份程序与文件目录：
   ```bash
   cp /home/ubuntu/chatbox/build/chatbox_server \
      /home/ubuntu/backups/chatbox_server-$(date +%Y%m%d-%H%M%S)
   rsync -a /home/ubuntu/chatbox/files/ /home/ubuntu/backups/files-$(date +%Y%m%d-%H%M%S)/
   ```
4. 执行迁移：
   ```bash
   mysql chatbox < /home/ubuntu/chatbox/deploy/migrate_20261029_files.sql
   ```
5. 初始化运行环境：
   ```bash
   sudo /home/ubuntu/chatbox/tools/install_files_runtime.sh /home/ubuntu/chatbox/files
   ```
6. 部署新 `chatbox_server`，确认启动日志有 `server ready`，并检查 `--files-dir` 指向正确目录。

## 2. 文件目录

- 默认：`/home/ubuntu/chatbox/files`，权限 `0700`，属主为服务运行用户。
- `tmp/`：上传中的 `.part` 临时文件；程序启动/取消时清理。
- `thumbs/`：图片/GIF JPEG 缩略图，最大 320×180。
- `<xx>/<file_id>`：正式文件，按 file_id 低 8 位分目录。
- 已在 `chatbox.service` 中显式传入 `--files-dir`；不要放到 nginx 可访问目录。

## 3. ffmpeg

图片/GIF 缩略图依赖 `/usr/bin/ffmpeg` 与 `/usr/bin/ffprobe`。缺失时服务仍可启动，
日志记录 WARN，文件上传成功但 `thumb_available=0`，客户端显示通用文件卡片。
安装：
```bash
sudo apt-get install -y ffmpeg
```
可通过 `--ffmpeg-path` / `--ffprobe-path` 覆盖路径。

## 4. 配额与磁盘

- 单文件上限 2GB；每群文件总量 20GB；私聊不累计配额。
- 服务器可用磁盘低于 5GB（含本次上传大小）时拒绝新的上传初始化。
- 删除权限：上传者；群主/管理员可删除群文件；私聊仅上传者可删除。
- 删除只标记服务器副本并删除物理文件，客户端已下载副本保留，消息显示“文件已删除”。
- 群解散会删除该群服务器文件与消息；客户端本地记录保留并只读展示。

## 5. systemd / 防火墙

`chatbox.service` 运行参数示例：
```ini
ExecStart=/home/ubuntu/chatbox/tools/run_server.sh --listen 127.0.0.1 --port 9100 \
  --io-threads 4 --worker-threads 4 --max-connections 500 --max-conn-per-ip 50 \
  --proxy-protocol --files-dir /home/ubuntu/chatbox/files
```
文件数据走同一条 TCP/TLS 连接，无需额外端口；nginx stream 只需继续转发 9100/9443。

## 6. 回滚

1. 停止服务。
2. 恢复旧 `chatbox_server`。
3. 恢复数据库备份：
   ```bash
   mysql chatbox < /home/ubuntu/backups/chatbox-YYYYMMDD-HHMMSS-pre-files.sql
   ```
4. 文件目录可整体恢复到备份目录；旧版本服务端不读 `files` 目录，保留不会影响运行。

## 7. 验收

```bash
python3 tools/test_files_acceptance.py --host 127.0.0.1 --port 19100 \
  --db chatbox_test_groups --files-dir /home/ubuntu/chatbox/files
```
慢速 2GB 用例：
```bash
python3 tools/test_files_acceptance.py ... --large
```
