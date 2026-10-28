param(
    [string]$Server = "127.0.0.1",
    [int]$Port = 19100
)

$ErrorActionPreference = "Stop"
$env:CHATBOX_TEST_HOST = $Server
$env:CHATBOX_TEST_PORT = $Port.ToString()
python (Join-Path $PSScriptRoot "test_groups_integration.py")
if ($LASTEXITCODE -ne 0) {
    throw "group integration test failed"
}
