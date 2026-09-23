# Implementation status

This is an alpha work log. A source build or a synthetic fixture is not proof
that Resolume, an audio interface, or the installed live rig works.

## Implemented in source

- Two scene decks, crossfader, blackout and panic-dim state.
- Sixteen original procedural HLSL scenes with a SceneManifestV1 asset,
  including the independently authored Hex Vortex, Ferrofluid Reactor,
  Shardwell and Neon Orbs. The latter two use the operator's video library as
  visual direction; no source footage or frames are included.
- Fixed 1920×1080 BGRA8 D3D11 output and `PrismForge` Spout sender.
- Four actual per-deck GPU effects: Bloom, Feedback, Kaleidoscope and Pixelate.
- Four default-off master performance controls: Motion, Warp, Trails and Color,
  exposed in Control and retained in show snapshots/cues. Control disables
  these macros when an older Engine snapshot lacks them.
- Four adaptive internal-resolution/frame-rate tiers. The renderer now
  compiles and selects quality-specific scene variants at runtime. Its current
  governor observes CPU render submission time, not GPU completion time.
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
- Sixteen HLSL scene passes compile as `ps_5_0` across four tier definitions
  with warnings treated as errors.
- A deterministic independent receiver saw distinct GPU pixels for all four
  deck effects at full strength. Changing all internal quality tiers left the
  Spout sender at 1920×1080 BGRA8, including the 30 fps safety gate.
- A short Engine run held about 60 fps at tier 0, and pipe commands/cue recall
  survived a control-client disconnect/reconnect.
- Autosave wrote a validated `ShowBundleV1` file and restored it on restart;
  `--no-persist` did not change its timestamp.
- Seven Release native tests pass, including always-on Launchpad mapping,
  audio handoff, and rejected-autosave assertions. Twenty-nine .NET control
  tests pass. A test-owned Control process was force-terminated while the
  Engine remained live, then a new IPC client read the 1080p sender state.
- Resolume Arena 7.6 visibly received the packaged Engine's 1920x1080
  `PrismForge` Spout output at checkpoints spanning 30 minutes 10 seconds. An
  independent receiver ran concurrently for 1,800 seconds and passed with
  107,997 fresh frames
  (about 60 fps), 8,841 changed-pixel samples and fixed BGRA8 format. This
  run had audio disabled; the probe does not measure Arena's output FPS.
- A later 16-scene development build displayed the four recent procedural
  looks and a manual crossfade in the still-unsaved Arena composition. Two
  independent 30-second receiver checks observed 1,800 and 1,798 new frames
  at fixed 1920×1080 BGRA8 while Arena stayed responsive. Audio and physical
  controller validation were not part of these checks.

## Not yet accepted for live use

- Arena's own output FPS measurement and end-to-end 1080p60 proof.
- One-hour two-deck, overlay, audio and controller soak.
- Repeated Arena reconnect stability after the observed Spout interop warning,
  plus Arena's own output-FPS measurement for the new portal scenes.
- Real FL Studio/system loopback, Maono/interface and room-PA validation.
- Physical Launchpad S input/LED colors, coexistence and reconnect; generic
  MIDI learn is not implemented. Live OSC/PrismBurst hardware validation also
  remains pending even though both bridges are in source.
- Four independent overlay slots, arbitrary media/webcam/Spout receiver.
- Curated palettes and full effects catalog.
- GPU luminance-change risk meter, venue-safe profile and soft limiter.
- GPU-timestamp-driven adaptive quality and nonblocking shader hot reload.
- ShowBundleV1 migration beyond v1, A/B undo and recording.
- Latency measurement, audio/video recording verification and installer.

No existing PrismBurst runtime, checkout or shortcut is modified by this repo.
