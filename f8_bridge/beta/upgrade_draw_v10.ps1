param([string]$GameDir)
$ErrorActionPreference='Stop'
Write-Host 'Mafia III v10: read-only all-Draw TAA profiler (not yet FSR gameplay)' -ForegroundColor Yellow
if(-not $GameDir) { $GameDir=Read-Host 'Enter the Mafia III game folder' }
if(-not(Test-Path -LiteralPath $GameDir -PathType Container)) { throw 'Game folder does not exist' }
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
if(Get-Process -Name 'mafia3definitiveedition' -ErrorAction SilentlyContinue) {
  throw 'Close Mafia III completely before replacing the diagnostic DLL.'
}
$expectedMod='529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900'
$expectedV9='6d3cdac2c7e40126dd70c30d1eb2459d102a33b999493f8fba0575a00cbd4c56'
$manifest=Join-Path $GameDir 'MafiaF8_install.json'
$activeProxy=Join-Path $GameDir 'dinput8.dll'
$original=Join-Path $GameDir 'ModernTweaks_original.dll'
$originalBackup=Join-Path $GameDir 'dinput8.ModernTweaks.2.5.1.bak'
$target=Join-Path $GameDir 'mafia_fsr_gamepass.dll'
$source=Join-Path $PSScriptRoot 'mafia_fsr_gamepass.dll'
$config=Join-Path $GameDir 'MafiaF8_test.ini'
$temp=Join-Path $GameDir 'mafia_fsr_gamepass.v9.recovery.bak'
foreach($p in @($manifest,$activeProxy,$original,$originalBackup,$target,$source,$config)) {
  if(-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Missing required beta file: $p" }
}
if(Test-Path -LiteralPath $temp) { throw 'Old recovery backup exists. Do not overwrite it.' }
$m=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if($m.originalSha256 -ne $expectedMod) { throw 'Unrecognized beta manifest.' }
if(($m.installVersion -ne 'v9') -and ($null -ne $m.installVersion)) {
  throw 'Existing beta version must be v9.'
}
foreach($p in @($original,$originalBackup)) {
  if((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedMod) {
    throw "Original Modern Tweaks hash mismatch: $p"
  }
}
if((Get-FileHash -LiteralPath $activeProxy -Algorithm SHA256).Hash.ToLowerInvariant() -ne $m.proxySha256) {
  throw 'Active dinput8.dll has changed; do not overwrite.'
}
$prev=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
if($prev -ne $expectedV9) {
  throw "Unrecognized current gamepass DLL. No change made. SHA256=$prev"
}
if(-not (Select-String -LiteralPath $config -SimpleMatch 'EnableFsrDrawTrace=1' -Quiet)) {
  throw 'F8 DirectDraw tracing is not enabled in the existing beta config.'
}
$confirm=Read-Host 'Update only the v9 diagnostic DLL to v10? Type V10'
if($confirm -cne 'V10') { Write-Host 'Cancelled; no changes made.'; exit 0 }
try {
  Copy-Item -LiteralPath $target -Destination $temp -ErrorAction Stop
  Copy-Item -LiteralPath $source -Destination $target -Force -ErrorAction Stop
  $new=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
  if($new -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()) {
    throw 'New gamepass DLL verification failed.'
  }
  Remove-Item -LiteralPath $temp -Force -ErrorAction Stop
  Write-Host 'v10 profiler installed. Original F8, proxy and AMD backend unchanged.' -ForegroundColor Green
  Write-Host 'Launch through Steam. No AMD FSR mode will be enabled by this diagnostic.'
} catch {
  if(Test-Path -LiteralPath $temp) {
    Copy-Item -LiteralPath $temp -Destination $target -Force
    Remove-Item -LiteralPath $temp -Force
  }
  throw
}
