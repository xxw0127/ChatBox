# Android OpenSSL 3

Qt 6.8 Android 的 `QSslSocket` 需要随包提供 OpenSSL 3。这里放的是 arm64-v8a 预编译库，来自：

- 仓库：`https://github.com/KDAB/android_openssl`
- 分支：`master`
- 参考提交：`b71f1470962019bd89534a2919f5925f93bc5779`
- 路径：`ssl_3/arm64-v8a/`

文件校验：

```text
libssl_3.so     SHA-256 01D2BD0BAAC626EFD3309F35F99C4B826DD9B885A7E4D14D5B12B3603D3A407F
libcrypto_3.so  SHA-256 1E6C12AE0C2DADFE9D178D7F80F0AB248A1877A234066098BF5594E5205E5740
```

许可证：OpenSSL 3 使用 Apache License 2.0；分发 APK 时需要保留 OpenSSL 的许可证与版权声明。

`CMakeLists.txt` 通过 `QT_ANDROID_EXTRA_LIBS` 把这两个库打进 APK 的 `lib/arm64-v8a/`。如果升级 Qt 或 OpenSSL，重新核对 Android 16 的 16 KiB page size 对齐（LOAD align 0x4000）。
