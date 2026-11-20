param([string]$GameDir)
$ErrorActionPreference = 'Stop'
$expected = '529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900'
Write-Host 'EXPERIMENTAL F8 MENU BETA - THIS DOES NOT ENABLE AMD FSR' -ForegroundColor Yellow
if (-not $GameDir) {
  $default = 'D:\SteamLibrary\steamapps\common\Mafia III'
  $GameDir = Read-Host "Enter Mafia III game folder (example: $default)"
}
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw 'Game folder does not exist.' }
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
$exe = Join-Path $GameDir 'mafia3definitiveedition.exe'
$original = Join-Path $GameDir 'dinput8.dll'
$backup = Join-Path $GameDir 'dinput8.ModernTweaks.2.5.1.bak'
$renamed = Join-Path $GameDir 'ModernTweaks_original.dll'
$adapter = Join-Path $GameDir 'mafia_f8_experimental.dll'
$config = Join-Path $GameDir 'MafiaF8_test.ini'
$manifest = Join-Path $GameDir 'MafiaF8_install.json'
$proxySrc = Join-Path $PSScriptRoot 'dinput8.dll'
$adapterSrc = Join-Path $PSScriptRoot 'mafia_f8_experimental.dll'
if (-not (Test-Path -LiteralPath $exe)) { throw 'mafia3definitiveedition.exe not found.' }
if (-not (Test-Path -LiteralPath $original)) { throw 'Original Modern Tweaks dinput8.dll not found.' }
foreach ($p in @($backup, $renamed, $adapter, $config, $manifest)) {
  if (Test-Path -LiteralPath $p) { throw "File already exists, refusing to overwrite: $p" }
}
foreach ($p in @($proxySrc, $adapterSrc)) {
  if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Missing compiled beta DLL: $p" }
}
$currentHash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash.ToLowerInvariant()
if ($currentHash -ne $expected) {
  throw "Wrong Modern Tweaks binary; expected exact v2.5.1, found SHA256 $currentHash"
}
Write-Host "Exact original DLL verified: $currentHash"
$answer = Read-Host 'This will temporarily replace dinput8.dll with an UNTESTED experimental proxy. Type TEST to continue'
if ($answer -cne 'TEST') { Write-Host 'Cancelled; no files changed.'; exit 0 }
try {
  Copy-Item -LiteralPath $original -Destination $backup -ErrorAction Stop
  Copy-Item -LiteralPath $original -Destination $renamed -ErrorAction Stop
  Copy-Item -LiteralPath $proxySrc -Destination $original -Force -ErrorAction Stop
  Copy-Item -LiteralPath $adapterSrc -Destination $adapter -ErrorAction Stop
  Set-Content -LiteralPath $config -Value 'EnableExperimentalF8=1' -Encoding Ascii -ErrorAction Stop
  $proxyHash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash.ToLowerInvariant()
  @{ originalSha256=$expected; proxySha256=$proxyHash } |
    ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8 -ErrorAction Stop
  Write-Host 'Beta installed. Launch game THROUGH STEAM (not directly from EXE).' -ForegroundColor Green
  Write-Host 'Open F8 -> Visuals -> AA and see whether four FSR choices appear as [unavailable].'
  Write-Host 'Send MafiaF8_loader.log, ModernTweaks.log, and a screenshot of the menu.'
} catch {
  if (Test-Path -LiteralPath $backup) { Copy-Item -LiteralPath $backup -Destination $original -Force }
  foreach ($p in @($renamed,$adapter,$config,$manifest,$backup)) {
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
  }
  throw
}
