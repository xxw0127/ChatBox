param(
    [string]$Server = "127.0.0.1",
    [int]$Port = 19100
)

# Prerequisite: a fresh MySQL database created from deploy/schema.sql with the
# test server running on $Port. The migration itself is exercised separately
# against a copy of the previous schema (see docs/superpowers/specs).
$ErrorActionPreference = "Stop"
Write-Host "Running group acceptance against ${Server}:${Port}"
& (Join-Path $PSScriptRoot "test_groups_integration.ps1") -Server $Server -Port $Port
if ($LASTEXITCODE -ne 0) {
    throw "group acceptance failed"
}
Write-Host "group acceptance: OK"
