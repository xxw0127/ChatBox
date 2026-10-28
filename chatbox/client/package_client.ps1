param(
    [string]$Version = "0.3.0",
    [string]$QtDir = "D:\Qt\5.15.2\mingw81_64",
    [string]$MingwDir = "D:\Qt\Tools\mingw810_64",
    [string]$OpenSSLDir = "C:\Program Files\OpenSSL-Win64"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $root "build_client.ps1") -QtDir $QtDir -MingwDir $MingwDir -OpenSSLDir $OpenSSLDir
if ($LASTEXITCODE -ne 0) { throw "build_client.ps1 failed" }

$release = Join-Path $root "build\release"
# Publish directly to release; build\release is only the linker output and dist
# would otherwise keep a second full copy of every package.
$publishRoot = Join-Path $root "release"
$name = "chatbox-$Version"
$target = Join-Path $publishRoot "$name-win64"
$zip = Join-Path $publishRoot "$name-win64.zip"
if (Test-Path $target) { Remove-Item $target -Recurse -Force }
New-Item -ItemType Directory -Force -Path $publishRoot | Out-Null
New-Item -ItemType Directory -Force -Path $target | Out-Null

Copy-Item (Join-Path $release "chatbox.exe") $target -Force
Copy-Item (Join-Path $release "*.dll") $target -Force
foreach ($directory in @("certs", "platforms", "styles", "imageformats", "sqldrivers")) {
    $source = Join-Path $release $directory
    if (Test-Path $source) {
        Copy-Item $source $target -Recurse -Force
    }
}
Copy-Item (Join-Path $root "README.md") $target -Force

# A package missing the platform plugin, TLS stack, CA, SQL module or SQLite
# driver would start (or fail) in ways the updater cannot recover from, so
# refuse to ship it.
foreach ($required in @(
    "chatbox.exe", "Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll",
    "Qt5Sql.dll", "Qt5Network.dll", "platforms\qwindows.dll",
    "sqldrivers\qsqlite.dll", "certs\ca.crt", "imageformats\qjpeg.dll"
)) {
    $path = Join-Path $target $required
    if (-not (Test-Path $path -PathType Leaf)) {
        throw "package is incomplete: $required is missing from $target"
    }
}
foreach ($pattern in @("libssl-*.dll", "libcrypto-*.dll")) {
    if (-not (Get-ChildItem -Path $target -Filter $pattern -File | Select-Object -First 1)) {
        throw "package is incomplete: $pattern is missing from $target"
    }
}

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $target -DestinationPath $zip -CompressionLevel Optimal

Write-Host "Release published: $zip"
Write-Host "Release folder: $target"
