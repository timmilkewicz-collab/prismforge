# Validation record — 2026-09-22

This record separates native build, synthetic probes, actual device startup,
and live-show acceptance. All tests were on the user's Windows RTX 4070 rig;
no installed PrismBurst files, launchers or shortcuts were changed.

## Build and deterministic checks

- Visual Studio Build Tools 2026 / Windows SDK 10.0.26100: Release x64 native
  build passed with pinned Spout2 source and pinned vcpkg manifest ports.
- Native CTest passed 5/5: core, OSC parser, Launchpad S mapping/LED behavior,
  and two Launcher layouts (including `--launchpad`). Checks are always-on in
  Release builds.
- All twelve original `ps_5_0` scene shaders compiled with `/WX` for all four
  quality-tier definitions (48 shader/tier combinations).
- Control frontend build and four Vitest checks passed; WPF WebView2 host built
  with no warnings/errors and 22 protocol/origin-policy xUnit checks passed.
  The production page is bundled and its CSP disallows network connections.
- A deterministic RTX Spout receiver saw distinct pixels for Bloom, Feedback,
  Kaleidoscope and Pixelate at full amount versus baseline. Each internal
  resolution tier retained external 1920×1080 BGRA8, including 720p/30 safety.
- The same isolated probe measured panic-dim luminance 0.00399 versus normal
  0.02379, blackout maximum RGB 0, and a failed HLSL reload retaining the
  prior output hash. Restoring the valid shader reloaded successfully.

## Short runtime checks

- D3D11→Spout spike: 720 sender frames in 12 s (60.00 fps average); independent
  receiver observed 360 fresh frames in 6 s and 29 changed samples.
- Full Engine: 10 s `--no-audio --no-persist` run stayed near 60 fps at tier 0;
  independent receiver observed 301 fresh frames in 5 s and 24 changed samples.
- Local pipe integration: scene/crossfader commands, cue save/recall, invalid
  scene error, UI-client disconnect/reconnect and output state all passed.
- OSC integration: port 12100 crossfader control and a PrismBurst-format
  `/prism/gesture ,sfff` packet on 12002 appeared in Engine signal frames.
- Audio sources: Engine enumerated 20 entries, selected the Maono input and
  reported capture connected. Default system WASAPI loopback also initialized.
  No music/PA transient-response measurement was performed.
- Autosave wrote and then restored `ShowBundleV1` under
  `%APPDATA%\PrismForge\autosave.json`; `--no-persist` left its timestamp intact.
- The UI-crash test force-terminated only its own Control process. The same
  Engine stayed alive and a new pipe client reconnected to its 1080p sender
  state. The full post-MIDI IPC smoke also passed with the Maono input selected.
- The portable alpha was built under `dist/PrismForge-alpha-20260922-154338`.
  All 33 SHA-256 manifest entries verified, the Launcher layout check passed,
  and both IPC smoke and UI-crash survival passed against the packaged binaries.
  This folder is local test output, not an installed live release.
- Control stayed alive offline, connected to a brief live Engine run, and
  returned to its disabled offline state after Engine exited. A second Control
  launch reused the single existing UI instance.

## Outstanding acceptance gates

- Resolume Arena has not yet been confirmed as a receiver, nor has the
  30-minute Spout gate or one-hour full live soak run.
- No live FL Studio/music/PA, Kinect body, physical Launchpad S input/LED, or
  recording acceptance test has been completed. MIDI ports were not opened
  during automated tests; default startup keeps them closed.
- Transient-to-pixel latency has not been measured with timestamped fixtures.
- `ShowBundleV1` migration from older/newer schema versions is not implemented.
- This is not a medically or regulatorily certified flash-safety system.

The separate installed PrismBurst runtime remains the fallback until live
acceptance, rollback packaging and hash checks are complete.
