# ChatBox Android

Android 客户端（F0-F3）。包含登录、会话列表、1:1 文本聊天、前台 service、本地 SQLite、Keystore、通知和自动重连。

## 版本

- Qt：6.8.3 Android（`android_arm64_v8a` / `android_x86_64`）
- JDK：17（`JAVA_HOME`）
- Android SDK：platform 35、build-tools 35.0.0
- NDK：26.1.10909125
- minSdk：28（Qt 6.8 Android 要求）
- targetSdk：35
- 包名：`com.chatbox.mobile`
- 应用名：`ChatBox`

如果本机安装的是其他 6.8.x 补丁版本，修改 `build_android.ps1` 的 `-QtVersion`，并把实际版本记到本文档；不要混用 6.7/6.9。

## 环境变量

```powershell
$env:JAVA_HOME = "C:\Program Files\Eclipse Adoptium\jdk-17"
$env:ANDROID_SDK_ROOT = "D:\Android\Sdk"
$env:ANDROID_HOME = $env:ANDROID_SDK_ROOT
$env:ANDROID_NDK_ROOT = "$env:ANDROID_SDK_ROOT\ndk\26.1.10909125"
$env:PATH = "D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;D:\Qt\6.8.3\mingw_64\bin;$env:PATH"
```

## 构建与安装

```powershell
cd chatbox\mobile
.\build_android.ps1              # Debug APK
.\build_android.ps1 -BuildType Release
.\build_android.ps1 -Install     # 安装并启动，手机会弹安装确认
```

APK 输出在 `build-android-<abi>\android-build\build\outputs\apk\<debug|release>`。

## 签名发布

`package_apk.ps1` 读取以下环境变量（不要把 keystore 或密码写进仓库）：

```powershell
$env:CHATBOX_ANDROID_KEYSTORE = "D:\keys\chatbox-release.jks"
$env:CHATBOX_ANDROID_KEY_ALIAS = "chatbox"
$env:CHATBOX_ANDROID_KEYSTORE_PASSWORD = "..."
$env:CHATBOX_ANDROID_KEY_PASSWORD = "..."   # 可选，默认与 keystore 密码相同
.\package_apk.ps1 -BuildType Release
```

输出 `release/chatbox-<version>-<abi>.apk`、同名 `.sha256`，并打印 `apksigner verify --print-certs` 的证书 SHA-256。生成 keystore、备份和轮换步骤见 `docs/release-signing.md`。

## 测试

`clientcore` 单元测试（Qt 6，Linux 或带 Qt 6.8 桌面 kit 的 Windows）：

```bash
cd chatbox/clientcore/tests
qmake6 clientcore_tests.pro && make -j4
QT_QPA_PLATFORM=offscreen ./clientcore_tests
```

覆盖 SessionClient、MobileStore、SecureStore、IPC 和 ChatService（47 项检查）。真机验收步骤和结果见 `docs/acceptance.md`。

## 结构

- `clientcore/`：跨平台共享核心（transport、session、SQLite、service 编排、IPC），同时用于 Android 和桌面测试。
- `src/`：Android 入口、service 组合、QML 桥、Keystore/通知 JNI 实现。
- `qml/`：登录、会话列表、1:1 聊天、设置。
- `android/`：Manifest、QtService、前台通知、通知点击入口、Keystore、运行时权限。

## 已知限制

- 自建服务端 + 国内 ROM 没有系统级推送；锁屏通知依赖前台服务保活。
- 小米 HyperOS 从最近任务划掉 App 会杀整个 UID，`START_STICKY` 在 30 秒内不会拉起；开机自启需要用户在系统设置里允许“自启动”。详见 `docs/f0-device-matrix.md`。
- 当前只做登录、会话列表、1:1 文本聊天；群管理、文件、搜索、资料页和应用内更新不在本阶段。
- 生产服务端尚未部署多设备版本时，新手机登录会按旧服务端的单会话规则踢掉桌面；服务端上线步骤见 `../docs/multidevice-deployment.md`。
