param([string]$GameDir)
$ErrorActionPreference='Stop'
Write-Host 'MAFIA III — FSR v10 Draw profiler (not functional in-game FSR yet)' -ForegroundColor Yellow
if(-not $GameDir) { $GameDir=Read-Host 'Enter your Mafia III game folder' }
if(-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw 'Game folder not found' }
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
$manifest=Join-Path $GameDir 'MafiaF8_install.json'
if(Test-Path -LiteralPath $manifest -PathType Leaf) {
  $previous=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
  $gamepass=Join-Path $GameDir 'mafia_fsr_gamepass.dll'
  if($previous.installVersion -eq 'v9' -and (Test-Path -LiteralPath $gamepass)) {
    & (Join-Path $PSScriptRoot 'upgrade_draw_v10.ps1') -GameDir $GameDir
  } else {
    # Older F8 beta, no gamepass installed yet.
    & (Join-Path $PSScriptRoot 'upgrade_v9.ps1') -GameDir $GameDir
  }
} else {
  & (Join-Path $PSScriptRoot 'install_beta.ps1') -GameDir $GameDir
}
