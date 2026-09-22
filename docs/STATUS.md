# Implementation status

This is an alpha work log. A source build or a synthetic fixture is not proof
that Resolume, an audio interface, or the installed live rig works.

## Implemented in source

- Two scene decks, crossfader, blackout and panic-dim state.
- Twelve original procedural HLSL scenes with a SceneManifestV1 asset.
- Fixed 1920×1080 BGRA8 D3D11 output and `PrismForge` Spout sender.
- Four actual per-deck GPU effects: Bloom, Feedback, Kaleidoscope and Pixelate.
- Four adaptive internal-resolution/frame-rate tiers.
- Asynchronous WASAPI loopback and selectable input capture through pinned
  miniaudio, using exact endpoint identities instead of device-list positions.
- 256-sample transient envelopes and 2048-sample/75%-overlap FFT analysis.
- Beat/bar-quantized cue storage and a 64-slot modulation route model.
- Atomic `%APPDATA%\PrismForge` autosave, portable `ShowBundleV1` save/load,
  and restore validation. A rejected autosave is preserved under a unique
  adjacent name before writes resume; if preservation fails, writes stay off
  for that run. Test/smoke runs can opt out with `--no-persist`.
- Loopback-only OSC commands/status and queued PrismBurst `/prism/gesture`
  inputs, plus sanitized read-only PrismBurst health telemetry.
- Opt-in Launchpad S WinMM input and mapped-only LED feedback, with 50 unique
  bindings, guarded cue saves, conservative PrismBurst/port-contention gates,
  and one-way runtime yield if PrismBurst subsequently appears.
- Bounded audio/command queues and length-prefixed versioned local IPC.
- Local React/WebView2 control surface with bundled assets, local-origin
  navigation/message policy and no remote runtime dependency.

## Validation achieved

- A separate RTX 4070 D3D11→Spout spike sent 720 frames in 12 seconds at
  1920×1080 BGRA8. An independent Spout receiver on the same host observed 360
  fresh frames over six seconds and 29 changed center-pixel samples.
- Twelve HLSL scene passes compile as `ps_5_0` across four tier definitions
  with warnings treated as errors.
- A deterministic independent receiver saw distinct GPU pixels for all four
  deck effects at full strength. Changing all internal quality tiers left the
  Spout sender at 1920×1080 BGRA8, including the 30 fps safety gate.
- A short Engine run held about 60 fps at tier 0, and pipe commands/cue recall
  survived a control-client disconnect/reconnect.
- Autosave wrote a validated `ShowBundleV1` file and restored it on restart;
  `--no-persist` did not change its timestamp.
- Seven Release native tests pass, including always-on Launchpad mapping,
  audio handoff, and rejected-autosave assertions. Twenty-three .NET control
  tests pass. A test-owned Control process was force-terminated while the
  Engine remained live, then a new IPC client read the 1080p sender state.

## Not yet accepted for live use

- Resolume reception and the 30-minute fixed-format smoke test.
- One-hour two-deck, overlay, audio and controller soak.
- Real FL Studio/system loopback, Maono/interface and room-PA validation.
- Physical Launchpad S input/LED colors, coexistence and reconnect; generic
  MIDI learn is not implemented. Live OSC/PrismBurst hardware validation also
  remains pending even though both bridges are in source.
- Four independent overlay slots, arbitrary media/webcam/Spout receiver.
- Curated palettes and full effects catalog.
- GPU luminance-change risk meter, venue-safe profile and soft limiter.
- ShowBundleV1 migration beyond v1, A/B undo and recording.
- Latency measurement, audio/video recording verification and installer.

No existing PrismBurst runtime, checkout or shortcut is modified by this repo.
