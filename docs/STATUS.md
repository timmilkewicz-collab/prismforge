# Implementation status

This is an alpha work log. A source build or a synthetic fixture is not proof
that Resolume, an audio interface, or the installed live rig works.

## Implemented in source

- Two scene decks, crossfader, blackout and panic-dim state.
- Eighteen original procedural HLSL scenes with a SceneManifestV1 asset,
  including the independently authored Hex Vortex, Ferrofluid Reactor,
  Shardwell, Neon Orbs, Mirror Cathedral and Recursive Circuit. Shardwell,
  Neon Orbs and Mirror Cathedral use the
  operator's video library as visual direction; no source footage or frames
  are included. Mirror Cathedral exposes live symmetry, depth, aperture and
  line-width controls per deck. Recursive Circuit is an original folded
  liquid-circuit field with Branching, Flow, Depth and Charge controls. Its
  first candidate was auditioned in Arena; the operator liked its added
  complexity but found its motion, fluidity and color/shape changes still far
  short of NestDrop. The later `20261005-233633` portable alpha adds a
  render-thread music envelope, a decaying onset, an audio-integrated flow
  clock and eased palette/topology phrases. It is now under supervised live
  audition, not show-cleared.
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
- Capture-open and fresh-block status are reported separately. A disconnect or
  250 ms block-delivery stall clears stale analyzer levels; resumed capture
  publishes a recovery event. A bounded aggregate-only audio check can test
  a selected physical endpoint without recording raw audio.
- 256-sample transient envelopes and 2048-sample/75%-overlap FFT analysis.
- Beat/bar-quantized cue storage and a 64-slot modulation route model.
- Four normalized live scene parameters are retained in cues and ShowBundleV1;
  older v1 bundles without them load at neutral values.
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
- Eighteen HLSL scene passes compile as `ps_5_0` across four tier definitions
  with warnings treated as errors.
- The isolated GPU receiver confirms Mirror Cathedral's four live controls and
  synthetic bass/mids/highs/hit inputs each change pixels. This does not yet
  prove visible response from FL Studio or the Maono input.
- A deterministic independent receiver saw distinct GPU pixels for all four
  deck effects at full strength. Changing all internal quality tiers left the
  Spout sender at 1920×1080 BGRA8, including the 30 fps safety gate.
- A short Engine run held about 60 fps at tier 0, and pipe commands/cue recall
  survived a control-client disconnect/reconnect.
- Autosave wrote a validated `ShowBundleV1` file and restored it on restart;
  `--no-persist` did not change its timestamp.
- Seven Release native tests pass, including always-on Launchpad mapping,
  audio handoff/recovery, and rejected-autosave assertions. Thirty-five .NET control
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
- A 17-scene development build passed its IPC smoke for Mirror Cathedral's
  scene controls and stale-command guard, along with fixed 1080p output and
  reconnect. A short Arena visual QA confirmed the sender appeared in the
  existing unsaved composition, and a second art pass produced larger faceted
  panels and a visibly different low-symmetry silhouette. The final shader
  was packaged in `dist/PrismForge-alpha-20260922-220042`; its 49 hashes,
  packaged IPC/control-survival checks, Arena reception, and a ten-second
  601-frame independent Spout continuity probe passed. The package remains a
  visual-only development candidate, not show-cleared output.
- With no music playing, the default system loopback opened but delivered no
  fresh blocks. Maono MIC In 1/2 delivered fresh blocks near its idle noise
  floor (about 0.0001 RMS) and no hit events. Neither is a music-signal pass.
- After the operator connected music and the interface on 2026-10-05, the
  current portable package passed separate aggregate-only `ACTIVE_SIGNAL`
  checks on system-default loopback and the currently enumerated Maono MIC
  In 1/2 input. The exact-package IPC and Control-crash checks, 49 package
  hashes, 7 native, 35 .NET and 8 UI tests also passed. No playback-versus-pause
  visual response or Arena output FPS was measured in this session.
- In a later supervised garage preview, the operator selected Maono MIC In
  1/2 and observed Mirror Cathedral reacting to music in Resolume. After an
  orderly switch from a bounded preview to an unbounded `--no-persist` Engine,
  OSC status sampled 59.9–60.1 Engine fps at tier 0 with music RMS ranging
  0.0055–0.2034 and Spout ready. Arena visibly displayed the scene, and an
  independent receiver counted 600 new fixed-1080p frames in ten seconds.
  Music-pause comparison and Arena's own output fps were not measured.
- On the separate `codex/recursive-visuals` source branch, 18 scenes compiled
  across all four tiers with warnings as errors. Seven Release native tests
  and an isolated RTX EffectProbe passed. At fixed time, quiet-to-music-range
  fixtures changed 139/576 sampled pixels for Neon Orbs, 134/576 for
  Shardwell and 458/576 for Recursive Circuit; all four new scene controls
  and each of bass, mids, highs and hit changed GPU pixels. These are
  synthetic checks, not an Arena or live-mic acceptance of the new build.
- The `20261005-224222` alpha was then opened beside the existing Resolume
  composition. A receiver saw 601 fresh 1920x1080 BGRA8 frames in ten seconds;
  Engine status was about 60 fps at tier 0 with nonzero audio RMS. The active
  NestDrop clip on the higher Resolume layer was stopped (not deleted) to
  isolate Recursive Circuit. The operator's live visual verdict was improved
  complexity, but insufficient musical motion, fluidity and color/shape
  switching. This is feedback, not show acceptance.
- The response pass keeps `SignalFrameV1` raw meter values intact
  while deriving bounded visual energy, a 300 ms onset tail, integrated flow
  and eased multi-phrase palette/topology transitions for Recursive Circuit.
  Its isolated CPU/GPU fixtures passed before the exact package was built and
  auditioned in the Resolume/Maono path.
- The hash-verified `20261005-233633` package passed exact-package IPC smoke
  and replaced only the verified older PrismForge Engine/Control after the
  operator approved a brief restart. The unbounded `--no-persist` Engine and
  matching Control remain live; the existing Arena composition was not saved.
  The higher NestDrop clip was stopped, not removed, exposing Recursive
  Circuit on Deck A. An independent receiver counted 597 fresh fixed-1080p
  BGRA8 frames in ten seconds; Engine status stayed near 60 fps at tier 0.
  Maono MIC In 1/2 was connected and receiving. The operator judged the new
  motion a real improvement but still far below NestDrop's evolving looks.
  Live RMS later spanned 0.02195–0.84279 during a music/interaction sample;
  gain and clipping quality need checking. Windows memory use was 92.7–94.2%
  during this preview, a rig headroom concern. Controlled music-versus-pause,
  Arena output FPS, flash-risk and full mixed-load soak remain unverified.
- A further **source-only, default-off** Flow Echo candidate reuses the
  existing per-deck Feedback history for Recursive Circuit. When that deck's
  Feedback is enabled, its Flow control and the bounded reactive energy,
  onset and flow clock gently advect the prior image; Feedback at zero and
  every other scene keep the previous effect path. It allocates no additional
  GPU textures or show-schema fields. The offline Release Engine built and
  the isolated RTX EffectProbe passed, including matched feedback timelines,
  exact non-Recursive output invariance, quality resize/scene switching,
  bounded luminance, blackout and panic-dim. This is **not** in the running
  `233633` package and has not been auditioned with Maono in Arena. A separate
  `20261006-001115` portable candidate contains it; all 50 packaged SHA-256
  entries match, but exact-package IPC, Control survival and Arena reception
  have not been retested for that folder.

## Not yet accepted for live use

- Arena's own output FPS measurement and end-to-end 1080p60 proof.
- One-hour two-deck, overlay, audio and controller soak.
- Repeated Arena reconnect stability after the observed Spout interop warning,
  plus Arena's own output-FPS measurement for the new portal scenes.
- Playback-versus-pause visual response in Arena, specific FL Studio/room-PA
  routing, gain/beat quality and end-to-end latency; the two live-input meter
  passes above do not establish these.
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
