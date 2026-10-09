param(
    [Parameter(Mandatory = $true)][string]$EnginePath,
    [string]$SourceId = '',
    [string]$SourceName = '',
    [ValidateRange(5, 30)][int]$SampleSeconds = 10,
    [ValidateRange(1, 10)][int]$SettleSeconds = 3
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($SourceId -and $SourceName) {
    throw 'Use SourceId or SourceName, not both.'
}

$engineFile = (Resolve-Path -LiteralPath $EnginePath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $engineFile -PathType Leaf) -or
    [IO.Path]::GetFileName($engineFile) -ine 'PrismForge.Engine.exe') {
    throw 'EnginePath must identify an existing PrismForge.Engine.exe file.'
}
if (@(Get-Process -Name 'PrismForge.Engine', 'PrismForge.Control' -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'A PrismForge Engine or Control already exists; refusing to disturb a live session.'
}

# A same-name pipe is not proof that this probe owns its server. Check the
# Windows pipe-server PID before reading state or sending setAudioSource.
if (-not ('PrismForgeAudioSmokePipeIdentity' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class PrismForgeAudioSmokePipeIdentity {
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetNamedPipeServerProcessId(
        SafePipeHandle pipe, out uint serverProcessId);
}
'@
}

function Assert-OwnedEngine([System.Diagnostics.Process]$process, [string]$expectedPath) {
    $process.Refresh()
    if ($process.HasExited) {
        throw "Test-owned Engine exited early with code $($process.ExitCode)."
    }
    $others = @(Get-Process -Name 'PrismForge.Engine' -ErrorAction SilentlyContinue)
    if ($others.Count -ne 1 -or $others[0].Id -ne $process.Id) {
        throw 'The running Engine process is not exclusively the test-owned PID.'
    }
    $identity = Get-CimInstance Win32_Process -Filter "ProcessId = $($process.Id)"
    if ($null -eq $identity -or [string]::IsNullOrEmpty($identity.ExecutablePath) -or
        -not [string]::Equals([IO.Path]::GetFullPath($identity.ExecutablePath),
            [IO.Path]::GetFullPath($expectedPath), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The test-owned Engine PID does not have the expected executable path.'
    }
}

function Read-Exact([System.IO.Stream]$stream, [int]$length) {
    $buffer = [byte[]]::new($length)
    $offset = 0
    $timeout = [System.Threading.CancellationTokenSource]::new()
    $timeout.CancelAfter(3000)
    try {
        while ($offset -lt $length) {
            try {
                $count = $stream.ReadAsync($buffer, $offset, $length - $offset,
                    $timeout.Token).GetAwaiter().GetResult()
            }
            catch [System.OperationCanceledException] {
                throw [TimeoutException]::new('Timed out waiting for an Engine IPC envelope.')
            }
            if ($count -le 0) { throw 'Engine closed the pipe during the audio check.' }
            $offset += $count
        }
    }
    finally { $timeout.Dispose() }
    return ,$buffer
}

function Read-Envelope([System.IO.Stream]$stream) {
    $header = Read-Exact $stream 4
    $length = [BitConverter]::ToUInt32($header, 0)
    if ($length -eq 0 -or $length -gt 65536) { throw "Invalid IPC envelope length: $length" }
    $payload = Read-Exact $stream ([int]$length)
    $envelope = [Text.Encoding]::UTF8.GetString($payload) | ConvertFrom-Json
    if ($envelope.version -ne 1) { throw 'Unexpected Engine IPC version.' }
    return $envelope
}

function Read-AudioMessage([System.IO.Stream]$stream) {
    $message = Read-Envelope $stream
    if ($message.type -eq 'ErrorEvent') {
        throw "Engine audio/IPC error: $($message.payload.message)"
    }
    return $message
}

function Send-AudioSource([System.IO.Stream]$stream, [string]$id) {
    $json = @{ version = 1; type = 'Command'; requestId = 'audio-signal-smoke';
        payload = @{ action = 'setAudioSource'; id = $id } } |
        ConvertTo-Json -Compress -Depth 8
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    $stream.Write([BitConverter]::GetBytes([uint32]$bytes.Length), 0, 4)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
}

function Require-AudioState($snapshot, [string]$id) {
    if ($snapshot.audio.sourceId -ne $id -or -not $snapshot.audio.connected) {
        throw "Audio source disconnected or changed: wanted '$id', got '$($snapshot.audio.sourceId)', connected=$($snapshot.audio.connected)."
    }
}

function Finite-Level($value, [string]$name) {
    $number = [double]$value
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or
        $number -lt 0 -or $number -gt 1.001) {
        throw "Invalid $name level from Engine: $number"
    }
    return $number
}

$engine = $null
$pipe = $null
try {
    # The Engine clock starts after shader initialization. This bounds the
    # device discovery, asynchronous switch, settling and sample stages.
    $engineSeconds = $SampleSeconds + $SettleSeconds + 45
    $engine = Start-Process -FilePath $engineFile `
        -ArgumentList @('--seconds', [string]$engineSeconds, '--no-persist') `
        -PassThru -WindowStyle Hidden
    Assert-OwnedEngine $engine $engineFile

    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        '.', 'PrismForge.v1', [System.IO.Pipes.PipeDirection]::InOut,
        [System.IO.Pipes.PipeOptions]::Asynchronous)
    # The cold Engine compiles all 64 scene/tier variants before opening IPC.
    $pipe.Connect(60000)
    Assert-OwnedEngine $engine $engineFile
    [uint32]$serverPid = 0
    if (-not [PrismForgeAudioSmokePipeIdentity]::GetNamedPipeServerProcessId(
            $pipe.SafePipeHandle, [ref]$serverPid) -or $serverPid -ne $engine.Id) {
        throw "IPC server PID $serverPid does not match the test-owned Engine PID $($engine.Id)."
    }

    $snapshot = $null
    $initialDeadline = [DateTime]::UtcNow.AddSeconds(8)
    while ($null -eq $snapshot -and [DateTime]::UtcNow -lt $initialDeadline) {
        $message = Read-AudioMessage $pipe
        if ($message.type -eq 'StateSnapshot' -and
            $null -ne $message.payload.PSObject.Properties['audio']) {
            $snapshot = $message.payload
        }
    }
    if ($null -eq $snapshot) { throw 'No initial StateSnapshot arrived.' }

    $targetId = if ([string]::IsNullOrEmpty($SourceId)) { 'system-default' } else { $SourceId }
    # Device enumeration is asynchronous. Wait for the requested exact ID;
    # never guess an input from a friendly name or a positional list index.
    $catalogDeadline = [DateTime]::UtcNow.AddSeconds(6)
    while ([DateTime]::UtcNow -lt $catalogDeadline) {
        $ids = @($snapshot.audio.sources | ForEach-Object { $_.id })
        if ($ids -contains $targetId -and $snapshot.audio.sources.Count -gt 1) { break }
        $message = Read-AudioMessage $pipe
        if ($message.type -eq 'StateSnapshot') { $snapshot = $message.payload }
    }
    if (-not $SourceId -and -not $SourceName) {
        Write-Output 'Enumerated audio sources (exact IDs):'
        foreach ($source in @($snapshot.audio.sources)) {
            Write-Output "  $($source.kind) | $($source.name) | $($source.id)"
        }
    } else {
        Write-Output "Enumerated $(@($snapshot.audio.sources).Count) audio sources."
    }
    if ($SourceName) {
        $named = @($snapshot.audio.sources | Where-Object { $_.name -ceq $SourceName })
        if ($named.Count -ne 1) {
            throw "SourceName '$SourceName' did not resolve to exactly one enumerated device."
        }
        $targetId = $named[0].id
    }
    if (@($snapshot.audio.sources | ForEach-Object { $_.id }) -notcontains $targetId) {
        throw "Requested source ID '$targetId' was not enumerated; no switch was attempted."
    }

    if (-not ([string]::IsNullOrEmpty($SourceId) -and [string]::IsNullOrEmpty($SourceName) -and
        $targetId -eq 'system-default') -and
        -not ($snapshot.audio.sourceId -eq $targetId -and $snapshot.audio.connected)) {
        Assert-OwnedEngine $engine $engineFile
        Send-AudioSource $pipe $targetId
    }

    $switchDeadline = [DateTime]::UtcNow.AddSeconds(12)
    while (($snapshot.audio.sourceId -ne $targetId -or -not $snapshot.audio.connected) -and
        [DateTime]::UtcNow -lt $switchDeadline) {
        $message = Read-AudioMessage $pipe
        if ($message.type -eq 'StateSnapshot') { $snapshot = $message.payload }
    }
    if ($snapshot.audio.sourceId -ne $targetId -or -not $snapshot.audio.connected) {
        throw "Source '$targetId' did not reach connected state after the asynchronous switch."
    }

    # Drain pre-switch frames; the analyzer is reset by a successful handoff.
    $settleDeadline = [DateTime]::UtcNow.AddSeconds($SettleSeconds)
    while ([DateTime]::UtcNow -lt $settleDeadline) {
        $message = Read-AudioMessage $pipe
        if ($message.type -eq 'StateSnapshot') {
            $snapshot = $message.payload
            Require-AudioState $snapshot $targetId
        }
    }
    Assert-OwnedEngine $engine $engineFile

    $stats = @{
        signalFrames = 0; uniqueFrames = 0; snapshots = 0; activeFrames = 0
        rmsSum = 0.0; rmsMax = 0.0; peakMax = 0.0
        lowMax = 0.0; midMax = 0.0; highMax = 0.0
        snapshotRmsMax = 0.0; snapshotPeakMax = 0.0
        hitIncrements = 0; hitFrames = 0; bandMax = [double[]]::new(32)
        lastIndex = $null; lastHitCount = $null
    }
    $sampleStart = [DateTime]::UtcNow
    $sampleDeadline = $sampleStart.AddSeconds($SampleSeconds)
    while ([DateTime]::UtcNow -lt $sampleDeadline) {
        $message = Read-AudioMessage $pipe
        if ($message.type -eq 'StateSnapshot') {
            $snapshot = $message.payload
            Require-AudioState $snapshot $targetId
            $stats.snapshots++
            $stats.snapshotRmsMax = [math]::Max($stats.snapshotRmsMax,
                (Finite-Level $snapshot.audio.rms 'snapshot RMS'))
            $stats.snapshotPeakMax = [math]::Max($stats.snapshotPeakMax,
                (Finite-Level $snapshot.audio.peak 'snapshot peak'))
            continue
        }
        if ($message.type -ne 'SignalFrame') { continue }
        $stats.signalFrames++
        $signal = $message.payload
        $index = [uint64]$signal.sampleIndex
        if ($null -ne $stats.lastIndex) {
            if ($index -lt $stats.lastIndex) { throw 'Audio sample index reset during the measured window.' }
            if ($index -eq $stats.lastIndex) { continue }
        }
        $stats.lastIndex = $index
        $stats.uniqueFrames++
        $rms = Finite-Level $signal.rms 'signal RMS'
        $peak = Finite-Level $signal.peak 'signal peak'
        $stats.rmsSum += $rms
        $stats.rmsMax = [math]::Max($stats.rmsMax, $rms)
        $stats.peakMax = [math]::Max($stats.peakMax, $peak)
        $stats.lowMax = [math]::Max($stats.lowMax, (Finite-Level $signal.low 'low band'))
        $stats.midMax = [math]::Max($stats.midMax, (Finite-Level $signal.mid 'mid band'))
        $stats.highMax = [math]::Max($stats.highMax, (Finite-Level $signal.high 'high band'))
        if ($rms -ge 0.01) { $stats.activeFrames++ }
        $bands = @($signal.bands)
        if ($bands.Count -ne 32) { throw "Expected 32 spectrum bands, got $($bands.Count)." }
        for ($band = 0; $band -lt 32; $band++) {
            $stats.bandMax[$band] = [math]::Max($stats.bandMax[$band],
                (Finite-Level $bands[$band] "band $band"))
        }
        $hitCount = [uint64]$signal.hitCount
        if ($null -ne $stats.lastHitCount) {
            if ($hitCount -lt $stats.lastHitCount) { throw 'Hit count reset during the measured window.' }
            $stats.hitIncrements += ($hitCount - $stats.lastHitCount)
        }
        $stats.lastHitCount = $hitCount
        if ($signal.hit) { $stats.hitFrames++ }
    }

    Assert-OwnedEngine $engine $engineFile
    Require-AudioState $snapshot $targetId
    $elapsed = ([DateTime]::UtcNow - $sampleStart).TotalSeconds
    $meanRms = if ($stats.uniqueFrames -gt 0) {
        $stats.rmsSum / $stats.uniqueFrames
    } else { 0.0 }
    $bandProfile = ($stats.bandMax | ForEach-Object { $_.ToString('F4',
        [Globalization.CultureInfo]::InvariantCulture) }) -join ','
    Write-Output "Measured source: $targetId (connected; $([math]::Round($elapsed, 1)) s)"
    Write-Output "SignalFrame total=$($stats.signalFrames) distinct-sample-indices=$($stats.uniqueFrames); StateSnapshot=$($stats.snapshots)"
    Write-Output ('RMS mean={0:F4} max={1:F4}; peak max={2:F4}; snapshot RMS max={3:F4} peak max={4:F4}' -f
        $meanRms, $stats.rmsMax, $stats.peakMax,
        $stats.snapshotRmsMax, $stats.snapshotPeakMax)
    Write-Output ('Grouped-band maxima: low={0:F4} mid={1:F4} high={2:F4}; hit increments={3} hit frames={4}' -f
        $stats.lowMax, $stats.midMax, $stats.highMax,
        $stats.hitIncrements, $stats.hitFrames)
    Write-Output "32-band maxima (indices 0-31): $bandProfile"
    if ($stats.uniqueFrames -lt 5) {
        throw 'RESULT: NO_FRESH_AUDIO_SAMPLES. Connection alone does not prove capture.'
    }
    if ($stats.activeFrames -ge 3 -and $stats.peakMax -ge 0.02) {
        Write-Output "RESULT: ACTIVE_SIGNAL (RMS >= 0.01 in $($stats.activeFrames) distinct frames; peak >= 0.02). Meter activity only, not a music-reactive or visual pass."
    } else {
        Write-Output 'RESULT: SILENT_OR_BELOW_THRESHOLD. No clear level above the conservative meter thresholds; check routing, playback and gain.'
        throw 'Audio meter activity criterion was not met.'
    }
}
finally {
    if ($pipe) { $pipe.Dispose() }
    if ($engine) {
        $engine.Refresh()
        if (-not $engine.HasExited) {
            # Never kill by name. A failed path check leaves the bounded test
            # process to exit via --seconds rather than risking another show.
            $owned = Get-CimInstance Win32_Process -Filter "ProcessId = $($engine.Id)" -ErrorAction SilentlyContinue
            if ($null -ne $owned -and -not [string]::IsNullOrEmpty($owned.ExecutablePath) -and
                [string]::Equals([IO.Path]::GetFullPath($owned.ExecutablePath),
                    [IO.Path]::GetFullPath($engineFile), [StringComparison]::OrdinalIgnoreCase)) {
                $engine.Kill()
            } else {
                Write-Warning "Did not stop PID $($engine.Id): executable path could not be reverified. Its --seconds bound is $engineSeconds."
            }
        }
    }
}
