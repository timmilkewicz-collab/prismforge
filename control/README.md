# PrismForge Control

`PrismForge.Control.exe` is the local operator surface for PrismForge. It is a
.NET 10 WPF application that hosts a React interface in the installed Microsoft
Edge WebView2 runtime. All HTML, JavaScript, and CSS are bundled with the app;
the control surface starts no HTTP server and does not load remote content.
Navigation, pop-up windows, and inbound WebView messages are restricted to the
bundled `https://control.prismforge.local` virtual origin. A restrictive content
security policy blocks network connections and permits scripts, styles, and
fonts only from that bundled origin.

The control app is deliberately not the owner of show state. It can start before
the render engine, display an offline state, reconnect automatically, and rebuild
its view from the next engine `StateSnapshot`.

## Build and test

Prerequisites on the development machine:

- .NET SDK 10
- Node.js 24 or newer
- Microsoft Edge WebView2 Runtime (included with current Windows 10/11 builds)

From this directory:

```powershell
.\build.ps1
```

The script installs the locked frontend dependencies, runs the TypeScript tests,
builds the frontend, builds the WPF host, and runs the .NET protocol tests. The
resulting executable is under
`PrismForge.Control\bin\Release\net10.0-windows\PrismForge.Control.exe`.

## Engine IPC contract: v1

The engine owns a duplex named pipe at:

```text
\\.\pipe\PrismForge.v1
```

Each message is encoded as:

1. Four-byte unsigned little-endian JSON byte length.
2. UTF-8 JSON, with no delimiter or terminator.

The maximum accepted message is 65,536 bytes. The control process rejects invalid
lengths, invalid UTF-8/JSON, unknown envelope types, and commands outside the
allowlist before they reach the pipe.

Every engine message and command uses this envelope:

```json
{
  "version": 1,
  "type": "Command",
  "requestId": "1c80f4c6-f724-47ff-96fe-37ccac14ca10",
  "payload": {
    "action": "setCrossfader",
    "value": 0.5
  }
}
```

Allowed `type` values are `Command`, `StateSnapshot`, `SignalFrame`,
`DeviceEvent`, and `ErrorEvent`. `requestId` is required for `Command` and
optional for engine-originated events.

### Commands

| Action | Required payload |
| --- | --- |
| `requestSnapshot` | none |
| `setScene` | `deck: "A" | "B"`, `sceneId: string` |
| `setCrossfader` | `value: number` in `0..1` |
| `setBlackout` | `enabled: boolean` |
| `setPanicDim` | `enabled: boolean` |
| `setEffect` | `deck`, `effectIndex: integer >= 0`, `amount: number` in `0..1` |
| `setModulation` | `deck`, `slot: integer >= 0`, `source`, `target`, `amount` in `-1..1`, `smoothing` in `0..1`, `enabled` |
| `saveCue` | `index: integer` in `0..15` |
| `recallCue` | `index: integer` in `0..15` |
| `setAudioSource` | `id: string` |
| `saveShow` | `name: string`, 1–64 safe filename characters |
| `loadShow` | `name: string`, 1–64 safe filename characters |

Show files are engine-owned. The current native host saves them atomically under
`Documents\PrismForge\Shows`; the control app sends only the safe show name and
never reads or writes show files itself.

On every new connection the engine must send a full `StateSnapshot`. The control
app also sends `requestSnapshot` immediately after connecting, so either side can
recover if the other started first.

### StateSnapshot payload

The v1 control surface consumes this shape. Extra fields are permitted and
ignored, so the engine can extend snapshots compatibly.

```json
{
  "revision": 42,
  "decks": {
    "A": {
      "sceneId": "ink-tide",
      "effects": [
        { "id": 0, "name": "Bloom", "amount": 0.32 },
        { "id": 1, "name": "Feedback", "amount": 0.18 },
        { "id": 2, "name": "Kaleidoscope", "amount": 0.0 },
        { "id": 3, "name": "Pixelate", "amount": 0.0 }
      ],
      "modulations": [{
        "slot": 0,
        "source": "audio.kick",
        "target": "deck.effect.0",
        "amount": 0.7,
        "smoothing": 0.15,
        "enabled": true
      }]
    },
    "B": { "sceneId": "prism-atrium", "effects": [], "modulations": [] }
  },
  "crossfader": 0.5,
  "blackout": false,
  "panicDim": false,
  "sceneCatalog": [{ "id": "ink-tide", "name": "Ink Tide", "category": "procedural" }],
  "audio": {
    "sourceId": "system-default",
    "sources": [{ "id": "system-default", "name": "System audio (default)", "kind": "loopback", "connected": true }],
    "rms": 0.1,
    "peak": 0.3,
    "low": 0.2,
    "mid": 0.1,
    "high": 0.05,
    "clipping": false
  },
  "cues": [{ "index": 0, "saved": true, "label": "OPEN" }],
  "performance": { "fps": 59.9, "targetFps": 60, "frameTimeMs": 16.7, "adaptiveQuality": true },
  "output": { "spout": { "ready": true, "connected": false, "senderName": "PrismForge" }, "width": 1920, "height": 1080 }
}
```

`SignalFrame` is the high-frequency, partial update. It may contain `audio` and
`performance`; omitted values retain their latest snapshot value. UI rendering
is paced by the browser, while pipe reads remain independent of the UI thread.

### WebView bridge

The native host forwards engine envelopes to the web app unchanged. Host-only
connection information uses a separate wrapper and never enters the engine pipe:

```json
{
  "kind": "HostStatus",
  "payload": { "status": "connected", "attempt": 0, "message": "Engine connected" }
}
```

The web app sends commands to the host as
`{ "kind": "EngineEnvelope", "envelope": { ... } }`. The host validates the
envelope and payload before writing it to the engine.
