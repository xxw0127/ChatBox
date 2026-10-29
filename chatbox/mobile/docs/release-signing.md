# ChatBox Android 发布签名

## 1. 生成 keystore（只做一次）

在离线或受控机器上执行，密码使用密码管理器保存：

```powershell
keytool -genkeypair -v `
  -keystore D:\keys\chatbox-release.jks `
  -alias chatbox `
  -keyalg RSA -keysize 4096 -validity 10000 `
  -storetype JKS
```

要求：

- keystore 和密码不进入 Git、网盘或聊天工具；至少两份离线备份（不同物理位置）。
- 记下证书 SHA-256，后续上架/升级必须使用同一 keystore；丢失后无法覆盖安装升级。
- 不要把 `CHATBOX_ANDROID_*` 写进 `.ps1`、`.env` 或 CI 明文；使用系统环境变量或 CI secret。

## 2. 打包

```powershell
$env:CHATBOX_ANDROID_KEYSTORE = "D:\keys\chatbox-release.jks"
$env:CHATBOX_ANDROID_KEY_ALIAS = "chatbox"
$env:CHATBOX_ANDROID_KEYSTORE_PASSWORD = "..."
$env:CHATBOX_ANDROID_KEY_PASSWORD = "..."   # 可选
cd chatbox\mobile
.\package_apk.ps1 -BuildType Release
```

脚本会：

1. 调 `build_android.ps1` 生成 release APK；
2. 用 `apksigner` 重新签名到 `release/chatbox-<version>-<abi>.apk`；
3. 执行 `apksigner verify --print-certs` 并打印证书 SHA-256；
4. 生成 `release/chatbox-<version>-<abi>.sha256`。

## 3. 版本号

`chatbox/mobile/CMakeLists.txt`：

```cmake
QT_ANDROID_VERSION_CODE 1
QT_ANDROID_VERSION_NAME "0.1.0"
```

每次对外发布递增 `VERSION_CODE`（整数，不可回退），`VERSION_NAME` 用语义化版本。`package_apk.ps1` 从 `QT_ANDROID_VERSION_NAME` 读取文件名，可用 `-VersionName` 覆盖。

## 4. 校验

```powershell
apksigner verify --print-certs release\chatbox-0.1.0-arm64-v8a.apk
Get-FileHash -Algorithm SHA256 release\chatbox-0.1.0-arm64-v8a.apk
```

把 `apksigner` 输出的证书 SHA-256 与首次发布记录比对；不一致说明用错了 keystore，禁止发布。

## 5. 安装与更新

```powershell
adb install -r release\chatbox-0.1.0-arm64-v8a.apk
```

正式分发时让用户下载 APK 后手动安装；Android 8+ 需要在系统里允许“安装未知应用”。应用内更新清单和下载校验属于后续阶段（设计 §5.8），本阶段只准备签名 APK 和 SHA-256。

## 6. 轮换与回滚

- 不能在不丢失升级能力的前提下更换 keystore；如必须更换，需要新包名或让用户卸载重装（会丢本地记录，服务端数据不受影响）。
- 发布前保留上一版 APK 和 SHA-256；回滚就是重新分发上一版 APK（`versionCode` 必须更大，或让用户卸载后安装）。
- 本阶段服务端与客户端的兼容边界见 `chatbox/docs/multidevice-deployment.md`。
