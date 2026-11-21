param([string]$GameDir)
$ErrorActionPreference = 'Stop'
Write-Host 'MAFIA III F8 / AMD FSR DEVELOPMENT v9' -ForegroundColor Yellow
Write-Host 'This is an in-game fullscreen TAA diagnostic. FSR remains unavailable until real frame data passes validation.'
if (-not $GameDir) { $GameDir=Read-Host 'Enter your Mafia III Definitive Edition game folder' }
if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw 'Folder does not exist' }
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
$manifest=Join-Path $GameDir 'MafiaF8_install.json'
if (Test-Path -LiteralPath $manifest) {
  Write-Host 'Existing F8 beta detected. Running verified upgrade to v9.'
  & (Join-Path $PSScriptRoot 'upgrade_v9.ps1') -GameDir $GameDir
} else {
  Write-Host 'Original Modern Tweaks install detected (no beta manifest).'
  & (Join-Path $PSScriptRoot 'install_beta.ps1') -GameDir $GameDir
}
