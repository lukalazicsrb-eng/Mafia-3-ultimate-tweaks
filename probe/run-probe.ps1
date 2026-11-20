[CmdletBinding()]
param([switch]$PrepareOnly)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = $PSScriptRoot
$runtime = Join-Path $root 'runtime\amd'
$d3d12Directory = Join-Path $root 'D3D12'
$resultPath = Join-Path $root 'FSR rezultat.txt'
$amdArchiveName = 'FidelityFX-SDK-2.4.0.zip'
$amdBaseUrl = 'https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/7539ae8b6568ad29ed78a753404351daf3b5cca7/Kits/FidelityFX/signedbin/'
$agilityArchiveName = 'microsoft.direct3d.d3d12.1.616.1.nupkg'
$agilityUrl = 'https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/1.616.1/microsoft.direct3d.d3d12.1.616.1.nupkg'
$agilityArchiveHash = '3d8f4af714947c8b64b5656f48deb08661d6a5f743d97c63766065bd633ad52e'
$agilityCoreHash = '9e47f236fd3244ad88443a61f7d549eced052b42cb9bd78bbeb35a5918c25bb9'
$amdFiles = [ordered]@{
    'amd_fidelityfx_loader_dx12.dll' = 'e2191d3c0828be45eb8affd4e3ec0378f2293adc345b772a7170edcf3b021ecb'
    'amd_fidelityfx_upscaler_dx12.dll' = 'aad4fccccb29084a655f9a4f9f613bd22c8c03fb4a0f8840148cc6543b4ccace'
}

$report = [System.Collections.Generic.List[string]]::new()
$report.Add('Provera AMD FSR na Windows x64')
$report.Add((Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz'))
$report.Add('Ovaj test obradjuje sinteticke frejmove izvan igre.')
$report.Add('')

function Write-Status([string]$Message) {
    Write-Host $Message
    $report.Add($Message)
}

function Test-Sha256([string]$Path, [string]$Expected) {
    return (Test-Path -LiteralPath $Path -PathType Leaf) -and
        ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Expected)
}

function Assert-Sha256([string]$Path, [string]$Expected) {
    if (-not (Test-Sha256 $Path $Expected)) {
        throw "SHA-256 provera nije prosla za $([IO.Path]::GetFileName($Path))."
    }
}

function Expand-ExactEntry([string]$Path, [string]$EntryName, [string]$Destination) {
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entry = $archive.GetEntry($EntryName)
        if ($null -eq $entry) {
            throw "U arhivi nedostaje $EntryName."
        }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $Destination, $true)
    } finally {
        $archive.Dispose()
    }
}

function Receive-File([string]$Url, [string]$Destination) {
    $ProgressPreference = 'SilentlyContinue'
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor
        [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing -TimeoutSec 120
}

function Get-LocalArchives([string[]]$Names) {
    $directories = @($root)
    if ($env:USERPROFILE) {
        $directories += Join-Path $env:USERPROFILE 'Downloads'
    }
    foreach ($directory in $directories) {
        foreach ($name in $Names) {
            $path = Join-Path $directory $name
            if (Test-Path -LiteralPath $path -PathType Leaf) {
                $path
            }
        }
    }
}

$exitCode = 1
$scratch = $null
try {
    if (-not [Environment]::Is64BitOperatingSystem) {
        throw 'Za test je potreban Windows x64.'
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $scratch = Join-Path $root ('.fsr-priprema-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $scratch, $runtime, $d3d12Directory -Force | Out-Null

    $amdArchives = @(Get-LocalArchives @($amdArchiveName))
    foreach ($name in $amdFiles.Keys) {
        $expected = $amdFiles[$name]
        $destination = Join-Path $runtime $name
        if (-not (Test-Sha256 $destination $expected)) {
            $staged = Join-Path $scratch $name
            $foundLocalCopy = $false
            foreach ($archive in $amdArchives) {
                try {
                    Expand-ExactEntry $archive "FidelityFX-SDK-2.4.0/Kits/FidelityFX/signedbin/$name" $staged
                    Assert-Sha256 $staged $expected
                    $foundLocalCopy = $true
                    break
                } catch {
                    Write-Status "Lokalna AMD arhiva nije upotrebljiva za $name. $($_.Exception.Message)"
                }
            }
            if (-not $foundLocalCopy) {
                Write-Status "Preuzimam $name sa zvanicnog AMD GitHub repozitorijuma."
                Receive-File ($amdBaseUrl + $name) $staged
                Assert-Sha256 $staged $expected
            }
            Move-Item -LiteralPath $staged -Destination $destination -Force
        }
        Assert-Sha256 $destination $expected
        $report.Add("SHA-256 $name $expected")
    }

    $core = Join-Path $d3d12Directory 'D3D12Core.dll'
    if (-not (Test-Sha256 $core $agilityCoreHash)) {
        $localAgility = @(Get-LocalArchives @($agilityArchiveName, 'microsoft.direct3d.d3d12.1.616.1.zip'))
        $verifiedArchives = @($localAgility | Where-Object { Test-Sha256 $_ $agilityArchiveHash })
        if ($verifiedArchives.Count -ne 0) {
            $agilityArchive = $verifiedArchives[0]
        } else {
            Write-Status 'Preuzimam zvanicni DirectX 12 Agility SDK 1.616.1 sa NuGet-a.'
            $agilityArchive = Join-Path $scratch $agilityArchiveName
            Receive-File $agilityUrl $agilityArchive
        }
        Assert-Sha256 $agilityArchive $agilityArchiveHash
        $staged = Join-Path $scratch 'D3D12Core.dll'
        Expand-ExactEntry $agilityArchive 'build/native/bin/x64/D3D12Core.dll' $staged
        Assert-Sha256 $staged $agilityCoreHash
        Move-Item -LiteralPath $staged -Destination $core -Force
    }
    Assert-Sha256 $core $agilityCoreHash
    $report.Add("SHA-256 D3D12Core.dll $agilityCoreHash")
    Write-Status 'AMD SDK 2.4.0 i DirectX 12 Agility 1.616.1 su spremni.'

    if ($PrepareOnly) {
        $exitCode = 0
    } else {
        $probe = Join-Path $root 'fsr-interop-probe.exe'
        if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) {
            throw 'U ovom direktorijumu nedostaje fsr-interop-probe.exe. Raspakuj ceo paket.'
        }

        $systemDirectory = [Environment]::SystemDirectory
        if (-not [Environment]::Is64BitProcess) {
            $systemDirectory = Join-Path $env:WINDIR 'Sysnative'
        }
        $crt = 'MSVCP140.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1.dll'
        $missing = @($crt | Where-Object { -not (Test-Path -LiteralPath (Join-Path $systemDirectory $_) -PathType Leaf) })
        if ($missing.Count -ne 0) {
            Write-Status ('Nedostaju Microsoft Visual C++ x64 biblioteke: ' + ($missing -join ', '))
            Write-Status 'Zvanicni Microsoft instalacioni paket: https://aka.ms/vc14/vc_redist.x64.exe'
        }

        Write-Status 'Pokrecem GPU proveru. Ovo moze potrajati.'
        $stdout = Join-Path $scratch 'stdout.txt'
        $stderr = Join-Path $scratch 'stderr.txt'
        $process = Start-Process -FilePath $probe -ArgumentList ('"' + $runtime + '"') -WorkingDirectory $root `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr -NoNewWindow -Wait -PassThru
        $exitCode = $process.ExitCode
        $output = Get-Content -LiteralPath $stdout -Raw
        $errors = Get-Content -LiteralPath $stderr -Raw
        $report.Add('')
        $report.Add("Izlazni kod programa: $exitCode")
        if ($output) {
            $report.Add($output.TrimEnd())
        }
        if ($errors) {
            $report.Add($errors.TrimEnd())
        }

        $provider = [regex]::Match([string]$output, '(?m)^Selected upscaler\s+([^\r\n]+)')
        $version = [regex]::Match($provider.Groups[1].Value, '\b(\d+\.\d+\.\d+)\b')
        if ($exitCode -ne 0) {
            Write-Status 'GPU provera nije prosla. Detalji su u izvestaju.'
        } elseif (-not $version.Success) {
            Write-Status 'GPU provera je prosla, ali verzija upscalera nije prepoznata.'
        } elseif ([version]$version.Value -ge [version]'4.1.2') {
            Write-Status "GPU provera je prosla. AMD FSR $($version.Value) obradio je sinteticke frejmove."
        } elseif (([version]$version.Value).Major -lt 4) {
            Write-Status "GPU provera je prosla sa analitickim FSR $($version.Value). FSR 4.1.2 nije aktivan."
        } else {
            Write-Status "GPU provera je prosla sa FSR $($version.Value). Potrebna verzija je 4.1.2 ili novija."
        }
    }
} catch {
    $exitCode = 1
    Write-Status "Provera nije pokrenuta ili je prekinuta. $($_.Exception.Message)"
} finally {
    if ($scratch -and (Test-Path -LiteralPath $scratch -PathType Container)) {
        Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (-not $PrepareOnly) {
        try {
            $report | Set-Content -LiteralPath $resultPath -Encoding UTF8
            Write-Host "Rezultat: $resultPath"
        } catch {
            Write-Host "Rezultat nije sacuvan. $($_.Exception.Message)"
            $exitCode = 1
        }
    }
}

exit $exitCode
