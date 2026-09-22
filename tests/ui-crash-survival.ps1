param(
    [Parameter(Mandatory = $true)][string]$EnginePath,
    [Parameter(Mandatory = $true)][string]$ControlPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$engineFile = (Resolve-Path -LiteralPath $EnginePath).Path
$controlFile = (Resolve-Path -LiteralPath $ControlPath).Path

# Never terminate or contend with a pre-existing live PrismForge instance.
if (@(Get-Process -Name 'PrismForge.Engine', 'PrismForge.Control' -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'A PrismForge process already exists; refusing the crash-survival test.'
}

function Read-Exact([System.IO.Stream]$stream, [int]$length) {
    $buffer = [byte[]]::new($length)
    $offset = 0
    $timeout = [System.Threading.CancellationTokenSource]::new()
    $timeout.CancelAfter(8000)
    while ($offset -lt $length) {
        $count = $stream.ReadAsync($buffer, $offset, $length - $offset,
            $timeout.Token).GetAwaiter().GetResult()
        if ($count -le 0) { throw 'Engine disconnected during UI-crash test' }
        $offset += $count
    }
    $timeout.Dispose()
    return ,$buffer
}

function Read-Snapshot {
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        '.', 'PrismForge.v1', [System.IO.Pipes.PipeDirection]::InOut,
        [System.IO.Pipes.PipeOptions]::Asynchronous)
    try {
        $pipe.Connect(8000)
        for ($index = 0; $index -lt 30; $index++) {
            $header = Read-Exact $pipe 4
            $length = [BitConverter]::ToUInt32($header, 0)
            if ($length -eq 0 -or $length -gt 65536) { throw "Invalid IPC length: $length" }
            $payload = Read-Exact $pipe ([int]$length)
            $message = [Text.Encoding]::UTF8.GetString($payload) | ConvertFrom-Json
            if ($message.type -eq 'StateSnapshot' -and
                $message.payload.PSObject.Properties['output']) {
                return $message.payload
            }
        }
        throw 'No StateSnapshot arrived'
    }
    finally { $pipe.Dispose() }
}

$engine = $null
$control = $null
try {
    $engine = Start-Process -FilePath $engineFile `
        -ArgumentList @('--seconds', '30', '--no-audio', '--no-persist') `
        -PassThru -WindowStyle Hidden
    $initial = Read-Snapshot
    if ($initial.output.width -ne 1920 -or $initial.output.height -ne 1080 -or
        -not $initial.output.spout.ready) {
        throw 'Engine output was not ready before the UI crash'
    }

    $control = Start-Process -FilePath $controlFile -PassThru
    Start-Sleep -Seconds 3
    $control.Refresh()
    if ($control.HasExited) { throw 'Control exited before crash simulation' }

    # Simulate a hard UI crash, targeting only the process this test created.
    Stop-Process -Id $control.Id -Force
    $control.WaitForExit(5000) | Out-Null
    $engine.Refresh()
    if ($engine.HasExited) { throw 'Engine exited when Control was terminated' }

    $after = Read-Snapshot
    $engine.Refresh()
    if ($engine.HasExited -or -not $after.output.spout.ready -or
        $after.output.width -ne 1920 -or $after.output.height -ne 1080) {
        throw 'Engine output did not survive the UI crash'
    }
    Write-Output 'UI-crash survival passed: Control was terminated, Engine stayed live, and IPC reconnected to the same 1080p Spout sender.'
}
finally {
    if ($control) {
        $control.Refresh()
        if (-not $control.HasExited) { Stop-Process -Id $control.Id -Force }
    }
    if ($engine) {
        $engine.Refresh()
        if (-not $engine.HasExited) { Stop-Process -Id $engine.Id -Force }
    }
}
