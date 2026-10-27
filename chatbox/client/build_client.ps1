param(
    [string]$QtDir = "D:\Qt\5.15.2\mingw81_64",
    [string]$MingwDir = "D:\Qt\Tools\mingw810_64",
    [string]$OpenSSLDir = "C:\Program Files\OpenSSL-Win64"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root "build"
$release = Join-Path $build "release"

if (-not (Test-Path (Join-Path $QtDir "bin\qmake.exe"))) {
    throw "qmake not found under $QtDir"
}
if (-not (Test-Path (Join-Path $MingwDir "bin\mingw32-make.exe"))) {
    throw "mingw32-make not found under $MingwDir"
}

$env:PATH = "$QtDir\bin;$MingwDir\bin;$OpenSSLDir\bin;$env:PATH"
New-Item -ItemType Directory -Force -Path $build | Out-Null

Push-Location $build
try {
    & (Join-Path $QtDir "bin\qmake.exe") "..\chatbox.pro"
    if ($LASTEXITCODE -ne 0) {
        throw "qmake failed"
    }
    & (Join-Path $MingwDir "bin\mingw32-make.exe") -f Makefile.Release -j8
    if ($LASTEXITCODE -ne 0) {
        throw "make failed"
    }
} finally {
    Pop-Location
}

New-Item -ItemType Directory -Force -Path $release | Out-Null
foreach ($name in @("Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll", "Qt5Sql.dll", "Qt5Network.dll")) {
    Copy-Item (Join-Path $QtDir "bin\$name") $release -Force
}
foreach ($name in @("libssl-4-x64.dll", "libcrypto-4-x64.dll")) {
    Copy-Item (Join-Path $OpenSSLDir "bin\$name") $release -Force
}
$vcRuntime = Join-Path $env:SystemRoot "System32\vcruntime140.dll"
if (Test-Path $vcRuntime) {
    Copy-Item $vcRuntime $release -Force
} else {
    Write-Warning "vcruntime140.dll not found; OpenSSL DLLs may fail to load on a clean machine"
}
foreach ($name in @("libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll")) {
    Copy-Item (Join-Path $MingwDir "bin\$name") $release -Force
}

$certs = Join-Path $root "certs"
if (Test-Path $certs) {
    Copy-Item $certs $release -Recurse -Force
}

$platforms = Join-Path $release "platforms"
New-Item -ItemType Directory -Force -Path $platforms | Out-Null
Copy-Item (Join-Path $QtDir "plugins\platforms\qwindows.dll") $platforms -Force

$imageformats = Join-Path $release "imageformats"
New-Item -ItemType Directory -Force -Path $imageformats | Out-Null
$jpeg_plugin = Join-Path $QtDir "plugins\imageformats\qjpeg.dll"
if (Test-Path $jpeg_plugin) {
    Copy-Item $jpeg_plugin $imageformats -Force
} else {
    Write-Warning "qjpeg.dll not found; JPEG avatars may not render"
}

# The window and tray icons load resources/app.ico through Qt, so the ICO
# plugin has to ship as well (only the exe resource works without it).
$ico_plugin = Join-Path $QtDir "plugins\imageformats\qico.dll"
if (Test-Path $ico_plugin) {
    Copy-Item $ico_plugin $imageformats -Force
} else {
    Write-Warning "qico.dll not found; window/tray icons may not render on machines without Qt installed"
}

$styles = Join-Path $release "styles"
New-Item -ItemType Directory -Force -Path $styles | Out-Null
$style_plugin = Join-Path $QtDir "plugins\styles\qwindowsvistastyle.dll"
if (Test-Path $style_plugin) {
    Copy-Item $style_plugin $styles -Force
}

# The local history store needs the Qt SQL module and its SQLite driver
# plugin; without them the portable build cannot open history.sqlite.
$sqlDrivers = Join-Path $release "sqldrivers"
New-Item -ItemType Directory -Force -Path $sqlDrivers | Out-Null
$sqlite_plugin = Join-Path $QtDir "plugins\sqldrivers\qsqlite.dll"
if (Test-Path $sqlite_plugin) {
    Copy-Item $sqlite_plugin $sqlDrivers -Force
} else {
    Write-Warning "qsqlite.dll not found; the local history store will not work in this build"
}

Write-Host "Build complete: $release\chatbox.exe"
