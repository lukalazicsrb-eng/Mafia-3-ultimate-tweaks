param([string]$GameDir)
$ErrorActionPreference = 'Stop'
$expectedMod = '529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900'
$expectedV5Trace = '7bd1e9d097cf671326d6cdd02600a2e2f303bc9ac9c8aaa2fdb460bcbd077a53'
Write-Host 'Mafia III V6 TAA TRACE UPGRADE -- NOT WORKING FSR' -ForegroundColor Yellow
if (-not $GameDir) { $GameDir = Read-Host 'Enter your Mafia III game folder' }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw 'Game folder not found.' }
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
if (Get-Process -Name 'mafia3definitiveedition' -ErrorAction SilentlyContinue) {
    throw 'Close Mafia III before replacing its diagnostic DLL.'
}
$old = Join-Path $GameDir 'mafia_taa_trace.dll'
$src = Join-Path $PSScriptRoot 'mafia_taa_trace.dll'
$backup = Join-Path $GameDir 'mafia_taa_trace.v5.bak'
$mod = Join-Path $GameDir 'ModernTweaks_original.dll'
$originalBackup = Join-Path $GameDir 'dinput8.ModernTweaks.2.5.1.bak'
$manifest = Join-Path $GameDir 'MafiaF8_install.json'
$activeProxy = Join-Path $GameDir 'dinput8.dll'
foreach ($p in @($src, $mod, $originalBackup, $manifest, $activeProxy)) {
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Missing: $p" }
}
foreach ($p in @($mod, $originalBackup)) {
    if ((Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedMod) {
        throw "Incorrect original Modern Tweaks v2.5.1 DLL: $p"
    }
}
$record = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($record.originalSha256 -ne $expectedMod) { throw 'Unknown prior beta manifest.' }
$nowProxyHash = (Get-FileHash -LiteralPath $activeProxy -Algorithm SHA256).Hash.ToLowerInvariant()
if ($nowProxyHash -ne $record.proxySha256) { throw 'Current proxy is different from the beta manifest.' }

if (-not (Test-Path -LiteralPath $old)) {
    Write-Host 'No existing v5 tracer. Trying earlier v4.1 -> trace installation.'
    & (Join-Path $PSScriptRoot 'upgrade_trace.ps1') -GameDir $GameDir
    exit $LASTEXITCODE
}
if (Test-Path -LiteralPath $backup) { throw "Old temporary backup exists: $backup" }
$oldHash = (Get-FileHash -LiteralPath $old -Algorithm SHA256).Hash.ToLowerInvariant()
if ($oldHash -ne $expectedV5Trace) { throw "Tracer does not match known v5. Refusing to replace: $oldHash" }
$confirm = Read-Host 'Replace ONLY the known v5 tracer DLL with v6? Type V6'
if ($confirm -cne 'V6') { Write-Host 'Cancelled.'; exit 0 }
try {
    Copy-Item -LiteralPath $old -Destination $backup -ErrorAction Stop
    Copy-Item -LiteralPath $src -Destination $old -Force -ErrorAction Stop
    $newHash = (Get-FileHash -LiteralPath $old -Algorithm SHA256).Hash.ToLowerInvariant()
    $sourceHash = (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($newHash -ne $sourceHash) { throw 'V6 tracer copy hash mismatch.' }
    Remove-Item -LiteralPath $backup -Force
    Write-Host 'V6 TAA tracer installed; start Mafia III using Steam.' -ForegroundColor Green
    Write-Host 'Set F8 Visuals -> AA to Game TAA, move the camera in gameplay.'
    Write-Host 'Send MafiaTaaTrace.log and MafiaF8_loader.log from the game folder.'
} catch {
    if (Test-Path -LiteralPath $backup) {
        Copy-Item -LiteralPath $backup -Destination $old -Force
        Remove-Item -LiteralPath $backup -Force
    }
    throw
}
