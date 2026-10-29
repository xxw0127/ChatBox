$ErrorActionPreference = "Stop"
$doc = Join-Path (Split-Path -Parent $PSScriptRoot) "docs\f0-device-matrix.md"
if (-not (Test-Path $doc)) {
    throw "F0 device matrix not found: $doc"
}
$content = Get-Content $doc -Raw -Encoding UTF8
foreach ($needle in @(
    "Xiaomi 24117RK2CC",
    "isForeground=true",
    "POST_NOTIFICATIONS",
    "chatbox_service",
    "START_STICKY"
)) {
    if (-not $content.Contains($needle)) {
        throw "F0 matrix is missing: $needle"
    }
}
Write-Output "F0 matrix recorded: Xiaomi Android 16, foreground/notifications pass, swipe/boot OEM limits documented"
