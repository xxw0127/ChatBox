param(
    [string]$QtRoot = "D:\Qt",
    [string]$QtVersion = "6.8.3",
    [ValidateSet("arm64-v8a", "x86_64")]
    [string]$Abi = "arm64-v8a",
    [ValidateSet("Debug", "Release")]
    [string]$BuildType = "Debug",
    [int]$Api = 28,
    [switch]$Install
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

$kitAbi = if ($Abi -eq "arm64-v8a") { "android_arm64_v8a" } else { "android_x86_64" }
$kitRoot = Join-Path $QtRoot $QtVersion
$qtCmake = Join-Path $kitRoot "$kitAbi\bin\qt-cmake.bat"
if (-not (Test-Path $qtCmake)) {
    throw "qt-cmake not found: $qtCmake. Install Qt $QtVersion Android $kitAbi first."
}

if (-not $env:JAVA_HOME) {
    $jdkCandidates = @(
        "C:\Program Files\Java\latest\jdk-17",
        "C:\Program Files\Eclipse Adoptium\jdk-17*",
        "C:\Program Files\Java\latest\jdk-21"
    )
    foreach ($candidate in $jdkCandidates) {
        $resolved = Get-ChildItem $candidate -Directory -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if ($resolved) { $env:JAVA_HOME = $resolved; break }
    }
}
if (-not $env:JAVA_HOME) {
    throw "JAVA_HOME is not set and no JDK was found."
}

if (-not $env:ANDROID_SDK_ROOT) {
    foreach ($candidate in @("$env:LOCALAPPDATA\Android\Sdk", "C:\Android\Sdk", "D:\Android\Sdk")) {
        if (Test-Path $candidate) { $env:ANDROID_SDK_ROOT = $candidate; break }
    }
}
if (-not $env:ANDROID_SDK_ROOT) {
    throw "ANDROID_SDK_ROOT is not set and no Android SDK was found."
}
$env:ANDROID_HOME = $env:ANDROID_SDK_ROOT

if (-not $env:ANDROID_NDK_ROOT) {
    $ndk = Get-ChildItem (Join-Path $env:ANDROID_SDK_ROOT "ndk") -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($ndk) { $env:ANDROID_NDK_ROOT = $ndk.FullName }
}
if (-not $env:ANDROID_NDK_ROOT) {
    throw "ANDROID_NDK_ROOT is not set and no NDK was found under the SDK."
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if (-not $cmake) {
    $cmakeCandidates = @(
        "$QtRoot\Tools\CMake_64\bin\cmake.exe",
        "$QtRoot\Tools\CMake\bin\cmake.exe"
    )
    foreach ($candidate in $cmakeCandidates) {
        if (Test-Path $candidate) { $cmake = $candidate; break }
    }
}
if (-not $cmake) { throw "cmake not found. Install Qt Tools CMake or add cmake to PATH." }
$cmakeExe = if ($cmake -is [string]) { $cmake } else { $cmake.Source }

$buildDir = Join-Path $here "build-android-$Abi"
$hostQt = Join-Path $QtRoot "$QtVersion\mingw_64"
if (-not (Test-Path $hostQt)) {
    throw "Host Qt not found: $hostQt. Install the desktop Qt $QtVersion kit first."
}
$ninja = Join-Path $QtRoot "Tools\Ninja\ninja.exe"
$generatorArgs = @()
if (Test-Path $ninja) {
    $generatorArgs = @("-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=$ninja")
}
& $qtCmake -S $here -B $buildDir @generatorArgs `
    "-DQT_HOST_PATH=$hostQt" `
    "-DANDROID_ABI=$Abi" `
    "-DANDROID_PLATFORM=android-$Api" `
    "-DCMAKE_BUILD_TYPE=$BuildType"
if ($LASTEXITCODE -ne 0) { throw "qt-cmake configure failed" }

$wrapper = Join-Path $buildDir "android-build\gradle\wrapper\gradle-wrapper.properties"
if (Test-Path $wrapper) {
    $lines = Get-Content $wrapper
    $lines = $lines | ForEach-Object {
        if ($_ -like "distributionUrl=*") {
            "distributionUrl=https\://mirrors.cloud.tencent.com/gradle/gradle-8.10-bin.zip"
        } else {
            $_
        }
    }
    $lines | Set-Content $wrapper -Encoding ascii
}

& $cmakeExe --build $buildDir --target apk -j 8
if ($LASTEXITCODE -ne 0) { throw "APK build failed" }

$apkDir = Join-Path $buildDir "android-build\build\outputs\apk"
$apk = Get-ChildItem $apkDir -Recurse -Filter *.apk -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $apk) { throw "APK not found under $apkDir" }

Write-Output "APK: $($apk.FullName)"

if ($Install) {
    $adb = Get-Command adb -ErrorAction SilentlyContinue
    if (-not $adb) {
        $adbPath = Join-Path $env:ANDROID_SDK_ROOT "platform-tools\adb.exe"
        if (Test-Path $adbPath) { $adb = $adbPath } else { throw "adb not found." }
    }
    $adbExe = if ($adb -is [string]) { $adb } else { $adb.Source }
    & $adbExe install -r $apk.FullName
    if ($LASTEXITCODE -ne 0) { throw "adb install failed" }
    & $adbExe shell am start -n com.chatbox.mobile/org.qtproject.qt.android.bindings.QtActivity
}
