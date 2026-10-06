param(
    [Parameter(Mandatory = $true)][string]$EnginePath,
    [switch]$TestAudioInput
)

$ErrorActionPreference = 'Stop'
$engine = (Resolve-Path -LiteralPath $EnginePath).Path
if (Get-Process -Name 'PrismForge.Engine' -ErrorAction SilentlyContinue) {
    throw 'IPC smoke requires no existing PrismForge Engine; refusing to control a live show.'
}
$process = Start-Process -FilePath $engine -ArgumentList @('--seconds', '30', '--no-audio', '--no-persist') `
    -PassThru -WindowStyle Hidden
$client = $null

function Open-Client {
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
        '.', 'PrismForge.v1', [System.IO.Pipes.PipeDirection]::InOut)
    # Cold startup compiles all scene/tier shader variants before IPC opens.
    $pipe.Connect(30000)
    return $pipe
}

function Read-Exact([System.IO.Stream]$stream, [int]$length) {
    $buffer = [byte[]]::new($length)
    $position = 0
    while ($position -lt $length) {
        $read = $stream.Read($buffer, $position, $length - $position)
        if ($read -le 0) { throw 'Pipe closed before envelope completed' }
        $position += $read
    }
    return ,$buffer
}

function Read-Envelope([System.IO.Stream]$stream) {
    $header = Read-Exact $stream 4
    $length = [BitConverter]::ToUInt32($header, 0)
    if ($length -eq 0 -or $length -gt 65536) { throw "Invalid engine length: $length" }
    $payload = Read-Exact $stream ([int]$length)
    return ([Text.Encoding]::UTF8.GetString($payload) | ConvertFrom-Json)
}

function Send-Command([System.IO.Stream]$stream, [hashtable]$payload) {
    $json = @{ version = 1; type = 'Command'; payload = $payload } |
        ConvertTo-Json -Compress -Depth 16
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    $stream.Write([BitConverter]::GetBytes([uint32]$bytes.Length), 0, 4)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
}

function Add-OscString([System.Collections.Generic.List[byte]]$bytes, [string]$value) {
    foreach ($item in [Text.Encoding]::UTF8.GetBytes($value)) { $bytes.Add($item) }
    $bytes.Add(0)
    while ($bytes.Count % 4 -ne 0) { $bytes.Add(0) }
}

function Add-OscFloat([System.Collections.Generic.List[byte]]$bytes, [single]$value) {
    $floatBytes = [BitConverter]::GetBytes($value)
    if ([BitConverter]::IsLittleEndian) { [array]::Reverse($floatBytes) }
    foreach ($item in $floatBytes) { $bytes.Add($item) }
}

function Send-Osc([int]$port, [byte[]]$packet) {
    $udp = [System.Net.Sockets.UdpClient]::new()
    try {
        $udp.Connect('127.0.0.1', $port)
        [void]$udp.Send($packet, $packet.Length)
    }
    finally { $udp.Dispose() }
}

function Wait-Snapshot([System.IO.Stream]$stream, [scriptblock]$predicate) {
    for ($index = 0; $index -lt 160; $index++) {
        $message = Read-Envelope $stream
        if ($message.version -ne 1) { throw 'Wrong IPC version' }
        if ($message.type -eq 'ErrorEvent') {
            throw "Engine error: $($message.payload.message)"
        }
        if ($message.type -eq 'StateSnapshot' -and (& $predicate $message.payload)) {
            return $message.payload
        }
    }
    throw 'Timed out waiting for expected StateSnapshot'
}

function Wait-Error([System.IO.Stream]$stream) {
    for ($index = 0; $index -lt 100; $index++) {
        $message = Read-Envelope $stream
        if ($message.type -eq 'ErrorEvent') { return $message.payload }
    }
    throw 'Timed out waiting for ErrorEvent'
}

try {
    $client = Open-Client
    $initial = Wait-Snapshot $client { param($state) $state.output.width -eq 1920 }
    if ($initial.output.height -ne 1080 -or
        $initial.output.spout.senderName -ne 'PrismForge') {
        throw 'Initial Spout state is wrong'
    }
    if ($initial.audio.sources.Count -lt 1) { throw 'No audio sources enumerated' }
    if ($initial.audio.connected -or $initial.audio.receiving) {
        throw '--no-audio incorrectly reports an active or receiving capture'
    }
    if ($initial.masterEffects.Count -ne 4) { throw 'Master performance controls are missing' }
    $catalogIds = @($initial.sceneCatalog | ForEach-Object { $_.id })
    foreach ($id in @('hex-vortex', 'ferrofluid-reactor', 'shardwell', 'neon-orbs', 'mirror-cathedral')) {
        if ($catalogIds -notcontains $id) { throw "New scene is missing: $id" }
    }
    $mirror = @($initial.sceneCatalog | Where-Object { $_.id -eq 'mirror-cathedral' })[0]
    if (@($mirror.parameters).Count -ne 4) { throw 'Mirror Cathedral live controls are missing' }
    # WASAPI enumeration is now off the render thread. Give its source-list
    # event a short window to arrive without making hardware presence a CI gate.
    $discovered = $initial
    for ($index = 0; $index -lt 40 -and $discovered.audio.sources.Count -le 1; $index++) {
        $message = Read-Envelope $client
        if ($message.type -eq 'StateSnapshot' -and
            $message.payload.audio.sources.Count -gt 1) {
            $discovered = $message.payload
        }
    }
    Send-Command $client @{ action = 'setScene'; deck = 'A'; sceneId = 'mirror-cathedral' }
    [void](Wait-Snapshot $client { param($state) $state.decks.A.sceneId -eq 'mirror-cathedral' })
    Send-Command $client @{ action = 'setSceneParameter'; deck = 'A'; sceneId = 'mirror-cathedral'; index = 0; amount = 0.82 }
    [void](Wait-Snapshot $client {
        param($state) [math]::Abs($state.decks.A.sceneParams[0] - 0.82) -lt 0.001
    })
    Send-Command $client @{ action = 'setSceneParameter'; deck = 'A'; sceneId = 'mirror-cathedral'; index = 3; amount = 0.14 }
    [void](Wait-Snapshot $client {
        param($state) [math]::Abs($state.decks.A.sceneParams[3] - 0.14) -lt 0.001
    })
    Send-Command $client @{ action = 'setScene'; deck = 'A'; sceneId = 'hex-vortex' }
    [void](Wait-Snapshot $client { param($state) $state.decks.A.sceneId -eq 'hex-vortex' })
    Send-Command $client @{ action = 'setSceneParameter'; deck = 'A'; sceneId = 'mirror-cathedral'; index = 0; amount = 0.1 }
    $staleParameter = Wait-Error $client
    if ($staleParameter.message -notmatch 'Scene parameter unavailable or stale') {
        throw "Wrong stale scene-control rejection: $($staleParameter.message)"
    }
    Send-Command $client @{ action = 'setScene'; deck = 'B'; sceneId = 'ferrofluid-reactor' }
    Send-Command $client @{ action = 'setCrossfader'; value = 0.7 }
    Send-Command $client @{ action = 'setMasterEffect'; index = 0; amount = 0.35 }
    Send-Command $client @{ action = 'setMasterEffect'; index = 1; amount = 0.2 }
    $changed = Wait-Snapshot $client {
        param($state)
        $state.decks.A.sceneId -eq 'hex-vortex' -and
            $state.decks.B.sceneId -eq 'ferrofluid-reactor' -and
            [math]::Abs($state.crossfader - 0.7) -lt 0.001 -and
            [math]::Abs($state.masterEffects[0] - 0.35) -lt 0.001 -and
            [math]::Abs($state.masterEffects[1] - 0.2) -lt 0.001
    }
    Send-Command $client @{ action = 'setScene'; deck = 'A'; sceneId = 'not-a-scene' }
    $rejected = Wait-Error $client
    if ($rejected.message -notmatch 'Scene unavailable') {
        throw "Wrong rejection: $($rejected.message)"
    }
    Send-Command $client @{ action = 'saveCue'; index = 0 }
    [void](Wait-Snapshot $client { param($state) $state.cues[0].saved })
    Send-Command $client @{ action = 'setCrossfader'; value = 0.2 }
    [void](Wait-Snapshot $client {
        param($state) [math]::Abs($state.crossfader - 0.2) -lt 0.001
    })
    Send-Command $client @{ action = 'recallCue'; index = 0 }
    [void](Wait-Snapshot $client {
        param($state) [math]::Abs($state.crossfader - 0.7) -lt 0.001
    })

    # Closing the UI's pipe must not reset or terminate output state.
    $client.Dispose()
    $client = Open-Client
    $afterReconnect = Wait-Snapshot $client {
        param($state) $state.decks.A.sceneId -eq 'hex-vortex'
    }
    if ([math]::Abs($afterReconnect.crossfader - 0.7) -ge 0.001) {
        throw 'State was lost on control reconnect'
    }
    if ([math]::Abs($afterReconnect.masterEffects[0] - 0.35) -ge 0.001) {
        throw 'Master macro state was lost on control reconnect'
    }
    Send-Command $client @{ action = 'setScene'; deck = 'A'; sceneId = 'shardwell' }
    Send-Command $client @{ action = 'setScene'; deck = 'B'; sceneId = 'neon-orbs' }
    [void](Wait-Snapshot $client {
        param($state)
        $state.decks.A.sceneId -eq 'shardwell' -and
            $state.decks.B.sceneId -eq 'neon-orbs'
    })

    $osc = [System.Collections.Generic.List[byte]]::new()
    Add-OscString $osc '/prismforge/crossfader'
    Add-OscString $osc ',f'
    Add-OscFloat $osc ([single]0.35)
    Send-Osc 12100 ($osc.ToArray())
    [void](Wait-Snapshot $client {
        param($state) [math]::Abs($state.crossfader - 0.35) -lt 0.001
    })

    $gesture = [System.Collections.Generic.List[byte]]::new()
    Add-OscString $gesture '/prism/gesture'
    Add-OscString $gesture ',sfff'
    Add-OscString $gesture 'punch'
    Add-OscFloat $gesture ([single]0.8)
    Add-OscFloat $gesture ([single]0.5)
    Add-OscFloat $gesture ([single]0.5)
    Send-Osc 12002 ($gesture.ToArray())
    $sawGesture = $false
    for ($index = 0; $index -lt 60; $index++) {
        $message = Read-Envelope $client
        if ($message.type -eq 'SignalFrame' -and
            $message.payload.gesture.name -eq 'punch' -and
            $message.payload.gesture.motion -gt 0.1) {
            $sawGesture = $true
            break
        }
    }
    if (-not $sawGesture) { throw 'OSC PrismBurst gesture was not observed' }

    $maono = @($discovered.audio.sources | Where-Object {
        $_.kind -eq 'input' -and $_.name -match 'Maono'
    } | Select-Object -First 1)
    $maonoResult = 'not opened (default)'
    if ($TestAudioInput -and $maono.Count -gt 0) {
        Send-Command $client @{ action = 'setAudioSource'; id = $maono[0].id }
        [void](Wait-Snapshot $client {
            param($state)
            $state.audio.sourceId -eq $maono[0].id -and $state.audio.connected
        })
        $maonoResult = 'selected'
    } elseif ($TestAudioInput) {
        $maonoResult = 'not present'
    }
    Write-Output "IPC smoke passed: 1080p sender, Mirror Cathedral controls/stale-command guard, five new scenes, master macros, $($discovered.audio.sources.Count) audio sources, Maono $maonoResult, pipe commands/reconnect, OSC commands/gesture"
}
finally {
    if ($client) { $client.Dispose() }
    if ($process -and -not $process.HasExited) {
        # Only the test process we started is stopped; no live engine is targeted.
        Stop-Process -Id $process.Id -Force
    }
}
