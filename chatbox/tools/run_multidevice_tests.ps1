param(
    [string]$ServerHost = "127.0.0.1",
    [int]$Port = 19100
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$tests = @(
    "test_multidevice_auth.py",
    "test_multidevice_fanout.py",
    "test_multidevice_read_echo.py",
    "test_multidevice_sync.py",
    "test_multidevice_caps.py"
)

Push-Location $here
try {
    foreach ($test in $tests) {
        Write-Output "== $test =="
        python $test --host $ServerHost --port $Port
        if ($LASTEXITCODE -ne 0) {
            throw "$test failed"
        }
    }
} finally {
    Pop-Location
}
Write-Output "all multi-device tests passed"
