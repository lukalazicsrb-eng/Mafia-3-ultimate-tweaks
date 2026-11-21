param([string]$GameDir)
$ErrorActionPreference='Stop'
$expected='529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900'
Write-Host 'Mafia III v9: upgrading known F8 beta safely (FSR not enabled yet)' -ForegroundColor Yellow
if (-not $GameDir) { $GameDir=Read-Host 'Enter Mafia III game folder' }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw 'Game folder missing' }
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
if (Get-Process -Name 'mafia3definitiveedition' -ErrorAction SilentlyContinue) {
  throw 'Close Mafia III completely before changing any DLLs.'
}
$manifest=Join-Path $GameDir 'MafiaF8_install.json'
$backup=Join-Path $GameDir 'dinput8.ModernTweaks.2.5.1.bak'
$orig=Join-Path $GameDir 'ModernTweaks_original.dll'
$active=Join-Path $GameDir 'dinput8.dll'
foreach ($file in @($manifest,$backup,$orig,$active)) {
  if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Existing F8 beta file missing: $file" }
}
$previous=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($previous.originalSha256 -ne $expected) { throw 'Existing beta is not a recognized Modern Tweaks 2.5.1 install.' }
foreach ($p in @($backup,$orig)) {
  if ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw "Original Modern Tweaks backup is invalid: $p"
  }
}
$activeHash=(Get-FileHash -LiteralPath $active -Algorithm SHA256).Hash.ToLowerInvariant()
if ($activeHash -ne $previous.proxySha256) {
  throw 'Active dinput8.dll differs from original beta manifest. No changes made.'
}
$replace=@('dinput8.dll','mafia_f8_experimental.dll','mafia_taa_trace.dll',
           'MafiaF8_test.ini','MafiaF8_install.json')
$new=@('mafia_fsr_gamepass.dll','mafia_live_fsr_backend.dll')
foreach ($name in $new) {
  if (Test-Path -LiteralPath (Join-Path $GameDir $name)) {
    throw "Refusing to overwrite unexpected existing file: $name"
  }
}
foreach ($name in @('dinput8.dll','mafia_f8_experimental.dll','mafia_taa_trace.dll') + $new) {
  $from=Join-Path $PSScriptRoot $name
  if (-not (Test-Path -LiteralPath $from -PathType Leaf)) {
    throw "Package is missing a required compiled DLL: $from"
  }
}
# Fail if a stale temporary backup already exists; do not touch it.
foreach ($name in $replace) {
  $temp=Join-Path $GameDir ($name+'.pre_v9.bak')
  if (Test-Path -LiteralPath $temp) { throw "Temporary backup already exists: $temp" }
}
$confirm=Read-Host 'Upgrade verified existing beta to v9? Type V9'
if ($confirm -cne 'V9') { Write-Host 'Cancelled. No files changed.';exit 0 }
$backed=@()
$created=@()
try {
  foreach ($name in $replace) {
    $path=Join-Path $GameDir $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
      $tmp=$path+'.pre_v9.bak'
      Copy-Item -LiteralPath $path -Destination $tmp -ErrorAction Stop
      $backed+=@($path,$tmp)
    }
  }
  foreach ($name in @('dinput8.dll','mafia_f8_experimental.dll','mafia_taa_trace.dll') + $new) {
    $from=Join-Path $PSScriptRoot $name
    $dest=Join-Path $GameDir $name
    Copy-Item -LiteralPath $from -Destination $dest -Force -ErrorAction Stop
    if ($name -in $new) { $created+=$dest }
  }
  $config=Join-Path $GameDir 'MafiaF8_test.ini'
  Set-Content -LiteralPath $config -Encoding Ascii -Value @(
    'EnableExperimentalF8=1','EnableTaaTrace=0','EnableFsrDrawTrace=1') -ErrorAction Stop
  $newProxyHash=(Get-FileHash -LiteralPath $active -Algorithm SHA256).Hash.ToLowerInvariant()
  @{originalSha256=$expected;proxySha256=$newProxyHash;installVersion='v9'} |
    ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8 -ErrorAction Stop
  foreach ($name in $replace) {
    $temp=Join-Path $GameDir ($name+'.pre_v9.bak')
    if (Test-Path -LiteralPath $temp) { Remove-Item -LiteralPath $temp -Force }
  }
  Write-Host 'v9 installed. Original Modern Tweaks backup retained.' -ForegroundColor Green
  Write-Host 'Start Mafia III through Steam, play 30 seconds, then send MafiaFsrDraw.log and MafiaF8_loader.log.'
} catch {
  foreach ($pair in $backed) {
    # Backed is a sequence of absolute file path / temporary path pairs.
  }
  for ($i=0; $i -lt $backed.Count; $i+=2) {
    Copy-Item -LiteralPath $backed[$i+1] -Destination $backed[$i] -Force
    Remove-Item -LiteralPath $backed[$i+1] -Force
  }
  foreach ($p in $created) {
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
  }
  throw
}
