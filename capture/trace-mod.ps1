Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

public static class MafiaTraceIni {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern uint GetPrivateProfileString(string section, string key, string fallback,
        StringBuilder result, int size, string path);

    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool WritePrivateProfileString(string section, string key, string value, string path);

    public static string Read(string path, string section, string key) {
        const string missing = "{missing-635825cb-1a8f-4768-b42b-786cb6804032}";
        var text = new StringBuilder(256);
        GetPrivateProfileString(section, key, missing, text, text.Capacity, path);
        return text.ToString() == missing ? null : text.ToString();
    }

    public static void Write(string path, string section, string key, string value) {
        if (!WritePrivateProfileString(section, key, value, path)) {
            throw new IOException("Cannot change Modern Tweaks diagnostic setting " + section + "/" + key);
        }
        if (Read(path, section, key) != value) {
            throw new IOException("Modern Tweaks diagnostic setting did not verify: " + section + "/" + key);
        }
    }

    public static void Delete(string path, string section, string key) {
        Write(path, section, key, null);
    }
}
'@

function Set-TraceSettings([string]$Path, [string]$Session) {
    $backup = Join-Path $Session 'ModernTweaks.dat.pre-testa'
    $existed = Test-Path -LiteralPath $Path -PathType Leaf
    if ($existed) {
        Copy-Item -LiteralPath $Path -Destination $backup
    }
    $keys = @(
        @{ Section = 'Visuals'; Key = 'TemporalAA' },
        @{ Section = 'Visuals'; Key = 'DLAA' },
        @{ Section = 'Debug'; Key = 'Log' },
        @{ Section = 'Debug'; Key = 'DevLog' }
    )
    foreach ($key in $keys) {
        $key.Original = [MafiaTraceIni]::Read($Path, $key.Section, $key.Key)
    }
    try {
        foreach ($key in $keys) {
            [MafiaTraceIni]::Write($Path, $key.Section, $key.Key, '1')
        }
        $hash = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    } catch {
        if ($existed) {
            Copy-Item -LiteralPath $backup -Destination $Path -Force
        } elseif (Test-Path -LiteralPath $Path -PathType Leaf) {
            Remove-Item -LiteralPath $Path
        }
        throw
    }
    return @{ Backup = $backup; Existed = $existed; Keys = $keys; Hash = $hash }
}

function Restore-TraceSettings([string]$Path, [hashtable]$State) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $State.Hash) {
        if ($State.Existed) {
            Copy-Item -LiteralPath $State.Backup -Destination $Path -Force
        } elseif (Test-Path -LiteralPath $Path -PathType Leaf) {
            Remove-Item -LiteralPath $Path
        }
        return
    }

    # Keep settings changed by the game or user while the diagnostic was running.
    foreach ($key in $State.Keys) {
        if ([MafiaTraceIni]::Read($Path, $key.Section, $key.Key) -eq '1') {
            if ($null -eq $key.Original) {
                [MafiaTraceIni]::Delete($Path, $key.Section, $key.Key)
            } else {
                [MafiaTraceIni]::Write($Path, $key.Section, $key.Key, $key.Original)
            }
        }
    }
}

function Test-TraceSettings([string]$Session) {
    $path = Join-Path $Session 'fixture.dat'
    [IO.File]::WriteAllText($path, "[Debug]`r`nLog=0`r`n[Visuals]`r`nTemporalAA=0`r`n")
    $original = (Get-FileHash -LiteralPath $path).Hash
    $state = Set-TraceSettings $path $Session
    Restore-TraceSettings $path $state
    if ((Get-FileHash -LiteralPath $path).Hash -ne $original) {
        throw 'Byte-for-byte settings restoration failed'
    }

    $state = Set-TraceSettings $path $Session
    [MafiaTraceIni]::Write($path, 'Visuals', 'FilmGrain', '0')
    Restore-TraceSettings $path $state
    if ([MafiaTraceIni]::Read($path, 'Visuals', 'FilmGrain') -ne '0' -or
        [MafiaTraceIni]::Read($path, 'Debug', 'Log') -ne '0' -or
        $null -ne [MafiaTraceIni]::Read($path, 'Visuals', 'DLAA')) {
        throw 'Settings were not merged safely'
    }

    $missing = Join-Path $Session 'absent.dat'
    $state = Set-TraceSettings $missing $Session
    Restore-TraceSettings $missing $state
    if (Test-Path -LiteralPath $missing) {
        throw 'Temporary settings were left behind'
    }
}

function Invoke-ModTrace([string]$Game, [string]$Session, [Collections.Generic.List[string]]$Report) {
    $gameDir = [IO.Path]::GetDirectoryName($Game)
    $dll = Join-Path $gameDir 'dinput8.dll'
    $expected = '529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900'
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf) -or
        (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $expected) {
        throw 'Ovaj test zahteva originalni Modern Tweaks 2.5.1 iz dostavljene arhive.'
    }

    $settings = Join-Path $gameDir 'ModernTweaks.dat'
    $previous = @{}
    $names = 'ModernTweaks.log', 'ModernTweaks_frame.txt'
    foreach ($name in $names) {
        $path = Join-Path $gameDir $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Copy-Item -LiteralPath $path -Destination (Join-Path $Session ($name + '.pre-testa'))
            $previous[$name] = (Get-Item -LiteralPath $path).LastWriteTimeUtc
        }
    }

    $oldDlaa = $env:MT_TEST_DLAA
    $oldRecord = $env:MT_TEST_RECORD
    $state = Set-TraceSettings $settings $Session
    try {
        $env:MT_TEST_DLAA = '1'
        $env:MT_TEST_RECORD = '1'
        Write-Host 'Ucitaj igru, hodaj i okreci kameru oko 30 sekundi, pa normalno izadji.'
        Write-Host 'Dijagnosticki rezim moze privremeno da promeni izgled ivica na slici.'
        $process = Start-Process -FilePath $Game -WorkingDirectory $gameDir -PassThru -Wait
        $Report.Add("Izlazni kod igre $($process.ExitCode)")
        $other = @(Get-Process -Name 'mafia3definitiveedition', 'mafia3' -ErrorAction SilentlyContinue)
        if ($other.Count) {
            Write-Host 'Cekam da se igra zatvori.'
            $other | Wait-Process -ErrorAction SilentlyContinue
        }
    } finally {
        $env:MT_TEST_DLAA = $oldDlaa
        $env:MT_TEST_RECORD = $oldRecord
        Restore-TraceSettings $settings $state
        $Report.Add('Privremene postavke testa su vracene.')
    }

    $hasTaa = $false
    foreach ($name in $names) {
        $path = Join-Path $gameDir $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
            ($previous.ContainsKey($name) -and (Get-Item -LiteralPath $path).LastWriteTimeUtc -eq $previous[$name])) {
            continue
        }
        Copy-Item -LiteralPath $path -Destination (Join-Path $Session $name)
        $Report.Add("`n$name")
        $pattern = '\[dlaa\]|DLAA:|Graphics: frame recorded'
        if ($name -eq 'ModernTweaks_frame.txt') {
            $pattern += '|\bviewport\b|motion vector|jitter'
        }
        $lines = @(Select-String -LiteralPath $path -Pattern $pattern)
        foreach ($line in ($lines | Select-Object -First 240)) {
            $Report.Add($line.Line)
        }
        if ($lines.Count -and ($lines.Line -match '\[dlaa\].*(TAA pass found|textures ready|motion vectors check out)')) {
            $hasTaa = $true
        }
    }
    if (-not $hasTaa) {
        throw 'U novom logu nema potvrde TAA prolaza. Posalji izvestaj da proverimo pokretanje testa.'
    }
    $Report.Add('Dijagnostika je zabelezila TAA prolaz.')
    Write-Host 'TAA podaci su zabelezeni u izvestaju.'
}
