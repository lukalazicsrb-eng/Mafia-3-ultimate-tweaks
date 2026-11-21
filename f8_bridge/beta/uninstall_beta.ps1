param([string]$GameDir)
$ErrorActionPreference = 'Stop'
if (-not $GameDir) {
  $GameDir = Read-Host 'Enter the Mafia III game folder used for the beta installation'
}
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
$original = Join-Path $GameDir 'dinput8.dll'
$backup = Join-Path $GameDir 'dinput8.ModernTweaks.2.5.1.bak'
$manifest = Join-Path $GameDir 'MafiaF8_install.json'
if (-not (Test-Path -LiteralPath $manifest)) { throw 'No beta install manifest; do not guess which DLL to restore.' }
if (-not (Test-Path -LiteralPath $backup)) { throw 'Backup missing. Cannot restore safely.' }
$record = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
$backupHash = (Get-FileHash -LiteralPath $backup -Algorithm SHA256).Hash.ToLowerInvariant()
if ($backupHash -ne $record.originalSha256) { throw 'Backup SHA mismatch; no changes made.' }
if (Test-Path -LiteralPath $original) {
  $nowHash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($nowHash -ne $record.proxySha256) { throw 'The active dinput8.dll was modified; refusing to overwrite it.' }
}
Copy-Item -LiteralPath $backup -Destination $original -Force
foreach ($p in @('ModernTweaks_original.dll','mafia_f8_experimental.dll','mafia_taa_trace.dll','mafia_fsr_gamepass.dll','mafia_live_fsr_backend.dll','MafiaF8_test.ini',
                 'dinput8.ModernTweaks.2.5.1.bak','MafiaF8_install.json')) {
  $target = Join-Path $GameDir $p
  if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
}
Write-Host 'Original Modern Tweaks restored. MafiaF8_loader.log was retained for diagnosis.' -ForegroundColor Green
