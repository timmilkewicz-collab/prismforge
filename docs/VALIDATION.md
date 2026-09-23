# Validation record — 2026-09-22

This record separates native build, synthetic probes, actual device startup,
and live-show acceptance. All tests were on the user's Windows RTX 4070 rig;
no installed PrismBurst files, launchers or shortcuts were changed.

## Build and deterministic checks

- Visual Studio Build Tools 2026 / Windows SDK 10.0.26100: Release x64 native
  build passed with pinned Spout2 source and pinned vcpkg manifest ports.
- Native CTest passed 7/7: core, OSC parser, Launchpad S mapping/LED behavior,
  rejected-autosave recovery, fake-device audio handoff, and two Launcher
  layouts (including `--launchpad`). Checks are always-on in Release builds.
- All twelve original `ps_5_0` scene shaders compiled with `/WX` for all four
  quality-tier definitions (48 shader/tier combinations).
- Control frontend build and four Vitest checks passed; WPF WebView2 host built
  with no warnings/errors and 23 protocol/origin-policy xUnit checks passed.
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
- Audio sources: before the asynchronous-switch hardening, Engine enumerated
  20 entries, selected the Maono input and reported capture connected. Default
  system WASAPI loopback also initialized. The new switch/hot-unplug path has
  only fake-device test coverage; no music/PA transient-response measurement
  was performed.
- Autosave wrote and then restored `ShowBundleV1` under
  `%APPDATA%\PrismForge\autosave.json`; `--no-persist` left its timestamp intact.
- The UI-crash test force-terminated only its own Control process. The same
  Engine stayed alive and a new pipe client reconnected to its 1080p sender
  state. The full post-MIDI IPC smoke also passed with the Maono input selected.
- The original portable alpha was built under
  `dist/PrismForge-alpha-20260922-154338`.
  All 33 SHA-256 manifest entries verified, the Launcher layout check passed,
  and both IPC smoke and UI-crash survival passed against the packaged binaries.
  This original folder was renamed with `-SUPERSEDED-DO-NOT-RUN` after later
  safety fixes; its files were preserved, not deleted.
- The replacement portable alpha is
  `dist/PrismForge-alpha-20260922-161109`. Its 33 SHA-256 manifest entries,
  Launcher layout, packaged-binary IPC smoke and UI-crash survival all passed.
  The IPC smoke enumerated 20 audio sources without opening the Maono input.
  This is local test output, not an installed live release.
- Control stayed alive offline, connected to a brief live Engine run, and
  returned to its disabled offline state after Engine exited. A second Control
  launch reused the single existing UI instance.
- Resolume Arena 7.6 received the packaged Engine's `PrismForge` Spout sender
  in a fresh 1920x1080 composition. Its Output Monitor and Display
  showed changing Ink Tide frames at checkpoints from 2026-09-23 01:23:18 UTC
  through 01:53:28 UTC (30 minutes 10 seconds). A separate receiver completed
  a concurrent 1,800-second probe with exit code 0: 107,997 fresh frames
  (about 60 fps), 8,841 changed-pixel samples, and fixed 1920x1080 BGRA8
  format on the RTX 4070 Laptop GPU. Arena and Engine remained running with
  stable working sets through the probe. The test Engine ran without audio or
  persistence and was stopped afterward; Arena and the composition were left
  open. All eight pre-existing `.avc` composition files matched their
  pre-test hashes. A new composition file dated before the timed test appeared
  and was preserved; its origin was not determined. The probe's cadence is
  not a measurement of Arena's output FPS.

## Outstanding acceptance gates

- Arena's own output FPS has not been measured, so end-to-end 1080p60 is not
  yet proven. The one-hour two-deck/overlay/audio/controller live soak has not
  run.
- The hardened audio switch needs a supervised loopback/Maono/hot-unplug test;
  the default IPC smoke does not open a physical input.
- No live FL Studio/music/PA, Kinect body, physical Launchpad S input/LED, or
  recording acceptance test has been completed. MIDI ports were not opened
  during automated tests; default startup keeps them closed.
- Transient-to-pixel latency has not been measured with timestamped fixtures.
- `ShowBundleV1` migration from older/newer schema versions is not implemented.
- This is not a medically or regulatorily certified flash-safety system.

The separate installed PrismBurst runtime remains the fallback until live
acceptance, rollback packaging and hash checks are complete.

## Weekend-teaser candidate checks — later on 2026-09-22

- The scene catalog expanded to 14 original shaders. All 56 shader/tier
  combinations compiled with `/WX`; Release native build and 7/7 CTests
  passed. The Control UI build and 6/6 Vitest checks passed; 29/29 .NET
  protocol/origin tests passed after adding `setMasterEffect` validation.
- An IPC smoke against the rebuilt Engine passed for the new Hex Vortex and
  Ferrofluid Reactor IDs, all four master-effect snapshot values, reconnect,
  OSC and the fixed 1080p sender. It enumerated 20 audio sources but did not
  open a physical input or use music.
- An independent RTX Spout pixel probe saw distinct output for both new
  scenes and all four master performance controls. It also passed the four
  pre-existing deck-effect differences, four fixed-output quality tiers,
  safety 30 fps gate, panic dim, blackout and hot-reload rollback. The probe
  now publishes only as `PrismForge.EffectProbe` so it cannot take the live
  sender name; this change was validated after a prior same-name probe had
  been run while Arena was open.
- The first visual pass in Arena displayed correctly but was judged too
  sparse/dim: thin hex outlines and a mostly empty ferrofluid ring. Both
  shaders were reworked with broad surfaces and denser motion. After a rapid
  sender restart, Arena showed a Spout "Cannot create DirectX/OpenGL interop"
  warning and temporarily became unresponsive. Only the test-owned Engine
  was stopped; Arena recovered on its own without being closed or restarted,
  preserving the unsaved composition. The fault's cause is not established.
- A separate new portable alpha, `dist/PrismForge-alpha-20260922-200202`,
  verified all 36 SHA-256 entries, Launcher layout, packaged IPC smoke and
  UI-crash survival. Its Engine then reconnected to the existing Arena session
  and displayed both second-pass portal scenes in the Output Monitor/Display.
  A concurrent independent receiver observed 600 fresh 1920×1080 BGRA8
  frames with 49 changing samples in ten seconds. Arena remained responsive.
  This is a short visual/reconnect check, not a one-hour soak; Arena's output
  FPS, real audio response, venue flash risk and controller behavior remain
  unmeasured. The transient interop fault still blocks show clearance.

## Operator-media art pass — 2026-09-22 evening

- Inspected four operator-supplied Resolume clips as visual references only:
  `164683 (Original).mp4`, `BC19_1.mov`, `BC20_1.mov`, and `BC6_1.mov`.
  Originals were not edited, copied into the repository, or packaged. The
  original new scenes Shardwell and Neon Orbs emphasize contrasting dense
  faceted debris and sparse glossy emissive forms; the existing portal looks
  were retained as separate directions.
- Source now contains 16 registered scenes. All 64 HLSL quality variants
  compiled with warnings treated as errors. The runtime now compiles and
  selects tier-specific scene shaders, rather than merely changing internal
  render resolution. Release build, 7/7 CTests, 29/29 .NET tests, 6/6 UI tests,
  and IPC smoke for all four recent scene IDs passed.
- The isolated RTX 4070 Spout pixel probe received distinct frames for Hex
  Vortex, Ferrofluid Reactor, Shardwell and Neon Orbs; checked all deck effects
  and performance macros, stable 1920×1080 BGRA8 output across quality tiers,
  the 30 fps safety gate, panic dim, blackout, and invalid/valid hot reload.
  It now paces GPU readback so a prior scene frame cannot falsely pass as the
  current one.
- The built Engine displayed all four recent scenes in the still-unsaved Arena
  composition. A live QA pass found the first Shardwell and Neon Orbs variants
  too small/dim; both were retuned and hot-reloaded. Arena then displayed
  larger readable faceted blocks, brighter glossy forms, and a blended scene
  with conservative Motion/Warp/Trails/Color. Two 30-second independent Spout
  probes during this session counted 1,800 and 1,798 fresh frames respectively,
  with unchanged 1920×1080 BGRA8 format. Engine telemetry held tier 0 near
  60 fps. Arena remained responsive; its own output FPS was not measured.
- The timed global color step in Hex Vortex and the renderer's one-hour shader
  time wrap were removed after code review. This reduces obvious discontinuity
  risks but is **not** a measured flash-safety clearance. Audio was disabled,
  there was no venue preview, and the earlier Arena Spout interop warning is
  still not root-caused. The retuned scenes are a development candidate, not
  approved show output.
- A new portable candidate at `dist/PrismForge-alpha-20260922-205129` was
  created after the final scene retunes. All 46 packaged SHA-256 entries
  verified, Launcher layout returned exit 0, packaged IPC smoke passed for
  all four recent scenes, and packaged Control-crash survival passed. The
  exact packaged Engine and Control were then started with audio/persistence
  disabled; Arena reconnected and displayed Shardwell. A 10-second receiver
  probe of that packaged sender counted 601 fresh 1920×1080 BGRA8 frames and
  49 changed spatial-grid samples. The earlier center-only probe had falsely
  failed Shardwell because its central well is deliberately black; the grid
  probe fixed that test limitation. The preview Engine is bounded to 900
  seconds and is not a show deployment.
- A subsequent probe review found that comparison against only the first
  frame could pass one early change followed by a freeze. The receiver now
  compares adjacent grid samples and, for runs of at least six seconds,
  requires changes in each third. Against the packaged live sender, a
  ten-second check passed with 601 fresh frames and change counts of
  16/17/16 across the three windows. This remains sender continuity evidence,
  not Arena output-FPS or flash-risk evidence.
- A mixed-version review found that an earlier Engine accepted master-effect
  commands without rendering the new macros. Control now treats absence of a
  four-value `masterEffects` field in each StateSnapshot as unsupported,
  clears the displayed macro values, and disables their controls. A UI test
  covers a transition from a current to a legacy snapshot. Exact package
  Engine-path verification remains a separate preflight gate.
- Final package candidate `dist/PrismForge-alpha-20260922-210828` passed all
  46 SHA-256 manifest checks, Launcher layout exit 0, exact packaged Engine
  IPC smoke (four scenes, macros, OSC, reconnect, 20 enumerated audio sources)
  and packaged Control-crash survival. No operator video files were bundled.
  The first IPC smoke attempt timed out at its old eight-second startup
  deadline while the Engine compiled 64 shader variants; after increasing
  the test's bounded cold-start deadline to 30 seconds, the same package
  passed. This does not prove startup under eight seconds, physical audio,
  Arena output FPS, or show readiness.
