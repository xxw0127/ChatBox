$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$schema = Get-Content (Join-Path $root "..\deploy\schema.sql") -Raw
$migration = Get-Content (Join-Path $root "..\deploy\migrate_20261029_files.sql") -Raw
$required = @("files", "file_uploads", "sha256", "thumb_available", "expires_at", "file_id")
foreach ($name in $required) {
    if (-not $schema.Contains($name)) { throw "schema.sql is missing $name" }
    if (-not $migration.Contains($name)) { throw "migration is missing $name" }
}
if (-not $schema.Contains("idx_files_room_status")) {
    throw "schema.sql is missing the room file index"
}
Write-Host "file schema guard: OK"
