# ChatBox 客户端（Qt 5.15）

产品名 **ChatBox**；目录、可执行文件（`chatbox.exe`）、QSettings 键与服务端 systemd 服务名在 2026-10-19 已全部统一为 `chatbox`/`ChatBox`。启动时会把旧的 `Chatroom/ChatroomClient` 设置一次性迁移到 `ChatBox/ChatBox`，已有登录会话和设置不受改名影响。

UI 颜色、字号、间距、组件和暗色规则见 [`../docs/design/DESIGN.md`](../docs/design/DESIGN.md)。

Windows Qt 5.15.2 Widgets 客户端，原生 Winsock + 独立网络线程。

## 环境

```text
Qt:      D:\Qt\5.15.2\mingw81_64
MinGW:   D:\Qt\Tools\mingw810_64
OpenSSL: C:\Program Files\OpenSSL-Win64
```

客户端固定连接 `121.5.168.189:9443`，开启 TLS 并校验 `certs\ca.crt`。登录页只显示用户名和密码；点“注册”进入注册态后才显示昵称，主按钮变成“注册”，次按钮变成“返回登录”。客户端不再提供服务器/IP/端口/TLS 编辑入口；旧版保存的 `frp-oil.com:15318` 会在首次启动时迁移到固定地址，并清除旧服务器 session token。

密码下方有“自动登录”和“记住密码”两个勾选框：自动登录默认勾选，用保存的会话 token 在启动时恢复登录；记住密码默认不勾选，勾选后才保存用户名和密码，密码通过 Windows DPAPI 加密后存本机，不保存明文。取消自动登录会清除已保存的会话 token；取消记住密码会清除登录页凭据和该账号的 DPAPI 密码。

左栏底部个人资料栏的 `⋯` 菜单提供账号切换。保存过密码的账号可直接切换并重新登录；没有保存密码的账号会停在登录页并预填用户名。切换会关闭旧会话和本地历史连接，新账号使用独立的 `user_id` 历史分区。

## 自动更新

客户端启动后会自动检查 `https://121.5.168.189/updates/latest.json`。发现新版本时提示“立即更新 / 稍后”；立即更新会下载完整便携 ZIP、校验 SHA-256、替换当前程序目录并重启，失败会回滚旧目录。用户数据在 `%APPDATA%`，不随程序目录替换。

更新包由自有服务器 443 提供；客户端固定使用 `certs/ca.crt`，不开放服务器地址编辑。第一个带更新器的版本是 0.2.4，首次需要手动安装，之后可一键更新。

## 高 DPI

Windows 的 125% / 150% 缩放使用 Qt 分数缩放（`PassThrough`），不会把 150% 向上取整成 200%。主窗口初始尺寸和最小尺寸都会限制在当前屏幕可用区域以内，避免高缩放设备上被压成一团。`QLineEdit` / `QSpinBox` 使用 `min-height: 36px` 且不设上下 padding，避免 150% 下字体行高变大后输入文字被上下裁切。

## 构建

```powershell
cd D:\OpenCode_WorkSpace\chatbox\client
.\build_client.ps1
```

脚本会编译并把 Qt DLL、MinGW 运行库、OpenSSL DLL、`platforms` 插件、`imageformats` 插件、`sqldrivers` 插件和 `certs` 复制到 `build\release`。

`imageformats\qjpeg.dll` 是 JPEG 头像编解码的运行时依赖；脚本会自动创建
`build\release\imageformats` 并复制该插件。缺少它时头像上传/显示会回退或失败。

`Qt5Sql.dll` 与 `sqldrivers\qsqlite.dll` 是本地聊天记录库（SQLite）的运行时依赖；
脚本会一并复制到 `build\release` 和 `build\release\sqldrivers`。缺少它们时客户端
仍能启动，但本地历史功能整体降级为「不可用」（详见下文「本地聊天记录与离线模式」）。

如果客户端正在运行，先关闭 `chatbox.exe`，否则链接会失败。

## 分发包

```powershell
cd D:\OpenCode_WorkSpace\chatbox\client
.\package_client.ps1
```

生成：

```text
release\chatbox-0.2.5-win64.zip
release\chatbox-0.2.5-win64\chatbox.exe
```

`release\` 是唯一的最终发布目录；`build\release\` 只是编译输出，不要直接发给别人。脚本不再额外保留 `dist\` 副本。

ZIP 内含 exe、Qt/MinGW/OpenSSL DLL、`vcruntime140.dll`、`certs`、`platforms`、`styles`、
`imageformats`、`sqldrivers` 和 README，可直接解压运行。
`installer\chatbox.iss` 是 Inno Setup 模板；当前机器未安装 Inno Setup，所以只生成便携 ZIP。
Windows 10/11 自带 UCRT；更旧的系统需要安装 VC++ 2015-2022 Redistributable。

> 便携包必须包含 `Qt5Sql.dll`、`sqldrivers\qsqlite.dll` 和 `imageformats\qjpeg.dll`。
> `package_client.ps1` 在打包后会逐个校验这三项，缺任何一项直接报错、不产出 ZIP；
> 缺 SQLite 驱动时程序仍能启动，但本地历史整体不可用。

> 用 Qt Creator 直接构建时，qmake 会复制 `certs`，但不会复制 OpenSSL DLL 或
> Qt `imageformats` / `sqldrivers` 插件。如果运行时提示缺少 `libssl-4-x64.dll` /
> `libcrypto-4-x64.dll`，或头像无法解码，或本地历史提示缺少 SQLite 驱动，
> 先执行一次 `build_client.ps1`，或手动把
> `C:\Program Files\OpenSSL-Win64\bin` 下的两个 DLL、
> `D:\Qt\5.15.2\mingw81_64\plugins\imageformats\qjpeg.dll` 和
> `D:\Qt\5.15.2\mingw81_64\plugins\sqldrivers\qsqlite.dll`
> 复制到 `build\release` 对应目录。

## 运行

```powershell
.\build\release\chatbox.exe
```

默认服务器地址：`121.5.168.189:9443`，TLS 固定开启；服务器地址由客户端编译配置固定，不在界面中修改。

## TLS 证书

客户端校验证书链和 IP SAN，CA 在：

```text
D:\OpenCode_WorkSpace\chatbox\client\certs\ca.crt
```

当前服务端证书 SAN 为 `IP:121.5.168.189, DNS:chatbox.local`，由随包更新的 `certs/ca.crt` 校验。IP 变化时需要重签服务器证书并同步更新客户端证书。

## 个人资料

- 左栏底部个人资料栏（头像、昵称、UID）点击打开自己的主页。
- 好友列表点击头像、好友右键「查看资料」、群成员右键「查看资料」、私聊顶部头像、用户搜索结果和好友申请对话框都可打开资料页（会话头部只有「头像 + 标题 + ⋯」，没有单独的「资料」按钮）。
- 他人主页显示头像、备注优先的标题、昵称、`@username`、UID、在线状态、签名和好友操作按钮；自己的主页可以编辑昵称（1–32 个 Unicode 字符）、签名（0–128 个字符），以及更换/移除头像。
- 头像选择支持 JPEG/PNG：客户端居中裁成正方形、缩放到 256×256、JPEG quality 85；超过 48 KiB 时降到 quality 75/65 和 192×192、128×128，仍超限则提示失败。
- 上传或清除头像都会递增服务端 `avatar_version`；客户端按 `user_id + version` 缓存。没有头像、清除头像或解码失败时回退为彩色首字母头像。
- 在线好友收到 `PROFILE_UPDATE_PUSH` 后更新好友列表昵称/头像；有备注时列表和聊天顶部仍优先显示备注。
- 修改昵称后自己的资料栏、资料页、会话 `QSettings` 和后续发送消息立即使用新昵称，不覆盖当前私聊/房间标题。
- JPEG 头像依赖 Qt `imageformats\qjpeg.dll`；`build_client.ps1` 会把它复制到 `build\release\imageformats`。
- 生产环境公网 `121.5.168.189:9443` 通过 TLS 连接；当前客户端可自动恢复会话并在服务重启后重连。

## 消息显示与操作

- 每条消息在气泡外侧显示圆形头像：对方在左、自己在右；对方消息在气泡上方显示「昵称 + UID」（同一发送者 3 分钟内只显示一次），自己消息不重复昵称。没有服务端头像时先用彩色首字母头像，头像到达后自动替换。
- 输入 `@`（行首、空白或常用标点之后）会弹出成员面板，继续输入按昵称/UID 过滤；`↑ / ↓` 移动、`Enter / Tab` 选中、`Esc` 关闭，鼠标单击同样可选。面板第一项是「所有人」，`@所有人` 会触发所有人的提及提醒。输入区的 `@` 按钮打开同一个面板；私聊不提供 @。
- 右键消息气泡弹出菜单：失败消息有「重发」，他人消息有「回复 / 复制」，自己的消息在窗口内有「回复 / 撤回 / 复制」；空白处保留系统自带的复制/全选菜单。不再有悬停浮动的「回复 / 撤回」小操作条。
- 「回复」把引用放进输入区，回车发送后新消息带引用行；「撤回」窗口与服务端一致（消息发出 2 分钟内），超出窗口的消息客户端不再提供入口，服务端仍会二次校验。
- 消息搜索结果显示当前会话内的命中记录；单击（或回车）跳转到原消息并短暂高亮。目标不在当前页时先拉取包含它的一页，页底出现「回到最新 ↓」，定位期间新消息只入库不打断当前页。

## 本地聊天记录与离线模式

客户端在本机维护一份聊天记录副本（SQLite），**服务端仍然是唯一权威来源**；本地库只是离线时的只读镜像。

### 存放位置与账号隔离

- 默认目录：`%APPDATA%\ChatBox\ChatBox\history\history.sqlite`（`AppDataLocation` + `history` 子目录，目录不存在会自动创建）。
- 可在「设置 → 本地聊天记录 → 更改目录」换成任意可写目录，「打开目录」直接打开该文件夹；设置页同时显示当前占用空间。
- 同一个 `history.sqlite` 文件按 `user_id` 分区：连接名按账号隔离（`history_<user_id>`），所有读写 SQL 都带 `user_id` 过滤；切换账号只看得到自己的记录，清理也只清当前账号。
- 换目录时可选择「迁移」或「不迁移」：迁移会复制现有记录并校验行数，「不迁移」会在新目录创建一个空的记录库；目标目录已存在 `history.sqlite` 时直接失败并继续使用旧目录。

### 写入规则

- **只保存服务端已确认的消息**（`message_id > 0` 且状态为已发送）。待发、发送中、失败的消息永远不落盘，本地库不会残留「其实没发出去」的记录。
- 落库时机：自己发送成功拿到 `message_id`、收到对方推送、打开会话时服务端返回的那一页历史。
- 按 `(user_id, 会话类型, 会话对象, message_id)` 去重，重复拉取同一页历史不会产生重复行。
- 撤回同样按会话定位：房间与私聊的 `message_id` 各自独立编号，撤回只清掉对应会话里的那一条；后台会话的撤回推送也会同步清空本地副本（不打断当前会话、不改未读数）。
- 在线时以服务端历史为准：重新打开会话会从服务端重新拉取并再次写入本地库；本地首屏与服务端这一页合并后按时间递增排列，本地更旧的行不会被压在列表底部。

### 离线模式

- 进入条件：本机存有该账号的记录、但当前拿不到服务端（恢复会话失败或超时）→ 自动进入离线模式，顶部横幅显示「离线模式 · 仅显示本地记录」。登录页不再提供手动「查看本地记录」入口。
- 离线模式是**只读**的：输入框与发送按钮禁用，@ 提及、好友申请、我的资料、用户搜索、在线成员列表都不可用；整个离线过程不向服务端发送任何业务请求帧。
- 本地搜索：⋮ 菜单「消息搜索」只查**当前会话**的本地记录（最多 50 条），不发帧、不占用服务端序号；切到别的会话就是另一个搜索范围。
- 自动重连：离线模式仍保留 token 时按指数退避继续重连，横幅右侧出现「重试连接」按钮；重连成功即退出离线模式并恢复完整能力。
- 清理只删除本机副本：设置页「清理」对话框里可选「清空全部本地记录」或「清理以下天数之前的本地记录」（需勾选「只删除本机副本，不影响服务器历史」再点「开始清理」）；只影响当前账号的本地库，不动服务端数据；之后在线重新打开会话会从服务端重新拉取并再次落盘。

### 便携包要求

便携包必须带 `Qt5Sql.dll`、`sqldrivers\qsqlite.dll` 和 `imageformats\qjpeg.dll`（`package_client.ps1` 打包后逐项校验，缺任何一项直接报错、不产出 ZIP）。

缺少 SQLite 驱动时客户端仍能启动、可正常登录聊天，但本地历史整体降级为「不可用」：设置页「本地聊天记录」分组显示「本地历史不可用（缺少 SQLite 驱动）」、相关按钮置灰，断网时不会进入离线模式。缺 `imageformats\qjpeg.dll` 时只影响 JPEG 头像的编解码。

## 快捷键

- `Ctrl+F`：搜索当前会话消息
- `Ctrl+Shift+F`：搜索用户
- `Ctrl+,`：打开设置
- `Alt+↑` / `Alt+↓`：切换上一条 / 下一条会话
- `Esc`：取消当前回复（仅在回复条可见时）
- 选中消息文本后按 `F10` / `Shift+F10` / `Menu`：打开该消息的回复/撤回菜单

## 测试

客户端核心逻辑使用 QtTest：

```powershell
cd D:\OpenCode_WorkSpace\chatbox\client\tests
.\run_client_tests.ps1
```

当前回归结果：181 passed、0 failed（含头像裁剪/压缩/回退、头像缓存版本、资料页按钮状态、昵称/签名字符计数、消息操作可用性、撤回窗口判定、气泡宽度上限与引用块宽度、气泡圆角绘制、右侧气泡描边与视口边缘裁剪、气泡描边非文本对比度、深浅色主按钮对比度、深层时间戳对比度、输入框空白滚动条、气泡位置映射、重试链接样式、账号切换器构造、账号密码 DPAPI 存储与注销、高 DPI 分数缩放、窗口可用区域与输入框行高守卫、服务器地址迁移、主题色板与 `style.qss` 一致性守卫、气泡配色来源、`app.ico` 多帧尺寸守卫、大厅徽标配色、托盘图标来源守卫、在线成员列表头像来源守卫、字母头像调色板单一来源守卫、产品名品牌守卫、QSettings 迁移（拷贝/不覆盖）、统一标识符守卫、安装包模板 exe 名守卫、私聊无 @ 守卫、头部头像/⋮ 菜单守卫、系统弹窗开关守卫、开机自启动命令与 Run 项写入、单实例启动守卫、托盘可用性策略、未读角标开关守卫、消息头像/UID 渲染、头像资源注入、@ 词法与面板过滤、搜索跳转高亮、presence 序列去重、气泡文字垂直居中、@ 空状态与默认选中、切换会话复位搜索、跳转超时复位）。

本地历史与离线相关的覆盖（2026-10-20）：

- 历史库 schema 创建、打开失败上报、同一连接名的第二实例拒绝。
- 消息落库与最近消息读取、按 `message_id` 去重、未确认消息不落盘、撤回标记（正文清空）。
- 撤回按会话定位（`historyStoreRecallIsConversationScoped`）：房间与私聊的 `message_id` 各自独立编号，私聊撤回不会误伤房间里同号的消息。
- 本地首屏与服务端返回页合并后按时间递增排列（`messageStoreMergesLocalAndServerPagesInOrder`）：本地更旧的行不会被留在列表底部，待发消息排最后。
- 后台会话的撤回推送同样落本地库（`recallPushUpdatesBackgroundHistory`）：不重绘当前视图、不改未读数。
- 账号隔离与会话隔离、会话内本地搜索、会话摘要（最后一条消息与时间）。
- 清理全部 / 按天数清理、换目录迁移数据、目标目录已存在时拒绝并保留旧目录。
- 写穿透：服务端确认的消息才落库（`message_store` 挂接历史库后）。
- 切换会话时先用本地历史首屏渲染（`conversationOpensFromLocalHistoryFirst`）、非当前会话的推送也先落本地库（`incomingPushesArePersistedForOtherConversations`）。
- 离线只读：离线模式不发送任何业务帧、离线本地搜索只查当前会话。
- 设置页历史信息（存放位置、占用空间、清理/换目录/打开目录入口）展示。
- 断网自动进入离线模式；构建脚本携带 SQLite 驱动的守卫。

## 已实现

- Coinbase 设计语言浅色 UI：白底、冷灰表面（`#F7F7F7` / `#EEF0F3`）、单一蓝色强调 `#0052FF`、hairline 分隔、4/8/12/16 圆角刻度（输入框与发送键保留 18px 胶囊）、气泡与 `@` 面板圆角卡片；己方气泡保留绿色语义（`#05B169` 的 10% tint `#E6F7F0`）。色板唯一来源 `core/ui_theme.h`，`themeTokensMatchStyleSheet` 测试守卫 `style.qss` 不漂移（规格：`../../docs/superpowers/specs/2026-10-19-coinbase-style-ui.md`）
- 品牌图标（2026-10-19）：应用/任务栏图标改为 `resources/app.ico` 多帧标记（16/24px 简化版纯气泡、32/48/64/128/256px 完整版蓝底白气泡 + 镂空 C；生成脚本 `tools/make_app_icon.ps1`，可重复再生成）；托盘图标同款（16px 简化帧）；大厅徽标改为品牌蓝气泡 `roomAvatar()`，房间列表与聊天头部共用，圆底取 `theme::kAccent`（规格：`../../docs/superpowers/specs/2026-10-19-client-brand-icons.md`、证据图 `screenshots/ui-brand-icons.png`）
- 注册 + 注册成功自动登录
- 登录、Token 会话恢复
- TLS 加密连接（OpenSSL，证书校验）
- 断线指数退避重连、连接横幅、离线禁用发送
- 单大厅、在线成员
- 群聊发送/接收、本地回显
- 消息发送中/已发送/失败状态、10 秒超时、失败重发
- 聊天气泡、日期分隔、UTC 转本地时间
- 引用回复（右键菜单）、@提及（输入 `@` 或点 `@` 按钮弹出成员面板；`@所有人` 提醒全员；私聊不提供 @）、2 分钟内撤回、消息搜索（点击结果跳转并高亮，支持离线本地记录）
- 私聊会话列表、私聊发送/接收
- 私聊未读数、任务栏闪烁、系统弹窗提醒（可在设置里关闭，关闭后只保留闪烁与角标）
- 房间/私聊历史分页（⋮ 菜单「加载更早消息」）
- Token 自动保存、启动 RESUME、退出登录
- 用户搜索（用户名/昵称/用户ID）、好友申请/接受/拒绝、好友列表、删除好友
- 个人主页：查看他人资料、编辑自己资料、头像上传/清除/回退、昵称/签名修改
- 在线好友资料推送（昵称 + 头像版本）、好友列表 `avatar_version`、头像按版本缓存
- 在线成员列表头像（2026-10-19）：成员行优先取头像缓存，未命中时按「版本未知先问一次」请求 `AVATAR_GET`，响应到达后重绘该行（含自己；证据图 `screenshots/ui-member-avatar.png`）
- 服务端好友备注、在线/离线分组、两行消息预览、时间/未读角标、实时在线状态推送（登录/登出/断线；客户端按序列号丢弃乱序旧事件）
- 消息头像与昵称/UID（2026-10-28）：气泡外侧圆形头像、同发送者昵称分组、首字母头像兜底、服务端头像异步替换
- `@` 成员面板（2026-10-28）：输入 `@` 触发、昵称/UID 过滤、键盘与鼠标选择、`@所有人`
- 消息搜索跳转（2026-10-28）：点击结果滚动定位并高亮 1.8 秒，旧页定位带「回到最新」。
- 用户身份标识 `U + 8 位 user_id`，设置页可查看，搜索和好友申请中展示
- 全量改名统一（2026-10-19）：目录 `chatbox/`、可执行文件 `chatbox.exe`、C++ 命名空间 `chatbox::client`、头文件宏 `CHATBOX_*`、QSettings `ChatBox/ChatBox`、服务端目标 `chatbox_server`、systemd 服务 `chatbox`、MySQL 库/用户 `chatbox` 全部一致；启动时自动迁移旧 QSettings（`Chatroom/ChatroomClient` → `ChatBox/ChatBox`），用户无感
- 设置页：固定的服务器地址（只读）、消息提醒、系统弹窗通知、开机自启动（静默到托盘）、外观（跟随系统/浅色/深色）、日志目录
- 会话头部（2026-10-20）：只保留 `头像 + 标题 + ⋮`；点头像进入当前私聊对象资料（房间头像无动作），⋮ 菜单含「消息搜索」与房间的「加载更早消息」，输入区不再有搜索按钮
- 启动行为（2026-10-20）：单实例锁（第二次启动提示后退出）；托盘菜单「打开 ChatBox / 退出」；`--minimized` 启动只驻留托盘，托盘不可用时回退为正常窗口；开机自启动写入 `HKCU\...\Run\ChatBox`
- 本地聊天记录库（2026-10-20）：单个 SQLite 文件 `history.sqlite`（默认 `%APPDATA%\ChatBox\ChatBox\history\`），按 `user_id` 分区、连接名按账号隔离（`history_<user_id>`）；只落服务端已确认的消息，写穿透 + `message_id` 去重 + 会话摘要 + 撤回同步
- 离线只读模式（2026-10-20）：恢复会话失败/超时自动进入离线模式（横幅「离线模式 · 仅显示本地记录」）；离线期间不发送任何业务请求帧；⋮ 菜单本地搜索只查当前会话；保留 token 时按指数退避自动重连，横幅上提供「重试连接」
- 设置页「本地聊天记录」分组（2026-10-20）：存放位置、占用空间、清理（「清空全部本地记录」/「清理以下天数之前的本地记录」）、更改目录（「迁移」/「不迁移」）、打开目录
- 便携包携带 `Qt5Sql.dll` 与 `sqldrivers\qsqlite.dll`（2026-10-20）：`package_client.ps1` 打包后逐项校验 `chatbox.exe`、`Qt5Sql.dll`、`sqldrivers\qsqlite.dll`、`imageformats\qjpeg.dll`，缺项直接报错不产出 ZIP
- 客户端日志文件、图标、版本信息和便携 ZIP
- 自己的推送按 `sender_id` 忽略，避免与本地回显重复

## 说明

- 网络线程用原生 Winsock，UI 不直接操作 socket。
- Token 保存在 `QSettings`，启动时自动 RESUME；退出登录只清除会话，好友和设置保留。
- 好友关系保存在服务端 MySQL，搜索用户后发送申请，对方接受后进入好友列表。
- 好友备注按当前账号保存在服务端，换电脑或重装客户端后仍可见；好友本人看不到该备注。
- 用户身份标识由不可变数据库主键生成，格式为 `U00000278`，不是可修改的昵称。
- 用户名 `@username` 只用于登录且不可修改；昵称用于展示，可以修改，二者不是同一个字段。
- 头像保存在服务端 `users.avatar`（最大 48 KiB），按 `avatar_version` 缓存；清除头像不会删除历史版本号，而是继续递增并回退首字母。
- 成员列表的成员多数不是好友，其头像版本不出现在好友/会话列表里：客户端按「版本未知先问一次」的策略请求一次 `AVATAR_GET`（服务端返回当前版本，含"无头像"），并把结果写进同一头像缓存；因此成员换头像后的展示依赖下次成员列表加载或资料更新推送。
- 字母头像（无头像时的回退）统一取色于 `core/avatar_utils.cpp::fallbackAvatar()`；修复前 `main_window.cpp` 自带 6 色私有表、而资料页用 8 色表，同一用户两处底色不同（证据图 `screenshots/ui-avatar-palette.png`）。
- JPEG 头像运行时必须有 `imageformats\qjpeg.dll`；该文件由 `build_client.ps1` 复制到构建输出。
- 窗口/托盘图标运行时读取 `resources/app.ico`，需要 `imageformats\qico.dll`（`build_client.ps1` 一并复制）；缺少该插件时窗口图标回退为 exe 内嵌图标（`app.rc` 的 `IDI_ICON1`），托盘图标可能不显示。
- 本地历史只是服务端数据的**本机副本**：在线时以服务端历史为准，重新打开会话会从服务端重新拉取并再次落盘；待发/失败消息不落盘。
- 离线模式是只读视图：输入与发送、@ 提及、好友申请、我的资料、用户搜索、在线成员列表均不可用，离线期间除重连外不产生任何业务请求。
- 清理只删本机副本，不动服务端数据；按 `user_id` 隔离，只清当前账号。
- 切换存储目录在设置页发起：迁移会复制现有记录并校验行数，目标目录已存在 `history.sqlite` 时直接失败并继续使用旧目录。
- 缺少 `Qt5Sql.dll` / `sqldrivers\qsqlite.dll` 时本地历史整体降级为「不可用」（设置页显示「本地历史不可用（缺少 SQLite 驱动）」），聊天功能不受影响。
- 日志：`%APPDATA%\ChatBox\ChatBox\logs\client.log`，超过 5 MB 轮转为 `.1`。
- Outbox 只保存在进程内：应用退出后，未发送/失败消息会丢失；跨进程持久化尚未实现。
- 当前仍没有聊天图片/文件传输（头像除外）、群管理。
- @提及目前按昵称匹配并触发客户端提醒，不保存服务端 mention 表。
- 消息搜索返回结果列表，暂不支持双击定位到历史位置。
- ???????`screenshots/README.md`?
  - ???`screenshots/ui-login-default.png`
  - ???`screenshots/ui-main-room.png`
  - ???`screenshots/ui-private-chat.png`
  - ?????`screenshots/ui-message-actions.png`
  - ?????`screenshots/ui-message-search.png`
  - ???`screenshots/ui-profile.png`
  - ???`screenshots/ui-settings.png`
  - ?????`screenshots/ui-user-search.png`
  - ?????`screenshots/ui-friend-request.png`
  - ?????`screenshots/ui-chat-offline.png`
  - ??/?????`screenshots/ui-brand-icons.png`?`screenshots/ui-member-avatar.png`?`screenshots/ui-avatar-palette.png`
