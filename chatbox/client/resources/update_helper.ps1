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
    # Diagnostics must never block rollback: a locked result file or a full
    # disk here would otherwise abort the catch block before recovery.
    try {
        New-Item -ItemType Directory -Force -Path (Split-Path $result) | Out-Null
        Set-Content -Path $result -Value $message -Encoding UTF8
    } catch {
    }
}

function Copy-Mirror([string]$source, [string]$target) {
    & robocopy $source $target /MIR /NFL /NDL /NJH /NJS /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) {
        throw "robocopy failed with exit code $LASTEXITCODE"
    }
}

$backup = $null
$backup_complete = $false
try {
    if (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) {
        Wait-Process -Id $ProcessId -Timeout 30 -ErrorAction SilentlyContinue
        if (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue) {
            throw 'old client did not exit within 30 seconds'
        }
    }
    Start-Sleep -Seconds 1

    $backup = Join-Path $env:TEMP ("ChatBox\backup-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Force -Path (Split-Path $backup) | Out-Null
    Copy-Item -LiteralPath $TargetDir -Destination $backup -Recurse -Force
    if (-not (Test-Path -LiteralPath (Join-Path $backup 'chatbox.exe'))) {
        throw 'backup copy did not produce a complete client'
    }
    $backup_complete = $true
    New-Item -ItemType Directory -Force -Path (Split-Path $marker) | Out-Null
    Set-Content -Path $marker -Value '1' -Encoding ASCII

    Copy-Mirror $SourceDir $TargetDir
    $new_process = Start-Process -FilePath $Executable -WorkingDirectory $TargetDir -PassThru
    Start-Sleep -Seconds 5
    if ($new_process.HasExited) {
        throw 'new version exited during startup check'
    }

    Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $result -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $SourceDir -Recurse -Force -ErrorAction SilentlyContinue
    exit 0
} catch {
    $message = $_.Exception.Message
    Write-Result $message
    # Only restore a snapshot this run completed. A partial backup copied over
    # the target would delete the remaining old files and destroy the install.
    if ($backup_complete -and (Test-Path -LiteralPath $backup)) {
        try {
            Copy-Mirror $backup $TargetDir
            Remove-Item -LiteralPath $marker -Force -ErrorAction SilentlyContinue
        } catch {
            Write-Result ($message + ' ; rollback failed: ' + $_.Exception.Message)
        }
    }
    try {
        Start-Process -FilePath $Executable -WorkingDirectory $TargetDir | Out-Null
    } catch {
        Write-Result ($message + ' ; could not restart the old version')
    }
    exit 1
}
