param(
    [Parameter(Mandatory = $true)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$TargetDir,
    [Parameter(Mandatory = $true)][string]$Executable
)

$ErrorActionPreference = 'Stop'
$result = Join-Path $env:TEMP 'ChatBox\update-result.txt'
$marker = Join-Path $env:TEMP 'ChatBox\update-in-progress'

function Write-Result([string]$message) {
    New-Item -ItemType Directory -Force -Path (Split-Path $result) | Out-Null
    Set-Content -Path $result -Value $message -Encoding UTF8
}

function Copy-Mirror([string]$source, [string]$target) {
    & robocopy $source $target /MIR /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "robocopy failed with exit code $LASTEXITCODE"
    }
}

try {
    if (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) {
        Wait-Process -Id $ProcessId -Timeout 30 -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 1

    $backup = Join-Path $env:TEMP ("ChatBox\backup-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path (Split-Path $backup) | Out-Null
    Copy-Item -LiteralPath $TargetDir -Destination $backup -Recurse -Force
    New-Item -ItemType Directory -Force -Path (Split-Path $marker) | Out-Null
    Set-Content -Path $marker -Value '1' -Encoding ASCII

    Copy-Mirror $SourceDir $TargetDir
    $new_process = Start-Process -FilePath $Executable -WorkingDirectory $TargetDir -PassThru
    Start-Sleep -Seconds 5
    if ($new_process.HasExited) {
        throw '新版本启动后立即退出'
    }

    Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $result -Force -ErrorAction SilentlyContinue
    exit 0
} catch {
    $message = $_.Exception.Message
    Write-Result $message
    if (Test-Path (Join-Path $env:TEMP 'ChatBox')) {
        $backups = Get-ChildItem (Join-Path $env:TEMP 'ChatBox') -Directory -Filter 'backup-*' |
            Sort-Object LastWriteTime -Descending
        if ($backups.Count -gt 0) {
            try {
                Copy-Mirror $backups[0].FullName $TargetDir
            } catch {
                Write-Result ($message + '；回滚失败: ' + $_.Exception.Message)
            }
        }
    }
    try {
        Start-Process -FilePath $Executable -WorkingDirectory $TargetDir | Out-Null
    } catch {
        Write-Result ($message + '；无法重新启动旧版本')
    }
    exit 1
}
