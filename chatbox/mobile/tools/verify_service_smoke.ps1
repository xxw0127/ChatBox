param(
    [string]$Adb = "D:\Android\Sdk\platform-tools\adb.exe"
)

$ErrorActionPreference = "Continue"
$package = "com.chatbox.mobile"
$activity = "com.chatbox.mobile/.ChatBoxActivity"
$remote = "/sdcard/chatbox-ui.xml"
$local = Join-Path $env:TEMP "chatbox-ui.xml"

function Get-UiNodes {
    & $Adb shell uiautomator dump $remote | Out-Null
    & $Adb pull $remote $local 2>&1 | Out-Null
    & $Adb shell rm $remote | Out-Null
    if (-not (Test-Path $local)) {
        throw "failed to pull UI hierarchy"
    }
    [xml]$xml = Get-Content $local -Encoding UTF8
    return $xml.SelectNodes("//node")
}

$path = & $Adb shell pm path $package
if (-not ($path -match "package:")) {
    throw "$package is not installed"
}

& $Adb shell pm grant $package android.permission.POST_NOTIFICATIONS 2>&1 | Out-Null
& $Adb shell am force-stop $package | Out-Null
Start-Sleep -Seconds 1
& $Adb shell am start -n $activity | Out-Null
Start-Sleep -Seconds 5

$services = & $Adb shell dumpsys activity services $package 2>&1
if (-not ($services -match "ChatBoxService")) {
    throw "ChatBoxService is not running"
}
if (-not ($services -match "processName=com\.chatbox\.mobile:qt_service")) {
    throw "ChatBoxService is not in :qt_service"
}
if (-not ($services -match "isForeground=true")) {
    throw "ChatBoxService is not foreground"
}

$notifications = & $Adb shell dumpsys notification --noredact 2>&1
if (-not ($notifications -match "com\.chatbox\.mobile\|1000")) {
    throw "foreground notification 1000 not found"
}
if (-not ($notifications -match "com\.chatbox\.mobile\|1001")) {
    throw "test notification 1001 not found"
}

$nodes = Get-UiNodes
$button = $nodes | Where-Object { $_.'content-desc' -eq "IPC ping" } | Select-Object -First 1
if (-not $button) {
    throw "IPC ping button not found"
}
if ($button.bounds -notmatch "\[(\d+),(\d+)\]\[(\d+),(\d+)\]") {
    throw "cannot parse IPC button bounds: $($button.bounds)"
}
$x = [int](([int]$matches[1] + [int]$matches[3]) / 2)
$y = [int](([int]$matches[2] + [int]$matches[4]) / 2)
& $Adb shell input tap $x $y | Out-Null

for ($i = 0; $i -lt 10; ++$i) {
    Start-Sleep -Seconds 1
    $nodes = Get-UiNodes
    $pong = $nodes | Where-Object { $_.'content-desc' -eq "service: pong" } | Select-Object -First 1
    if ($pong) {
        Write-Output "service smoke passed: foreground + notifications + IPC pong"
        exit 0
    }
}
throw "service smoke failed: IPC pong not observed within 10 seconds"

