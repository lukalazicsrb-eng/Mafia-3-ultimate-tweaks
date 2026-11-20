[CmdletBinding()]
param(
    [switch]$PrepareOnly,
    [switch]$TestExporter,
    [switch]$AnalyzeExisting,
    [switch]$TraceMod,
    [switch]$TestTraceSettings
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$result = Join-Path $root 'Mafia III rezultat.txt'
$renderDoc = Join-Path $root 'RenderDoc_1.47_64'
$captureCommand = Join-Path $renderDoc 'renderdoccmd.exe'
$captureUI = Join-Path $renderDoc 'qrenderdoc.exe'
$report = [Collections.Generic.List[string]]::new()
$report.Add('Mafia III - D3D11 dijagnostika')
$report.Add('Pokretac mafia-capture-v0.1.3')

function Prepare-RenderDoc {
    if ((Test-Path -LiteralPath $captureCommand -PathType Leaf) -and
        (Test-Path -LiteralPath $captureUI -PathType Leaf)) {
        return
    }

    $stage = Join-Path $root ('.renderdoc-' + [Guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Path $stage | Out-Null
        $zip = Join-Path $stage 'RenderDoc_1.47_64.zip'
        Write-Host 'Preuzimam RenderDoc 1.47 sa zvanicnog sajta. Ovo se radi jednom.'
        $ProgressPreference = 'SilentlyContinue'
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor
            [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri 'https://renderdoc.org/stable/1.47/RenderDoc_1.47_64.zip' `
            -OutFile $zip -UseBasicParsing -TimeoutSec 300
        $expected = 'b51712e58d40066b34ca30c7869d15f72e4b5c001b2d92232f6783a636d5a3c1'
        if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $expected) {
            throw 'RenderDoc arhiva nije prosla SHA-256 proveru.'
        }
        Expand-Archive -LiteralPath $zip -DestinationPath $stage
        $extracted = Join-Path $stage 'RenderDoc_1.47_64'
        if (Test-Path -LiteralPath $renderDoc) {
            throw 'Postojeci folder RenderDoc_1.47_64 je nepotpun. Raspakuj ZIP u nov folder.'
        }
        Move-Item -LiteralPath $extracted -Destination $renderDoc
    } finally {
        Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Find-Game {
    $paths = [Collections.Generic.List[string]]::new()
    $paths.Add($root)
    $steam = Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction SilentlyContinue
    if ($null -ne $steam -and $steam.SteamPath) {
        $libraries = @($steam.SteamPath)
        $libraryList = Join-Path $steam.SteamPath 'steamapps\libraryfolders.vdf'
        if (Test-Path -LiteralPath $libraryList -PathType Leaf) {
            foreach ($match in [regex]::Matches((Get-Content -LiteralPath $libraryList -Raw), '"path"\s+"([^"]+)"')) {
                $libraries += $match.Groups[1].Value.Replace('\\', '\')
            }
        }
        foreach ($library in $libraries | Select-Object -Unique) {
            $apps = Join-Path $library 'steamapps'
            $manifest = Join-Path $apps 'appmanifest_360430.acf'
            if (Test-Path -LiteralPath $manifest -PathType Leaf) {
                $dir = [regex]::Match((Get-Content -LiteralPath $manifest -Raw), '"installdir"\s+"([^"]+)"')
                if ($dir.Success) {
                    $paths.Add((Join-Path (Join-Path $apps 'common') $dir.Groups[1].Value))
                }
            }
            $paths.Add((Join-Path $apps 'common\Mafia III'))
        }
    }
    foreach ($path in $paths) {
        foreach ($name in 'mafia3definitiveedition.exe', 'mafia3.exe') {
            $exe = Join-Path $path $name
            if (Test-Path -LiteralPath $exe -PathType Leaf) {
                return (Get-Item -LiteralPath $exe).FullName
            }
        }
    }

    Select-InputFile 'Izaberi Mafia III EXE fajl' 'Mafia III|mafia3definitiveedition.exe;mafia3.exe'
}

function Select-InputFile([string]$Title, [string]$Filter) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = [Windows.Forms.OpenFileDialog]::new()
    try {
        $dialog.Title = $Title
        $dialog.Filter = $Filter
        if ($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK) {
            return $dialog.FileName
        }
    } finally {
        $dialog.Dispose()
    }
    throw 'Nijedan fajl nije izabran.'
}

function Export-Capture([string]$Directory, [string]$CaptureFile) {
    # On a fresh RenderDoc setup a first-run dialog otherwise blocks --python.
    $config = Join-Path ([Environment]::GetFolderPath('ApplicationData')) 'qrenderdoc\UI.config'
    $createdConfig = $false
    try {
        if (-not (Test-Path -LiteralPath $config)) {
            New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($config)) -Force | Out-Null
            $stream = [IO.File]::Open($config, [IO.FileMode]::CreateNew)
            $createdConfig = $true
            try {
                $json = '{"rdocConfigData":1,"Analytics_TotalOptOut":true,"CheckUpdate_AllowChecks":false}'
                $bytes = [Text.Encoding]::UTF8.GetBytes($json)
                $stream.Write($bytes, 0, $bytes.Length)
            } finally {
                $stream.Dispose()
            }
        }
        $env:MAFIA_CAPTURE_DIRECTORY = $Directory
        $env:MAFIA_CAPTURE_FILE = $CaptureFile
        $script = Join-Path $root 'izvezi-snimak.py'
        $process = Start-Process -FilePath $captureUI -ArgumentList "--python `"$script`"" -PassThru
        if (-not $process.WaitForExit(180000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'RenderDoc analiza je istekla. Originalni snimak je sacuvan.'
        }
    } finally {
        if ($createdConfig) {
            Remove-Item -LiteralPath $config -ErrorAction SilentlyContinue
        }
    }
}

$status = 1
try {
    if ($TraceMod -or $TestTraceSettings) {
        . (Join-Path $root 'trace-mod.ps1')
    }
    if ($TestTraceSettings) {
        $test = Join-Path $root 'trace-test'
        New-Item -ItemType Directory -Path $test -Force | Out-Null
        Test-TraceSettings $test
        exit 0
    }
    if ($PrepareOnly -or $TestExporter) {
        Prepare-RenderDoc
        if ($TestExporter) {
            $test = Join-Path $root 'export-test'
            New-Item -ItemType Directory -Path $test -Force | Out-Null
            $sample = Join-Path $test 'empty.rdc'
            Set-Content -LiteralPath $sample -Value 'not a capture'
            Export-Capture $test $sample
        }
        exit 0
    }

    $session = Join-Path $root ('Snimci\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $PID)
    New-Item -ItemType Directory -Path $session -Force | Out-Null
    $selectedCapture = ''
    if ($AnalyzeExisting) {
        $selectedCapture = Select-InputFile 'Izaberi postojeci RenderDoc snimak (.rdc)' 'RenderDoc snimak|*.rdc'
        Prepare-RenderDoc
        $captures = @(Get-Item -LiteralPath $selectedCapture)
    } else {
        if (Get-Process -Name 'mafia3definitiveedition', 'mafia3' -ErrorAction SilentlyContinue) {
            throw 'Ugasi Mafia III, pa ponovo pokreni ovu skriptu.'
        }
        $game = Find-Game
        $gameDir = [IO.Path]::GetDirectoryName($game)
        $report.Add("Igra $([IO.Path]::GetFileName($game))")
        $report.Add("EXE SHA-256 $((Get-FileHash -LiteralPath $game -Algorithm SHA256).Hash)")
        $mod = Join-Path $gameDir 'dinput8.dll'
        if (Test-Path -LiteralPath $mod -PathType Leaf) {
            $report.Add("dinput8.dll SHA-256 $((Get-FileHash -LiteralPath $mod -Algorithm SHA256).Hash)")
        } else {
            $report.Add('dinput8.dll nije pronadjen uz igru.')
        }

        if ($TraceMod) {
            Invoke-ModTrace $game $session $report
            exit 0
        }

        Prepare-RenderDoc
        $template = Join-Path $session 'MafiaIII'
        Write-Host 'Ucitaj igru, okreni kameru i pritisni Print Screen jednom.'
        Write-Host 'Sacekaj da RenderDoc zavrsi snimanje, pa izadji iz igre kroz meni.'
        & $captureCommand capture --working-dir $gameDir --capture-file $template --wait-for-exit $game
        if ($LASTEXITCODE -ne 0) {
            throw "RenderDoc nije mogao da zavrsi snimanje (kod $LASTEXITCODE)."
        }

        $captures = @(Get-ChildItem -LiteralPath $session -Filter '*.rdc' -File)
        if ($captures.Count -eq 0) {
            throw 'RenderDoc nije sacuvao frejm. Pritisni Print Screen ili F12 dok je igra aktivna.'
        }
    }
    foreach ($capture in $captures) {
        $report.Add("Snimak $($capture.Name) ($($capture.Length) bajtova)")
    }
    $analysis = Join-Path $session 'RenderDoc analiza.txt'
    Write-Host 'Obradjujem snimak. Nemoj jos zatvarati ovaj prozor.'
    Export-Capture $session $selectedCapture
    if (Test-Path -LiteralPath $analysis -PathType Leaf) {
        $summary = Get-Content -LiteralPath $analysis -Raw -Encoding UTF8
        $report.Add($summary)
        if ($summary -notmatch '(?m)^Uspesno analizirani snimci [1-9][0-9]* od ') {
            throw 'Nijedan snimak nije mogao da se analizira. Detalji su u rezultatu.'
        }
    } else {
        throw 'Snimak je sacuvan, ali RenderDoc nije mogao da izveze tekstualni pregled.'
    }
    $status = 0
} catch {
    $report.Add($_.Exception.Message)
    Write-Host $_.Exception.Message
} finally {
    if (-not ($PrepareOnly -or $TestExporter -or $TestTraceSettings)) {
        $report | Set-Content -LiteralPath $result -Encoding UTF8
        Write-Host 'Posalji fajl Mafia III rezultat.txt iz foldera sa pokretacem.'
    }
}
exit $status
