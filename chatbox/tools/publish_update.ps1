param(
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$Notes = "ChatBox update",
    [string]$Server = "ubuntu@121.5.168.189",
    [string]$ClientRelease = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if ([string]::IsNullOrWhiteSpace($ClientRelease)) {
    $ClientRelease = Join-Path $root "client\release\chatbox-$Version-win64.zip"
}
if (-not (Test-Path $ClientRelease)) {
    throw "client ZIP not found: $ClientRelease"
}
$zipName = Split-Path -Leaf $ClientRelease
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
$manifest | ConvertTo-Json -Depth 3 | Set-Content -Path $manifestPath -Encoding UTF8

$remote = "/tmp/chatbox-update-$Version"
ssh $Server "rm -rf '$remote'; mkdir -p '$remote'"
scp -q $ClientRelease "${Server}:$remote/$zipName"
scp -q $manifestPath "${Server}:$remote/latest.json"
$remoteScript = @"
set -e
sudo mkdir -p /var/www/chatbox-updates
sudo install -m 644 '$remote/$zipName' /var/www/chatbox-updates/$zipName
sudo install -m 644 '$remote/latest.json' /var/www/chatbox-updates/latest.json
# Keep the immediately previous ZIP for rollback/manual recovery.
sudo find /var/www/chatbox-updates -maxdepth 1 -type f -name 'chatbox-*-win64.zip' -printf '%T@ %p\n' |
  sort -nr | tail -n +4 | cut -d' ' -f2- | xargs -r sudo rm -f
rm -rf '$remote'
"@
$remoteScript | ssh $Server "bash -s"
Write-Host "Published $Version"
Write-Host "manifest: https://121.5.168.189/updates/latest.json"
Write-Host "sha256: $hash"
