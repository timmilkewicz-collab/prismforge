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

## Mirror Cathedral and audio-readiness pass — 2026-09-22 late evening

- Seventeen scene shaders compiled at all four quality tiers with warnings
  treated as errors. The Release native build and 7/7 CTests passed; the
  Control UI build and 8/8 Vitest checks passed; 35/35 .NET protocol/origin
  tests passed. A build-target change copies current shader/manifest assets
  even when no C++ object was recompiled; the source/runtime Mirror Cathedral
  shader hashes matched after that rebuild.
- IPC smoke against a bounded `--no-audio --no-persist` Engine passed fixed
  1920×1080 output, Mirror Cathedral registration, both scene-control state
  updates and rejection of a late control for a different scene, reconnect,
  OSC command/gesture, and 20 enumerated audio sources. It did not establish
  an active audio signal.
- The isolated `PrismForge.EffectProbe` receiver verified stable identical
  pixels for fixed Mirror Cathedral inputs, and changed pixels for each of its
  four controls and synthetic bass, mids, highs and hit independently. With
  the final shader and synthetic fixture, average luminance was about 0.0838
  and peak RGB 144. This is a GPU-path check, not real-source music response.
- The supervised aggregate-only test found `system-default` loopback open but
  with no fresh sample-index advance, zero RMS/bands/hits over about 5.6 s
  without playback. Maono MIC In 1/2 delivered 96 distinct sample indices in
  about 5.8 s, but room-idle RMS averaged about 0.0001 (peak below 0.0004)
  with zero hits. Both deliberately failed the music-signal criterion. No raw
  audio was recorded and no user audio app was changed.
- A test-owned development Engine displayed Mirror Cathedral in the existing
  still-unsaved Arena composition. The first pass read as repetitive rings;
  a revised pass had broader faceted panels and cropped foreground masses.
  Changing Symmetry/Depth/Aperture/Line width to approximately
  8%/82%/22%/78% visibly produced a larger monolithic silhouette. A 50/50
  blend with Neon Orbs displayed but was dimmer than a full-deck cut. The
  Engine ran with audio/persistence disabled. Arena's output FPS, real music
  response, venue flash risk and one-hour soak remain unmeasured.
- The final shader was included in portable candidate
  `dist/PrismForge-alpha-20260922-220042`: all 49 SHA-256 entries verified,
  Launcher layout check exited 0, and exact-package IPC and Control-crash
  survival checks passed. Arena visibly displayed that package's bounded,
  audio-disabled Mirror Cathedral preview in the still-unsaved composition.
  An independent RTX 4070 Spout receiver counted 601 fresh 1920x1080 BGRA8
  frames in ten seconds, with spatial-grid changes in all three temporal
  windows (14/17/16). This verifies sender continuity and visible reception,
  not Arena output FPS, music response, flash safety, or show readiness.

## Standalone checkpoint and live-input meters — 2026-10-05

- A fresh Visual Studio 2026 Release build passed 7/7 native CTests. The
  Control passed 35/35 .NET tests, 8/8 UI tests and its production build. All
  17 scene shaders compiled across four quality tiers with warnings as errors.
  A bounded IPC smoke against the fresh Engine passed Mirror Cathedral's
  controls/stale-command guard, fixed 1080p output, OSC and reconnect.
- The independent `PrismForge.EffectProbe` passed all four deck effects,
  Mirror Cathedral's four controls and synthetic bass/mids/highs/hit pixel
  differences, four fixed-size output tiers, safety-tier frame gate, panic dim,
  blackout, macro differences and shader-reload rollback. These are isolated
  GPU-path checks, not a real-music visual pass.
- Control now clears stale live audio, FPS and Spout indicators when its Engine
  link is lost; the reconnect test requires fresh telemetry and keeps master
  macros unavailable until a current Engine snapshot arrives. It does not
  assume that the Engine itself stopped when Control loses IPC.
- Portable `dist/PrismForge-alpha-20261005-185711` passed 49/49 SHA-256
  manifest entries, exact-package IPC smoke and test-owned Control-crash
  survival. Neither test saved a show or modified PrismBurst.
- With music and the interface connected, this exact package's bounded
  aggregate-only probe measured **ACTIVE_SIGNAL** on `system-default` loopback:
  171 distinct sample indices in 10.8 seconds, mean RMS 0.0392, peak 0.3924,
  and 33 hit increments. The current `Maono MIC In 1/2 (3- Maono ProStudio
  2x2 lite)` input separately passed: 179 distinct sample indices in 10.7
  seconds, mean RMS 0.0178, peak 0.1360, and 31 hit increments. Exact endpoint
  identity was resolved from the current enumeration; no raw audio was saved
  and no audio routing was changed. These meter thresholds confirm live signal
  delivery to the analyzer, not that Mirror Cathedral visibly responds to
  music or that either route has ideal gain/beat quality.
- A controlled visual preview was attempted, but the Windows window-control
  helper could not bind to the test-owned Control window. No uncertain UI
  input or Arena manipulation was attempted. The exact test-owned Control and
  bounded Engine were stopped; real-music playback-versus-pause visual QA,
  Arena output FPS, mixed-load soak and flash-risk checks remain open.

## Supervised garage preview and recursive-visuals source candidate — 2026-10-05

- The operator put the exact `20261005-185711` package's Spout sender into a
  fresh, unsaved Resolume Arena composition and selected the current Maono
  MIC In 1/2 source in Control. The operator observed music-driven motion in
  Mirror Cathedral. Engine OSC status over eight music-playing seconds showed
  59.9–60.1 fps at tier 0, RMS 0.0055–0.2034 and Spout ready. An independent
  receiver counted 600 fresh fixed-size 1920x1080 BGRA8 frames in ten seconds;
  Arena's Output Monitor visibly showed the scene. Neither a controlled
  playback-versus-pause comparison nor Arena's own output fps was measured.
- The first Engine had a 30-minute preview limit. With the operator confirming
  the setup was off-air, only that verified Engine was stopped gracefully; the
  same package was restarted without `--seconds` and with `--no-persist`.
  Resolume recovered the sender. The operator reselected Maono after the
  restart; a subsequent live RMS/fps sample confirmed signal delivery. This
  continuity check does not resolve the earlier transient Spout warning under
  rapid replacement or establish a one-hour soak.
- On new local branch `codex/recursive-visuals`, Neon Orbs and Shardwell use
  bounded audio-contour knees matched to ordinary room-mic band levels.
  Recursive Circuit is a new original five-octave-at-tier-0 folded field with
  four scene controls and reduced octave counts in degraded tiers. The
  existing live package and Engine were not modified by these source edits.
- All 18 manifest shaders compiled across four tiers with `/WX` (72 passes).
  A fresh Release Engine build and 7/7 native CTests passed. The isolated RTX
  EffectProbe received distinct Recursive Circuit pixels, verified each of
  its four controls and bass/mids/highs/hit independently, preserved fixed
  1080p output through all quality tiers, and passed blackout, panic-dim and
  hot-reload rollback checks. Quiet-to-music-range fixtures at a fixed time
  changed 139/576 sampled pixels for Neon Orbs, 134/576 for Shardwell and
  458/576 for Recursive Circuit; average frame luminance changed by 0.0035,
  0.0045 and 0.0096 respectively. BMP stills under `build/visual-preview-20261005`
  are local review artifacts, not bundled scene assets. At this source
  checkpoint the new scene had not yet run into Resolume. GPU completion
  timing, Arena output fps, flash-risk monitoring and extended stability
  remain open.

## Recursive Circuit first Arena audition and response pass — 2026-10-05 late PT

- After the operator closed Control, the older `20261005-185711` Engine was
  still the active sender, as designed. Its exact executable path and OSC
  ownership were verified before it received an isolated console Ctrl+C.
  The hash-verified `20261005-224222` Engine and matching Control were then
  opened with `--no-persist`; no PrismBurst process, shortcut or saved Arena
  composition was changed.
- Recursive Circuit was selected on deck A through the new Engine's loopback
  OSC port. An independent receiver observed 601 fresh 1920x1080 BGRA8 frames
  in ten seconds. Engine status sampled about 60 fps at tier 0 and later
  measured nonzero music RMS. The currently playing NestDrop clip on Arena's
  higher layer was stopped, not removed, to expose PrismForge by itself.
  Arena's Output Monitor then visibly showed the circuit field. The operator
  judged it more complex and cool, but still far below NestDrop in
  music-driven movement, fluidity and dramatic color/shape transitions.
- Inspection found the first shader's movement mostly free-running, its
  grouped-band influence small, its hit accent only one render frame, and its
  palette fixed per octave. The follow-up source pass derives a bounded
  visual envelope from real-scale RMS while preserving raw SignalFrameV1
  metering, decays onsets over 300 ms, integrates motion speed from music,
  and eases phrase-level palette/topology changes. The first alpha remains
  the live rollback until the new package passes its own audition.
- In source, all 18 shaders compiled at four tiers with `/WX`; the Release
  Engine built and 7/7 native CTests passed. An isolated RTX EffectProbe using
  RMS 0.005 versus 0.06 and grouped bands in the 0.002–0.018 range observed
  480/576 changed pixels for quiet versus music, 208/576 at 100 ms after an
  onset, and 454/576 at 800 ms into a shape/palette phrase. Mean-luminance
  differences were 0.0163 and 0.0205 for the music and phrase comparisons;
  the onset-only luminance difference fell from 0.000804 at 100 ms to 0.000059
  at 600 ms. The probe also passed fixed 1080p output, effects, tier, safety,
  blackout and shader-reload checks. Stills under
  `build/visual-preview-20261005-v2` show a clear rail-to-loop and
  cyan/violet/amber-to-green/red/blue change. These are synthetic fixtures,
  not a live music-to-pixels or flash-safety pass for the new Engine.

## Reactive Recursive Circuit portable alpha in Arena — 2026-10-05 late PT

- The full Release native build and all 7/7 CTests passed, followed by a
  separate `dist/PrismForge-alpha-20261005-233633` portable package with
  50/50 SHA-256 files verified. The exact package passed IPC smoke, including
  fixed 1080p sender state, scene controls, reconnect and OSC. The isolated
  RTX EffectProbe and 18 shaders × four tiers had passed before packaging.
- With the operator's permission for a brief off-air restart, the exact older
  `20261005-224222` PrismForge Control closed via its window and its verified
  Engine exited on console Ctrl+C. The replacement Engine and Control were
  launched from the new package with `--no-persist`. Deck A was set to
  Recursive Circuit, crossfader to A and audio to the currently enumerated
  Maono MIC In 1/2 endpoint. No PrismBurst process, shortcut or source was
  changed; the Arena composition remains unsaved.
- The independent Spout receiver connected to `PrismForge` and counted 597
  fresh 1920×1080 BGRA8 frames in ten seconds on the RTX 4070. OSC status
  sampled 59.88–60.15 Engine fps at tier 0 with Spout ready. The active
  NestDrop clip on Arena's higher Layer 3 obscured Forge; its playing clip
  was stopped, not deleted, and Arena's Output Monitor then visibly showed
  Recursive Circuit. This is Arena visual reception, not Arena output-FPS
  measurement.
- The Maono capture connected and reported fresh blocks. An early quiet
  11-second status sample had RMS 0.00049–0.00571; a later 12-second sample
  during the operator's positive visual reaction ranged 0.02195–0.84279 RMS
  while Engine cadence stayed near 60 fps. The operator said the result was
  a real improvement but still nowhere near NestDrop's movement and evolving
  scenes. There was no controlled playback-versus-pause comparison or raw
  audio recording, and the high RMS warrants a gain/clipping check.
- Windows reported 92.7–94.2% physical memory use (roughly 1.8–2.3 GB free)
  during the preview; no process was killed or system setting changed. The
  live Engine remains running for supervised play. Flash-risk monitoring,
  Arena output FPS, mixed-load soak and show clearance remain open.

## Flow Echo source-only GPU candidate — 2026-10-06 PT

- The renderer now has an opt-in, Recursive Circuit-only advection of its
  existing per-deck Feedback history. Feedback at zero and non-Recursive
  scenes use the original effect sampling path; no new textures, output
  dimensions, show fields or controller mapping were added. This code is not
  in the live `20261005-233633` Engine.
- Offline Release Engine build, 7/7 native CTests and all 18 scene shaders ×
  four tiers passed. The independent RTX EffectProbe passed with fixed 1080p
  output, tier resize/scene-switch safety, bounded luma (0.0721 at 75% and
  0.0904 at full internal scale in that check), blackout, panic-dim, invalid
  shader-reload rollback and the 30-fps tier gate. Matched fresh senders
  proved a non-Recursive scene produced an exactly equal full-frame hash when
  only its unused Flow parameter differed.
- Against matched Feedback-off traces, Feedback-on quiet/music/onset frames
  changed 114/303/322 of 576 grid samples. With Feedback on, quiet-to-music,
  onset and temporal comparisons changed 497/456/517 of 576 samples,
  respectively, and selected frame luminance remained finite and bounded.
  These comparisons establish response and containment, but do not isolate
  the small curl displacement from ordinary Feedback plus the scene's Flow
  shader; live visual value must still be judged. No Maono-to-Arena audition,
  Arena output-FPS measurement or flash-risk validation has been done for
  this candidate.
- The same Release Engine bytes were copied into separate portable folder
  `dist/PrismForge-alpha-20261006-001115`, and all 50 SHA-256 manifest entries
  reverified. Its Engine SHA-256 is
  `8d99e35de9052c7f4ebab30172e57705465040668e02f6beae441343fd814aaa`;
  the still-live `233633` Engine has different bytes. The new folder has not
  passed exact-package IPC or Arena reception and must not be treated as a
  live replacement on the basis of the source/isolated GPU checks.

## Deterministic musical-state source candidate — 2026-10-06 PT

- Work was isolated in a managed worktree on branch
  `codex/musical-interpretation`, based on the existing Recursive Circuit/Flow
  Echo source at `f77230d`. The running
  `PrismForge-alpha-20261005-233633` Engine was observed but not stopped,
  replaced, signaled or rebuilt in place. PrismBurst and saved Resolume
  compositions were not touched.
- A fresh Visual Studio 2026 Release configure/build completed, and native
  CTest passed 8/8. The new musical-state test replays eight complete
  `SignalFrameV1` CSV timelines twice and after reset, checks canonical
  explicit-field hashes, verifies semantic changes over time, and covers
  duplicates, large gaps, clock/counter rewinds, held event flags, real sample
  rate changes, malformed/clipped inputs, seed isolation and 120,000 bounded
  updates. Regenerating the eight fixtures produced byte-identical files.
- All 18 scene shaders compiled at all four quality tiers with warnings as
  errors (72 combinations). The Control build passed 8/8 Vitest checks,
  produced its release frontend, built the .NET host with zero warnings or
  errors, and passed 35/35 xUnit checks. `npm audit` still reports one existing
  high-severity dependency advisory; no broad dependency update was attempted.
- Review removed a non-periodic Recursive flow modulo that would have caused a
  long-show topology jump, made committed slow-state age monotonic while a
  challenger satisfies dwell, and expanded sample-rate and matched-role probe
  coverage. Release Engine and EffectProbe targets compile after those changes.
- The EffectProbe executable, IPC smoke, Control-crash survival and any
  candidate Engine were deliberately not run while the known-good Engine owned
  the PrismForge sender, pipe and OSC endpoints. Therefore fixed 1080p sender
  behavior, received GPU differences for quiet/groove/build/peak/release,
  blackout, panic dim, adaptive tiers, shader-reload rollback, real audio and
  Arena reception are not newly proven for this candidate. They remain
  explicit off-air acceptance gates rather than inferred successes.
- Portable staging then created
  `dist/PrismForge-alpha-20261006-011833-musical-state-candidate` without
  launching it. All 51 SHA-256 manifest entries verified and the Launcher
  `--check-layout` process exited 0. The packaged Engine SHA-256 is
  `7a97ae651919acf35e07cb09961d3b2a8c6b8905a34166fd969162b5b0c198c6`.
  Process inspection afterward still showed only the known-good
  `20261005-233633` Engine (`--no-persist`) running.
