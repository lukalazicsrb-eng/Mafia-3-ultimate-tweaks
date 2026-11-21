param([string]$GameDir)
$ErrorActionPreference = 'Stop'
Write-Host 'Mafia III Luma FSR4 — restore original files' -ForegroundColor Yellow
if (-not $GameDir) { $GameDir = Read-Host 'Enter Mafia III game folder' }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw 'Game folder missing' }
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
if (Get-Process -Name 'mafia3definitiveedition' -ErrorAction SilentlyContinue) {
  throw 'Close Mafia III completely before restoration.'
}
$manifestPath=Join-Path $GameDir 'MafiaFSR4_Luma_Install.json'
$backupDir=Join-Path $GameDir 'MafiaFSR4_Luma_Preinstall_Backup'
if (-not(Test-Path -LiteralPath $manifestPath -PathType Leaf) -or
    -not(Test-Path -LiteralPath $backupDir -PathType Container)) {
  throw 'Luma FSR4 install manifest or backup missing. No changes made.'
}
$m=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($m.schema -ne 1 -or $m.package -ne 'Luma Mafia III with AMD FSR 4.1.2 signed runtime') {
  throw 'Unrecognized manifest. No changes made.'
}
$records=@($m.files)
if ($records.Count -lt 7) { throw 'Manifest is incomplete.' }

# Preflight all current files and backups before touching anything.
$rootPrefix=$GameDir.TrimEnd('\')+'\'
foreach ($row in $records) {
  if ([string]::IsNullOrWhiteSpace($row.rel) -or [IO.Path]::IsPathRooted($row.rel)) {
    throw 'Untrusted path in manifest.'
  }
  $dest=[IO.Path]::GetFullPath((Join-Path $GameDir $row.rel))
  if (-not $dest.StartsWith($rootPrefix,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Manifest contains path outside game folder.'
  }
  if (-not(Test-Path -LiteralPath $dest -PathType Leaf)) {
    throw "Installed file missing: $dest. Restore it manually or use a backup."
  }
  $current=(Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($current -ne $row.installedHash) {
    throw "File was changed after FSR4 install: $dest. Refusing to overwrite user changes."
  }
  if ($row.existed) {
    $saved=Join-Path $backupDir $row.rel
    if (-not(Test-Path -LiteralPath $saved -PathType Leaf)) {
      throw "Missing original backup: $saved"
    }
    $savedHash=(Get-FileHash -LiteralPath $saved -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($savedHash -ne $row.previousHash) { throw "Backup changed: $saved" }
  }
}
$confirm=Read-Host 'Restore files to their exact pre-FSR4 state? Type RESTORE'
if ($confirm -cne 'RESTORE') { Write-Host 'Cancelled.'; exit 0 }
[Array]::Reverse($records)
foreach ($row in $records) {
  $dest=Join-Path $GameDir $row.rel
  if ($row.existed) {
    $saved=Join-Path $backupDir $row.rel
    Copy-Item -LiteralPath $saved -Destination $dest -Force -ErrorAction Stop
  } else {
    Remove-Item -LiteralPath $dest -Force -ErrorAction Stop
  }
}
Remove-Item -LiteralPath $manifestPath -Force -ErrorAction Stop
# Keep the backup directory instead of discarding it automatically.
Write-Host 'Original files restored; backup folder was preserved for safety.' -ForegroundColor Green
Write-Host "Backup: $backupDir"
