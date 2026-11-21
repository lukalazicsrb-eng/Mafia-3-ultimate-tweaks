param([string]$GameDir)

$ErrorActionPreference = 'Stop'
$originalModernTweaksSha256 = '529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900'
Write-Host 'MAFIA III — Experimental FSR 4.1.2 / Luma Mafia III' -ForegroundColor Yellow
Write-Host 'This rebuild enables Luma FSR4, but actual gameplay and Modern Tweaks coexistence are not yet verified.' -ForegroundColor Yellow

if (-not $GameDir) { $GameDir = Read-Host 'Enter Mafia III game folder containing mafia3definitiveedition.exe' }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw "Game folder does not exist: $GameDir" }
$GameDir = (Resolve-Path -LiteralPath $GameDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'mafia3definitiveedition.exe'))) {
  throw 'The specified folder does not contain mafia3definitiveedition.exe.'
}
if (Get-Process -Name 'mafia3definitiveedition' -ErrorAction SilentlyContinue) {
  throw 'Exit Mafia III completely before installation.'
}
$existingManifest = Join-Path $GameDir 'MafiaFSR4_Luma_Install.json'
$backupDir = Join-Path $GameDir 'MafiaFSR4_Luma_Preinstall_Backup'
if (Test-Path -LiteralPath $existingManifest) { throw 'FSR4 Luma package already installed. Uninstall first.' }
if (Test-Path -LiteralPath $backupDir) { throw 'Existing FSR4 backup folder. Do not overwrite it.' }
if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'BUILD-MANIFEST.json'))) {
  throw 'Missing signed build manifest; extract the WHOLE ZIP outside the game directory.'
}

# This is a separate ReShade addon, not an updated Modern Tweaks dinput8.dll.
# If the game's dinput8.dll is our earlier experimental F8 proxy, restore the
# VERIFIED original Modern Tweaks to avoid two experimental DirectX hooks.
$betaManifest = Join-Path $GameDir 'MafiaF8_install.json'
$restoreBetaProxy = $false
$dinput = Join-Path $GameDir 'dinput8.dll'
$originalBackup = Join-Path $GameDir 'dinput8.ModernTweaks.2.5.1.bak'
if (Test-Path -LiteralPath $betaManifest) {
  $beta = Get-Content -LiteralPath $betaManifest -Raw | ConvertFrom-Json
  if ($beta.originalSha256 -ne $originalModernTweaksSha256) {
    throw 'Unknown earlier F8 beta manifest. Refusing to change DirectInput chain.'
  }
  foreach ($p in @($dinput, $originalBackup)) {
    if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { throw "Missing earlier F8 beta component: $p" }
  }
  $active = (Get-FileHash -LiteralPath $dinput -Algorithm SHA256).Hash.ToLowerInvariant()
  $saved = (Get-FileHash -LiteralPath $originalBackup -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($active -ne $beta.proxySha256 -or $saved -ne $originalModernTweaksSha256) {
    throw 'Earlier beta DLL files differ from recorded hashes; no automatic replacement.'
  }
  $restoreBetaProxy = $true
  Write-Host 'Earlier F8 beta detected: its experimental proxy will be temporarily replaced by the verified ORIGINAL Modern Tweaks.' -ForegroundColor Yellow
}

$files = @()
$names = @('dxgi.dll','Luma-Mafia III.addon','amd_fidelityfx_loader_dx12.dll',
           'amd_fidelityfx_upscaler_dx12.dll','nvngx_dlss.dll')
foreach ($name in $names) {
  $p = Join-Path $PSScriptRoot $name
  if (Test-Path -LiteralPath $p -PathType Leaf) {
    $files += [pscustomobject]@{ src = $p; rel = $name }
  } elseif ($name -ne 'nvngx_dlss.dll') {
    throw "Required DLL/addon missing from ZIP: $name"
  }
}
$shaderRoot = Join-Path $PSScriptRoot 'Luma'
if (-not (Test-Path -LiteralPath $shaderRoot -PathType Container)) { throw 'Luma shaders missing.' }
foreach ($file in Get-ChildItem -LiteralPath $shaderRoot -File -Recurse) {
  $rel = 'Luma\' + $file.FullName.Substring($shaderRoot.Length).TrimStart('\')
  $files += [pscustomobject]@{ src = $file.FullName; rel = $rel }
}
if ($files.Count -lt 7) { throw 'Missing expected Luma files; refusing partial install.' }
$confirm = Read-Host 'Back up existing files and install Luma FSR4 addon for testing? Type FSR4'
if ($confirm -cne 'FSR4') { Write-Host 'Cancelled. No files changed.'; exit 0 }

New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
$records = @()
$allSuccess = $false
try {
  if ($restoreBetaProxy) {
    $oldRelative = 'dinput8.dll'
    Copy-Item -LiteralPath $dinput -Destination (Join-Path $backupDir $oldRelative)
    $oldHash = (Get-FileHash -LiteralPath $dinput -Algorithm SHA256).Hash.ToLowerInvariant()
    Copy-Item -LiteralPath $originalBackup -Destination $dinput -Force
    $installedHash = (Get-FileHash -LiteralPath $dinput -Algorithm SHA256).Hash.ToLowerInvariant()
    $records += [pscustomobject]@{rel=$oldRelative;existed=$true;previousHash=$oldHash;installedHash=$installedHash}
  }
  foreach ($file in $files) {
    $src = $file.src
    $rel = $file.rel
    $dest = Join-Path $GameDir $rel
    $existed = Test-Path -LiteralPath $dest -PathType Leaf
    $prevHash = ''
    if ($existed) {
      $prevHash = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
      $savedFile = Join-Path $backupDir $rel
      $parent = Split-Path -Parent $savedFile
      New-Item -ItemType Directory -Path $parent -Force | Out-Null
      Copy-Item -LiteralPath $dest -Destination $savedFile -Force
    }
    $records += [pscustomobject]@{rel=$rel;existed=$existed;previousHash=$prevHash;installedHash=''}
    New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
    Copy-Item -LiteralPath $src -Destination $dest -Force
    $records[-1].installedHash = (Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant()
  }
  $manifest = [pscustomobject]@{
    schema = 1
    createdUtc = [DateTime]::UtcNow.ToString('o')
    restoredOldF8Proxy = $restoreBetaProxy
    package = 'Luma Mafia III with AMD FSR 4.1.2 signed runtime'
    files = $records
  }
  $manifest | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath $existingManifest -Encoding UTF8
  $allSuccess = $true
} finally {
  if (-not $allSuccess) {
    Write-Host 'Installation interrupted; restoring existing files.' -ForegroundColor Yellow
    foreach ($row in @($records)[-1..-($records.Count)] ) { }
    [array]::Reverse($records)
    foreach ($row in $records) {
      $dst=Join-Path $GameDir $row.rel
      if ($row.existed) {
        $savedFile=Join-Path $backupDir $row.rel
        if (Test-Path -LiteralPath $savedFile) { Copy-Item -LiteralPath $savedFile -Destination $dst -Force }
      } else {
        if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }
      }
    }
    Write-Host "Backup was left in $backupDir for inspection."
  }
}

Write-Host 'Luma FSR4 installed. Start Mafia III through Steam.' -ForegroundColor Green
Write-Host 'Press HOME to open the Luma/ReShade menu and select an FSR4 option if available.'
Write-Host 'Modern Tweaks F8 remains its original menu; it does NOT control this separate FSR4 addon.'
Write-Host 'If the game crashes or FSR4 is unavailable, exit and run Vrati pre instalacije.cmd.'
