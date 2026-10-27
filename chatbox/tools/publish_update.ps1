param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Notes = "ChatBox update",
    [string]$Server = "ubuntu@121.5.168.189",
    [string]$ClientRelease = ""
)

$ErrorActionPreference = "Stop"

if ($Version -notmatch '^[0-9]+(\.[0-9]+){1,3}$') {
    throw "invalid version: $Version"
}
if ([string]::IsNullOrWhiteSpace($Server)) {
    throw "server must not be empty"
}

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrWhiteSpace($ClientRelease)) {
    $ClientRelease = Join-Path $root "client\release\chatbox-$Version-win64.zip"
}
if (-not (Test-Path $ClientRelease -PathType Leaf)) {
    throw "client ZIP not found: $ClientRelease"
}
$zipName = Split-Path -Leaf $ClientRelease
if ($zipName -notmatch '^[A-Za-z0-9._-]+\.zip$') {
    throw "unsafe ZIP file name: $zipName"
}
$hash = (Get-FileHash $ClientRelease -Algorithm SHA256).Hash.ToLower()

$publishDir = Join-Path $root "release\update-$Version"
New-Item -ItemType Directory -Force -Path $publishDir | Out-Null
$manifestPath = Join-Path $publishDir "latest.json"
$manifest = [ordered]@{
    schema = 1
    version = $Version
    url = "https://121.5.168.189/updates/$zipName"
    sha256 = $hash
    notes = $Notes
    published_at = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
}
$manifestJson = $manifest | ConvertTo-Json -Depth 3
# Write without a BOM; the client tolerates one, but a clean UTF-8 file is
# friendlier to curl/tools and makes remote byte comparisons predictable.
[IO.File]::WriteAllText($manifestPath, $manifestJson + "`n",
    (New-Object Text.UTF8Encoding($false)))

$remote = "/tmp/chatbox-update-$Version"
ssh $Server "rm -rf '$remote'; mkdir -p '$remote'"
if ($LASTEXITCODE -ne 0) {
    throw "remote preparation failed (exit $LASTEXITCODE)"
}
scp -q $ClientRelease "${Server}:$remote/$zipName"
if ($LASTEXITCODE -ne 0) {
    throw "ZIP upload failed (exit $LASTEXITCODE)"
}
scp -q $manifestPath "${Server}:$remote/latest.json"
if ($LASTEXITCODE -ne 0) {
    throw "manifest upload failed (exit $LASTEXITCODE)"
}

$remoteScript = @"
set -euo pipefail
sudo mkdir -p /var/www/chatbox-updates/releases/$Version
sudo install -m 644 '$remote/$zipName' /var/www/chatbox-updates/$zipName
sudo install -m 644 '$remote/$zipName' /var/www/chatbox-updates/releases/$Version/$zipName
sudo install -m 644 '$remote/latest.json' /var/www/chatbox-updates/releases/$Version/manifest.json
sudo install -m 644 '$remote/latest.json' /var/www/chatbox-updates/latest.json.tmp
sudo mv /var/www/chatbox-updates/latest.json.tmp /var/www/chatbox-updates/latest.json
cd /var/www/chatbox-updates
ls -t chatbox-*-win64.zip 2>/dev/null | tail -n +4 | while IFS= read -r old; do
    sudo rm -f -- "`$old"
done
sudo rm -rf '$remote'
sudo sha256sum /var/www/chatbox-updates/$zipName | cut -d' ' -f1
"@
# Write LF explicitly: PowerShell's default text pipeline can add CR, which
# breaks bash scripts and cut expressions on the Linux side.
$remoteScriptPath = Join-Path $publishDir "publish-$Version.sh"
$remoteScriptLf = $remoteScript -replace "`r`n", "`n"
[IO.File]::WriteAllText($remoteScriptPath, $remoteScriptLf,
    (New-Object Text.UTF8Encoding($false)))
$remoteScriptName = "chatbox-publish-$Version.sh"
scp -q $remoteScriptPath "${Server}:/tmp/$remoteScriptName"
if ($LASTEXITCODE -ne 0) {
    throw "remote script upload failed (exit $LASTEXITCODE)"
}
$remoteOutput = @(ssh $Server "bash /tmp/$remoteScriptName")
if ($LASTEXITCODE -ne 0) {
    throw "remote publish failed (exit $LASTEXITCODE)"
}
ssh $Server "rm -f /tmp/$remoteScriptName" | Out-Null
$remoteHash = ($remoteOutput | Select-Object -Last 1).Trim().ToLower()
if ($remoteHash -ne $hash) {
    throw "remote SHA-256 mismatch: expected $hash, got $remoteHash"
}

$remoteJson = ssh $Server "cat /var/www/chatbox-updates/latest.json"
if ($LASTEXITCODE -ne 0) {
    throw "remote manifest read-back failed (exit $LASTEXITCODE)"
}
$published = ($remoteJson -join "`n") | ConvertFrom-Json
if ($published.version -ne $Version -or $published.sha256 -ne $hash) {
    throw "remote manifest does not match the published version/hash"
}

Write-Host "Published $Version"
Write-Host "manifest: https://121.5.168.189/updates/latest.json"
Write-Host "sha256: $hash"
