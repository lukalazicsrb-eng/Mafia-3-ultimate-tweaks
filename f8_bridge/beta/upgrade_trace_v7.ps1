param([string]$GameDir)
$ErrorActionPreference = 'Stop'
# Only changes the read-only tracer DLL. Original F8, DirectInput proxy and
# original Modern Tweaks DLL remain intact.
$shaV5 = '7bd1e9d097cf671326d6cdd02600a2e2f303bc9ac9c8aaa2fdb460bcbd077a53'
$shaV6 = 'd0d035aaf7397202775913a5dedc9da385d57a42bb348166b0f344df8ffe146d'
$shaOriginal = '529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900'
Write-Host 'MAFIA III V7 – DIRECT DrawIndexed TAA DIAGNOSTIC (NO FSR RENDERING)' -ForegroundColor Yellow
if (-not $GameDir) { $GameDir = Read-Host 'Enter your Mafia III game folder' }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw "Game folder not found: $GameDir" }
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
if (Get-Process -Name 'mafia3definitiveedition' -ErrorAction SilentlyContinue) {
    throw 'Close Mafia III before replacing diagnostic DLLs.'
}
$active = Join-Path $GameDir 'mafia_taa_trace.dll'
$from = Join-Path $PSScriptRoot 'mafia_taa_trace.dll'
$backup = Join-Path $GameDir 'mafia_taa_trace.pre_v7.bak'
$original = Join-Path $GameDir 'ModernTweaks_original.dll'
$originalBackup = Join-Path $GameDir 'dinput8.ModernTweaks.2.5.1.bak'
$manifest = Join-Path $GameDir 'MafiaF8_install.json'
$config = Join-Path $GameDir 'MafiaF8_test.ini'
$proxy = Join-Path $GameDir 'dinput8.dll'
foreach ($file in @($active,$from,$original,$originalBackup,$manifest,$config,$proxy)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing file: $file" }
}
if (Test-Path -LiteralPath $backup) { throw "Previous backup exists: $backup" }
foreach ($file in @($original,$originalBackup)) {
    $h = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($h -ne $shaOriginal) { throw "Original Modern Tweaks DLL hash mismatch: $file" }
}
$m = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($m.originalSha256 -ne $shaOriginal) { throw 'Unknown beta installer manifest.' }
$proxyHash = (Get-FileHash -LiteralPath $proxy -Algorithm SHA256).Hash.ToLowerInvariant()
if ($proxyHash -ne $m.proxySha256) { throw 'Current dinput8.dll proxy does not match manifest.' }
if (-not (Select-String -LiteralPath $config -SimpleMatch 'EnableTaaTrace=1' -Quiet)) {
    throw 'Existing beta is not configured to run TAA trace.'
}
$oldHash = (Get-FileHash -LiteralPath $active -Algorithm SHA256).Hash.ToLowerInvariant()
if ($oldHash -notin @($shaV5,$shaV6)) {
    throw "Tracer is neither known v5 nor v6; no changes made. SHA256: $oldHash"
}
$newHash = (Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash.ToLowerInvariant()
if ($newHash -eq $oldHash) { throw 'The supplied tracer is the same as the installed one.' }
$confirm = Read-Host 'Replace only the existing v5/v6 tracer with experimental v7? Type V7'
if ($confirm -cne 'V7') { Write-Host 'Cancelled; no files changed.'; exit 0 }
try {
    Copy-Item -LiteralPath $active -Destination $backup -ErrorAction Stop
    Copy-Item -LiteralPath $from -Destination $active -Force -ErrorAction Stop
    $installedHash=(Get-FileHash -LiteralPath $active -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($installedHash -ne $newHash) { throw 'Tracer hash mismatch after copy.' }
    Remove-Item -LiteralPath $backup -Force -ErrorAction Stop
    Write-Host 'V7 installed; existing F8 hook and original Modern Tweaks unchanged.' -ForegroundColor Green
    Write-Host 'No test is required now. The tracer runs only when Mafia III is launched through Steam.'
} catch {
    if (Test-Path -LiteralPath $backup) {
        Copy-Item -LiteralPath $backup -Destination $active -Force
        Remove-Item -LiteralPath $backup -Force
    }
    throw
}
