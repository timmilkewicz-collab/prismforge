[CmdletBinding()]
param(
    [ValidateRange(1, 3600)][int]$SampleSeconds = 30,
    [ValidateRange(50, 10000)][int]$OutputIntervalMilliseconds = 500,
    [ValidateRange(1000, 60000)][int]$ConnectTimeoutMilliseconds = 30000,
    [string]$ExpectedEnginePath = '',
    [switch]$SelfTest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-Exact([System.IO.Stream]$Stream, [int]$Length) {
    $buffer = [byte[]]::new($Length)
    $offset = 0
    $timeout = [System.Threading.CancellationTokenSource]::new()
    $timeout.CancelAfter(3000)
    try {
        while ($offset -lt $Length) {
            try {
                $count = $Stream.ReadAsync($buffer, $offset, $Length - $offset,
                    $timeout.Token).GetAwaiter().GetResult()
            }
            catch [System.OperationCanceledException] {
                throw [TimeoutException]::new(
                    'Timed out waiting for an Engine IPC envelope.')
            }
            if ($count -le 0) { throw 'Engine closed the pipe.' }
            $offset += $count
        }
    }
    finally { $timeout.Dispose() }
    return ,$buffer
}

function Read-Envelope([System.IO.Stream]$Stream) {
    $header = Read-Exact $Stream 4
    $length = [BitConverter]::ToUInt32($header, 0)
    if ($length -eq 0 -or $length -gt 65536) {
        throw "Invalid IPC envelope length: $length"
    }
    $bytes = Read-Exact $Stream ([int]$length)
    $envelope = [Text.Encoding]::UTF8.GetString($bytes) | ConvertFrom-Json
    if ($envelope.version -ne 1) { throw 'Unexpected Engine IPC version.' }
    return $envelope
}

function Get-RequiredProperty($Object, [string]$Name) {
    if ($null -eq $Object) { throw "Missing object containing '$Name'." }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { throw "Missing telemetry field '$Name'." }
    return $property.Value
}

function Get-FiniteNumber($Value, [string]$Name, [double]$Minimum,
        [double]$Maximum) {
    $number = [double]$Value
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or
        $number -lt $Minimum -or $number -gt $Maximum) {
        throw "Invalid telemetry field '$Name': $Value"
    }
    return $number
}

function Read-MusicalView($Payload) {
    $musical = Get-RequiredProperty $Payload 'musical'
    $performance = Get-RequiredProperty $Payload 'performance'
    $eventId = [string](Get-RequiredProperty $musical 'eventId')
    if ($eventId -notmatch '^\d{1,20}$') {
        throw "Invalid telemetry field 'eventId': $eventId"
    }
    return [pscustomobject]@{
        SampleIndex = [uint64](Get-RequiredProperty $musical 'sourceSampleIndex')
        SampleTimeSeconds = Get-FiniteNumber `
            (Get-RequiredProperty $musical 'sampleTimeSeconds') `
            'sampleTimeSeconds' 0 ([double]::MaxValue)
        Rms = Get-FiniteNumber (Get-RequiredProperty $Payload 'rms') 'rms' 0 1.001
        Groove = Get-FiniteNumber (Get-RequiredProperty $musical 'grooveEnergy') `
            'grooveEnergy' 0 1.001
        Building = Get-FiniteNumber (Get-RequiredProperty $musical 'building') `
            'building' 0 1.001
        Peak = Get-FiniteNumber (Get-RequiredProperty $musical 'peak') 'peak' 0 1.001
        Release = Get-FiniteNumber (Get-RequiredProperty $musical 'release') `
            'release' 0 1.001
        Calm = Get-FiniteNumber (Get-RequiredProperty $musical 'calm') 'calm' 0 1.001
        EventId = $eventId
        Fps = Get-FiniteNumber (Get-RequiredProperty $performance 'fps') `
            'fps' 0 10000
    }
}

function Format-MusicalView($View) {
    return [string]::Format(
        [Globalization.CultureInfo]::InvariantCulture,
        'sample={0} time={1:F3}s rms={2:F4} groove={3:F3} build={4:F3} peak={5:F3} release={6:F3} calm={7:F3} event={8} fps={9:F1}',
        [object[]]@($View.SampleIndex, $View.SampleTimeSeconds, $View.Rms,
            $View.Groove, $View.Building, $View.Peak, $View.Release,
            $View.Calm, $View.EventId, $View.Fps))
}

if ($SelfTest) {
    $fixture = @{
        version = 1
        type = 'SignalFrame'
        payload = @{
            rms = 0.125
            musical = @{
                sourceSampleIndex = 96000
                sampleTimeSeconds = 2.0
                eventId = '18446744073709551615'
                grooveEnergy = 0.2
                building = 0.3
                peak = 0.4
                release = 0.5
                calm = 0.6
            }
            performance = @{ fps = 59.9 }
        }
    } | ConvertTo-Json -Compress -Depth 8
    $payloadBytes = [Text.Encoding]::UTF8.GetBytes($fixture)
    $stream = [IO.MemoryStream]::new()
    try {
        $headerBytes = [BitConverter]::GetBytes([uint32]$payloadBytes.Length)
        $stream.Write($headerBytes, 0, $headerBytes.Length)
        $stream.Write($payloadBytes, 0, $payloadBytes.Length)
        $stream.Position = 0
        $envelope = Read-Envelope $stream
        if ($envelope.type -ne 'SignalFrame') { throw 'Self-test type mismatch.' }
        $view = Read-MusicalView $envelope.payload
        $formatted = Format-MusicalView $view
        if ($view.SampleIndex -ne 96000 -or
            $view.EventId -ne '18446744073709551615' -or
            $formatted -notmatch 'groove=0\.200' -or
            $formatted -notmatch 'fps=59\.9') {
            throw 'Self-test schema/format mismatch.'
        }
    }
    finally { $stream.Dispose() }
    Write-Output 'Musical-state monitor parser self-test passed.'
    return
}

# This monitor intentionally opens the pipe read-only. It never writes a
# Command envelope, starts a process, stops a process, or changes show state.
if (-not ('PrismForgeMusicalMonitorPipeIdentity' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class PrismForgeMusicalMonitorPipeIdentity {
    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool GetNamedPipeServerProcessId(
        SafePipeHandle pipe, out uint serverProcessId);
}
'@
}

$expectedPath = ''
if ($ExpectedEnginePath) {
    $expectedPath = (Resolve-Path -LiteralPath $ExpectedEnginePath -ErrorAction Stop).Path
} else {
    $packagedEngine = [IO.Path]::GetFullPath(
        (Join-Path $PSScriptRoot '..\PrismForge.Engine.exe'))
    if (Test-Path -LiteralPath $packagedEngine -PathType Leaf) {
        $expectedPath = $packagedEngine
    }
}

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
    '.', 'PrismForge.v1', [System.IO.Pipes.PipeDirection]::In,
    [System.IO.Pipes.PipeOptions]::Asynchronous)
try {
    try { $pipe.Connect($ConnectTimeoutMilliseconds) }
    catch [TimeoutException] {
        throw 'Could not connect to PrismForge.v1. The Engine must already be running, and Control must be closed because this alpha permits one pipe client.'
    }

    [uint32]$serverPid = 0
    if (-not [PrismForgeMusicalMonitorPipeIdentity]::GetNamedPipeServerProcessId(
            $pipe.SafePipeHandle, [ref]$serverPid)) {
        throw 'Could not identify the named-pipe server process.'
    }
    $identity = Get-CimInstance Win32_Process -Filter "ProcessId = $serverPid"
    if ($null -eq $identity -or
        [string]::IsNullOrWhiteSpace([string]$identity.ExecutablePath) -or
        [IO.Path]::GetFileName([string]$identity.ExecutablePath) -ine
            'PrismForge.Engine.exe') {
        throw "Pipe server PID $serverPid is not an identifiable PrismForge.Engine.exe."
    }
    if ($expectedPath -and -not [string]::Equals(
            [IO.Path]::GetFullPath([string]$identity.ExecutablePath),
            [IO.Path]::GetFullPath($expectedPath),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Pipe server path '$($identity.ExecutablePath)' does not match expected '$expectedPath'."
    }

    Write-Output "Read-only musical telemetry from PID $serverPid at '$($identity.ExecutablePath)'."
    $deadline = [DateTime]::UtcNow.AddSeconds($SampleSeconds)
    $nextOutput = [DateTime]::MinValue
    $frames = 0
    while ([DateTime]::UtcNow -lt $deadline) {
        $envelope = Read-Envelope $pipe
        if ($envelope.type -eq 'ErrorEvent') {
            throw "Engine IPC error: $($envelope.payload.message)"
        }
        if ($envelope.type -ne 'SignalFrame') { continue }
        ++$frames
        $view = Read-MusicalView $envelope.payload
        $now = [DateTime]::UtcNow
        if ($now -ge $nextOutput) {
            Write-Output (Format-MusicalView $view)
            $nextOutput = $now.AddMilliseconds($OutputIntervalMilliseconds)
        }
    }
    if ($frames -eq 0) { throw 'No SignalFrame telemetry arrived.' }
}
finally { $pipe.Dispose() }
