$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$schema = Get-Content (Join-Path $root "..\deploy\schema.sql") -Raw
$migration = Get-Content (Join-Path $root "..\deploy\migrate_20261028_groups.sql") -Raw
$required = @(
    "room_members", "room_read_state", "room_join_requests", "room_invites",
    "group_no", "is_default", "join_message_id", "dissolved_at"
)
foreach ($name in $required) {
    if (-not $schema.Contains($name)) {
        throw "schema.sql is missing $name"
    }
    if (-not $migration.Contains($name)) {
        throw "migration is missing $name"
    }
}
if ($schema -match "大厅 2" -or $migration -notmatch "WHERE id <> 1") {
    throw "seed rooms must be reduced to room 1 only"
}
if ($schema -notmatch "UNIQUE KEY uk_group_no") {
    throw "schema.sql must enforce unique group numbers"
}
Write-Host "group schema guard: OK"
