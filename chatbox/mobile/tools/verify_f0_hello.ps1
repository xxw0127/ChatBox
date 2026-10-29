param(
    [string]$Apk = ""
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$mobile = Split-Path -Parent $here
if (-not $Apk) {
    $Apk = Join-Path $mobile "build-android-arm64-v8a\android-build\build\outputs\apk\debug\android-build-debug.apk"
}
if (-not (Test-Path $Apk)) {
    throw "APK not found: $Apk"
}

$aapt = "D:\Android\Sdk\build-tools\35.0.0\aapt2.exe"
$badging = & $aapt dump badging $Apk
if (-not ($badging -match "package: name='com\.chatbox\.mobile'")) {
    throw "unexpected package name"
}
if (-not ($badging -match "minSdkVersion:'28'")) {
    throw "unexpected minSdkVersion"
}
if (-not ($badging -match "application-label:'ChatBox'")) {
    throw "unexpected application label"
}

$adb = "D:\Android\Sdk\platform-tools\adb.exe"
$devices = & $adb devices
if (-not ($devices -match "\sdevice\s*$")) {
    throw "no authorized Android device attached"
}
$path = & $adb shell pm path com.chatbox.mobile
if (-not ($path -match "package:")) {
    throw "com.chatbox.mobile is not installed"
}

Write-Output "F0 hello APK installed: package=com.chatbox.mobile minSdk=28 label=ChatBox device=authorized"
