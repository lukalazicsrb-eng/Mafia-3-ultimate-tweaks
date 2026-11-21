param([string]$GameDir)
$ErrorActionPreference = 'Stop'
Write-Host 'Mafia III: FSR DEVELOPMENT - TAA RESOURCE DIAGNOSTIC (NOT REAL FSR)' -ForegroundColor Yellow
if (-not $GameDir) {
  $GameDir = Read-Host 'Enter Mafia III folder where v4.1 F8 beta already works'
}
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw 'Game folder not found.' }
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
$active = Join-Path $GameDir 'dinput8.dll'
$original = Join-Path $GameDir 'ModernTweaks_original.dll'
$backup = Join-Path $GameDir 'dinput8.ModernTweaks.2.5.1.bak'
$manifest = Join-Path $GameDir 'MafiaF8_install.json'
$config = Join-Path $GameDir 'MafiaF8_test.ini'
$traceDst = Join-Path $GameDir 'mafia_taa_trace.dll'
$traceSrc = Join-Path $PSScriptRoot 'mafia_taa_trace.dll'
$proxySrc = Join-Path $PSScriptRoot 'dinput8.dll'
$temp = Join-Path $GameDir 'dinput8.before-TAA-trace.bak'
$expected = '529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900'
foreach ($p in @($active,$original,$backup,$manifest,$config,$traceSrc,$proxySrc)) {
  if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Required file missing: $p" }
}
foreach ($p in @($traceDst,$temp)) {
  if (Test-Path -LiteralPath $p) { throw "Unexpected existing file, refusing to overwrite: $p" }
}
$m = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($m.originalSha256 -ne $expected) { throw 'Original manifest SHA mismatch.' }
foreach ($p in @($original,$backup)) {
  if ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw "Original Modern Tweaks SHA mismatch: $p"
  }
}
$currentHash = (Get-FileHash -LiteralPath $active -Algorithm SHA256).Hash.ToLowerInvariant()
if ($currentHash -ne $m.proxySha256) { throw 'Active beta proxy does not match manifest. No changes made.' }
$confirm = Read-Host 'This changes only the existing reversible beta loader and adds an experimental TAA resource hook. Type TRACE'
if ($confirm -cne 'TRACE') { Write-Host 'Cancelled.'; exit 0 }
$previousManifest = Get-Content -LiteralPath $manifest -Raw
$previousConfig = Get-Content -LiteralPath $config -Raw
try {
  Copy-Item -LiteralPath $active -Destination $temp -ErrorAction Stop
  Copy-Item -LiteralPath $proxySrc -Destination $active -Force -ErrorAction Stop
  Copy-Item -LiteralPath $traceSrc -Destination $traceDst -ErrorAction Stop
  $updated = @($previousConfig -split '\r?\n' | Where-Object { $_.Trim().Length -gt 0 -and $_ -ne 'EnableTaaTrace=1' })
  $updated += 'EnableTaaTrace=1'
  Set-Content -LiteralPath $config -Value $updated -Encoding Ascii -ErrorAction Stop
  $m.proxySha256 = (Get-FileHash -LiteralPath $active -Algorithm SHA256).Hash.ToLowerInvariant()
  $m | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8 -ErrorAction Stop
  Write-Host 'Read-only TAA probe installed. Start game THROUGH STEAM.' -ForegroundColor Green
  Write-Host 'Play for 30 seconds and send MafiaTaaTrace.log and MafiaF8_loader.log.'
  Remove-Item -LiteralPath $temp -Force
} catch {
  if (Test-Path -LiteralPath $temp) {
    Copy-Item -LiteralPath $temp -Destination $active -Force
    Remove-Item -LiteralPath $temp -Force
  }
  if (Test-Path -LiteralPath $traceDst) { Remove-Item -LiteralPath $traceDst -Force }
  Set-Content -LiteralPath $config -Value $previousConfig -NoNewline -Encoding Ascii
  Set-Content -LiteralPath $manifest -Value $previousManifest -NoNewline -Encoding UTF8
  throw
}
