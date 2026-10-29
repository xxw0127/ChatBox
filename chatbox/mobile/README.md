# ChatBox Android（F0）

Android 客户端的底座工程。当前阶段只验证工具链、TLS、service/IPC 和通知，不包含完整聊天 UI。

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
$env:JAVA_HOME = "C:\Program Files\Java\latest\jdk-17"
$env:ANDROID_SDK_ROOT = "$env:LOCALAPPDATA\Android\Sdk"
$env:ANDROID_HOME = $env:ANDROID_SDK_ROOT
$env:ANDROID_NDK_ROOT = "$env:ANDROID_SDK_ROOT\ndk\26.1.10909125"
```

## 构建

```powershell
cd chatbox\mobile
.\build_android.ps1
```

Release：

```powershell
.\build_android.ps1 -BuildType Release
```

模拟器（x86_64）：

```powershell
.\build_android.ps1 -Abi x86_64
```

安装到已连接的手机：

```powershell
.\build_android.ps1 -Install
```

APK 输出在 `build-android-<abi>` 下，脚本会打印完整路径。

## F0 验收

1. APK 能安装并显示“ChatBox Android”。
2. TLS 能连上 `121.5.168.189:9443`，发送 PING 收到 PONG。
3. service 在 `:qt_service` 进程常驻，通知栏有常驻通知。
4. UI 进程能通过 `QLocalSocket` 和 service 双向通信。
5. 锁屏、划掉任务、重启手机的行为记录在 `docs/f0-device-matrix.md`。

TLS 冒烟可以自动跑：

```powershell
.\tools\verify_tls_smoke.ps1
```

脚本会启动 app、点“连接测试”、读 UI 层级并断言出现 `PONG received`。手机需要保持解锁并允许安装/调试。

service/IPC/通知冒烟：

```powershell
.\tools\verify_service_smoke.ps1
```

脚本会授予通知权限、重启 app、检查 `ChatBoxService` 在 `:qt_service` 前台运行、通知 1000/1001 存在，并点 “IPC ping” 断言 `service: pong`。测试会 `force-stop` 自己的包，不影响其他应用。

OpenSSL 预编译库来源和校验见 `android/libs/README.md`；不要依赖 Android 系统自带的 OpenSSL。

## 已知限制

- 自建服务端 + 国内 ROM 没有系统级推送；锁屏通知依赖前台服务保活。
- 开机自启受 Android 15 和厂商 ROM 限制，按“尽力而为”处理。
- 本阶段不做完整手机 UI、群管理、文件、搜索、资料页和应用内更新。
