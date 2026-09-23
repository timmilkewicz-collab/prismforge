# Local control protocol v1

The only control transport in this alpha is the local named pipe
`\\.\pipe\PrismForge.v1`. Remote pipe clients are rejected. Each message is
a four-byte unsigned little-endian byte count followed by UTF-8 JSON (maximum
65,536 bytes). The envelope is:

```json
{"version":1,"type":"Command","requestId":"optional","payload":{"action":"setCrossfader","value":0.5}}
```

Engine→control types are `StateSnapshot`, `SignalFrame`, and `ErrorEvent`.
The engine sends an initial snapshot on connect, then signal frames roughly
every 50 ms and snapshots roughly every 250 ms. The control window may exit or
reconnect without killing or resetting the engine. A bounded 128-command queue
is the only path to show-state mutation; audio callback threads also publish to
a bounded queue and do not mutate show state.

Commands currently accepted: `requestSnapshot`, `setScene` (`deck` A/B,
`sceneId`), `setCrossfader` (`value` 0–1), `setBlackout` and `setPanicDim`
(`enabled`), `setEffect` (`deck`, `effectIndex` 0–3, `amount`), `setModulation`
(`deck`, `slot` 0–63, `source`, `target`, `amount`, `smoothing`, `enabled`),
`saveCue` and `recallCue` (`index` 0–31; recall optionally includes
`quantization`: `immediate`, `beat`, or `bar`), and `setAudioSource` (`id`).
Audio source IDs are stable WASAPI endpoint identities rather than positional
enumeration numbers; see [audio switching](AUDIO_SWITCH.md). Source-switch
acceptance is asynchronous: a successful queue request does not change the
selected source until a new stream has actually started. A failed switch emits
an `ErrorEvent` while retaining the prior source.
`setMasterEffect` (`index` 0–3, `amount` 0–1) controls Motion, Warp, Trails
and Color in the renderer. All four default to zero and are stored in cues and
show bundles. `saveShow` (`name`, 1–64 safe filename characters) queues an atomic portable
bundle under `Documents\PrismForge\Shows`; `loadShow` loads a validated bundle
by the same name. `setOverlay` updates its state slot, but overlay rendering
is still under development. `reloadShaders` compiles all scene passes before swapping
the live set; a failure keeps the prior valid set and reports `ErrorEvent`.

`StateSnapshot` includes revision, A/B deck scene/effect/modulation state,
crossfader, four `masterEffects` amounts, blackout, panic dim, scene catalog, audio sources/levels, cue
presence, performance tier/fps, and fixed Spout output format. `SignalFrame`
contains transient, spectrum, beat and performance values. Unsupported,
malformed, or overlong input is rejected. This is a local UI protocol, not an
authenticated LAN API.

The schema is still v1 alpha: show files validate/restorable in v1, but
migration from future schemas is not implemented. Do not use this IPC alone as
a durable state store.
