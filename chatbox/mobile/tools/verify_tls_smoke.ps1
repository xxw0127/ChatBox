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

& $Adb shell am start -n $activity | Out-Null
Start-Sleep -Seconds 2

$nodes = Get-UiNodes
$button = $nodes | Where-Object { $_.'resource-id' -like "*Button_QMLTYPE*" } | Select-Object -First 1
if (-not $button) {
    $button = $nodes | Where-Object { $_.clickable -eq "true" } | Select-Object -First 1
}
if (-not $button) {
    throw "connect-test button not found in UI hierarchy"
}
if ($button.bounds -notmatch "\[(\d+),(\d+)\]\[(\d+),(\d+)\]") {
    throw "cannot parse button bounds: $($button.bounds)"
}
$x = [int](([int]$matches[1] + [int]$matches[3]) / 2)
$y = [int](([int]$matches[2] + [int]$matches[4]) / 2)
& $Adb shell input tap $x $y | Out-Null

for ($i = 0; $i -lt 10; ++$i) {
    Start-Sleep -Seconds 1
    $nodes = Get-UiNodes
    $pong = $nodes | Where-Object { $_.'content-desc' -eq "PONG received" } | Select-Object -First 1
    if ($pong) {
        Write-Output "TLS smoke passed: PONG received"
        exit 0
    }
}
throw "TLS smoke failed: PONG not observed within 10 seconds"

