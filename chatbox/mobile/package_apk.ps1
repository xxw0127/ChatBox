param(
    [string]$QtRoot = "D:\Qt",
    [string]$QtVersion = "6.8.3",
    [ValidateSet("arm64-v8a", "x86_64")]
    [string]$Abi = "arm64-v8a",
    [ValidateSet("Debug", "Release")]
    [string]$BuildType = "Release",
    [string]$VersionName = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

$keystore = $env:CHATBOX_ANDROID_KEYSTORE
$alias = $env:CHATBOX_ANDROID_KEY_ALIAS
$storePassword = $env:CHATBOX_ANDROID_KEYSTORE_PASSWORD
$keyPassword = $env:CHATBOX_ANDROID_KEY_PASSWORD
if (-not $keyPassword) { $keyPassword = $storePassword }
# apksigner reads --key-pass from the environment; make the fallback visible.
if (-not $env:CHATBOX_ANDROID_KEY_PASSWORD) {
    $env:CHATBOX_ANDROID_KEY_PASSWORD = $keyPassword
}

if (-not $keystore -or -not (Test-Path $keystore)) {
    throw "CHATBOX_ANDROID_KEYSTORE must point to an existing keystore"
}
if (-not $alias) { throw "CHATBOX_ANDROID_KEY_ALIAS is required" }
if (-not $storePassword) { throw "CHATBOX_ANDROID_KEYSTORE_PASSWORD is required" }

$sdk = $env:ANDROID_SDK_ROOT
if (-not $sdk) { $sdk = $env:ANDROID_HOME }
if (-not $sdk -or -not (Test-Path $sdk)) { throw "ANDROID_SDK_ROOT/ANDROID_HOME is required" }

$buildTools = Get-ChildItem (Join-Path $sdk "build-tools") -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $buildTools) { throw "Android build-tools not found" }
$apksigner = Join-Path $buildTools.FullName "apksigner.bat"
$zipalign = Join-Path $buildTools.FullName "zipalign.exe"
if (-not (Test-Path $apksigner)) { throw "apksigner not found: $apksigner" }

if ($VersionName -eq "") {
    $cmake = Get-Content (Join-Path $here "CMakeLists.txt") -Raw
    if ($cmake -match 'QT_ANDROID_VERSION_NAME\s+"([^"]+)"') {
        $VersionName = $matches[1]
    } else {
        $VersionName = "0.1.0"
    }
}
if ($OutputDir -eq "") {
    $OutputDir = Join-Path $here "release"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

& (Join-Path $here "build_android.ps1") -QtRoot $QtRoot -QtVersion $QtVersion `
    -Abi $Abi -BuildType $BuildType
if ($LASTEXITCODE -ne 0) { throw "Android build failed" }

$apkDir = Join-Path $here ("build-android-" + $Abi + "\android-build\build\outputs\apk\" + $BuildType.ToLower())
$apk = Get-ChildItem -LiteralPath $apkDir -Filter *.apk -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $apk) { throw "APK not found under $apkDir" }

$outName = "chatbox-$VersionName-$Abi.apk"
$outApk = Join-Path $OutputDir $outName
if (Test-Path $outApk) { Remove-Item -Force $outApk }

& $apksigner sign --ks $keystore --ks-key-alias $alias `
    "--ks-pass" "env:CHATBOX_ANDROID_KEYSTORE_PASSWORD" `
    "--key-pass" "env:CHATBOX_ANDROID_KEY_PASSWORD" `
    "--out" $outApk $apk.FullName
if ($LASTEXITCODE -ne 0) { throw "apksigner sign failed" }

$verify = & $apksigner verify --print-certs $outApk 2>&1
if ($LASTEXITCODE -ne 0) { throw "apksigner verify failed: $verify" }
$verify | Write-Output

$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $outApk).Hash.ToLower()
$hashFile = Join-Path $OutputDir ("chatbox-" + $VersionName + "-" + $Abi + ".sha256")
"$hash  $outName" | Set-Content -LiteralPath $hashFile -Encoding ascii

Write-Output ("APK: " + $outApk)
Write-Output ("SHA256: " + $hash)
Write-Output ("SHA256 file: " + $hashFile)
