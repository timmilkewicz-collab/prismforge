# PrismForge live teaser — operator runbook

**Status: supervised garage-preview alpha, not show-cleared.** The current
portable package is `dist/PrismForge-alpha-20261005-233633`. Its Engine is
running with `--no-persist` alongside matching Control; Recursive Circuit is
on deck A, Maono MIC In 1/2 is receiving music, and Resolume Arena displays the
`PrismForge` Spout sender in an **unsaved** composition. The NestDrop clip on
the higher Layer 3 was **stopped, not removed** to expose PrismForge; this
does not establish that the NestDrop application is closed. Do not save or
rearrange that composition merely to run this preview.
The later `20261006-001115` Flow Echo package is a separate offline candidate;
this runbook does not authorize substituting it for the live Engine.

The operator reports that the new motion is a real improvement, but that the
look remains far short of NestDrop's fluidity and evolving color and shape.
Treat this as an art-direction checkpoint, not a performance or safety sign-off.
Play by hand in Control and mix in Resolume; do not run the rejected automated
twelve-scene tour. Keep PrismBurst as the known live fallback, with its checkout,
shortcuts, settings and controller routing untouched. No standalone backup
video was requested.

## Evidence for this exact alpha

- The source build compiled **18 original scene shaders × 4 quality tiers**
  with warnings as errors. The portable folder passed **50/50 SHA-256** file
  checks and exact-package IPC smoke. These are build/package checks, not a
  venue acceptance test.
- After the approved brief restart, only the verified older PrismForge
  Engine/Control were replaced. An independent Spout receiver counted **597
  fresh 1920×1080 BGRA8 frames in 10 seconds**; Engine status was near
  **60 fps at tier 0**. Arena visibly displayed Recursive Circuit and Maono
  MIC In 1/2 delivered live audio. Receiver cadence and Engine telemetry do
  **not** measure Arena's final output FPS.
- The current Engine has no time limit but uses `--no-persist`, protecting the
  existing autosave. Closing Control does not stop this Engine or its sender.
  The existing Arena composition remains unsaved.

## Go/no-go before putting it on air

**Do not call this alpha show-cleared on the evidence above.** On the intended
rig and actual output, an operator must still:

1. Confirm the running Engine and Control executable paths both point into
   `PrismForge-alpha-20261005-233633`. One Engine owns the `PrismForge` sender;
   starting a different package may reconnect Control to the old Engine.
   Confirm Arena is showing the intended PrismForge clip, not the stopped
   NestDrop Layer 3 clip or a window capture.
2. With a deliberately controlled music-playing versus music-paused test,
   confirm Maono `audio.receiving`, sensible meter/headroom, audible-source
   identity, and visible Recursive Circuit response/recovery. The prior live
   signal and subjective motion report do not substitute for this comparison.
   Check for clipping before raising visual gain; a later preview RMS sample
   reached 0.84. Recheck if the endpoint or routing changes.
3. Measure **Arena's own output FPS** while performing the intended deck
   crossfades, Recursive Circuit's Branching/Flow/Depth/Charge controls and
   master macros. The 597-frame receiver pass and near-60 Engine fps are
   sender-side evidence only. Rehearse a Resolume clip/layer cut, Control
   reconnect, Panic dim and Blackout before bringing the layer on air.
4. Inspect the actual projector/LED preview for uncomfortable luminance
   changes, overheating, stalls and dropped frames. PrismForge has **no GPU
   flash-risk meter, venue-safe profile or soft limiter**; this checklist is
   not a flash-safety guarantee or medical/regulatory certification. The
   one-hour mixed two-deck/overlay/audio/controller soak and end-to-end
   latency measurement are also still open. If any check fails, keep the
   PrismForge layer off air.

These gates permit, at most, a short supervised teaser if the operator and
venue accept the remaining risk and can cut the output immediately. **Do not
assign the Launchpad S to PrismForge for this preview**: physical input, LED
color, coexistence and reconnect acceptance remain unverified. The adaptive
governor currently uses CPU render-submission time rather than GPU completion
timing. A transient Arena Spout interop warning occurred during an earlier
rapid sender replacement and has not been root-caused; avoid rapid sender
swaps while Arena is connected.

## Preflight and play

1. Keep the current Resolume composition and PrismBurst route intact. The
   `PrismForge` Spout source is already visible in Arena; its clip must remain
   off air until the checks above pass. The stopped NestDrop clip on Layer 3
   remains available and was not deleted. Do not overwrite a saved composition.
   If the Spout source disappears, do not substitute window capture and call
   it a Spout pass.
2. **Do not launch a second Engine just to start playing.** If the current
   Engine/Control are no longer running, verify exact process paths and use
   `.\PrismForge.Engine.exe --no-persist` and
   `.\PrismForge.Control.exe` from the same hash-verified
   `dist/PrismForge-alpha-20261005-233633` folder. Do not add `--no-audio`
   for a music-reactive test or `--launchpad` for this teaser. `--no-persist`
   prevents this session from changing the existing autosave or saving a
   portable show. Avoid recompiling/hot-reloading shaders while its layer is
   on air: compilation pauses the render thread.
3. Confirm Control is connected to the matching Engine, its sender is
   `PrismForge`, Maono MIC In 1/2 is receiving, and Arena visibly receives a
   changing 1920×1080 image. The UI's `spout.connected` field is Engine
   telemetry, not receiver proof. Only one named-pipe client connects at a
   time; close Control before an IPC CLI smoke and reopen it afterward. If
   master controls say **Requires matching Engine**, keep PrismForge off air
   and resolve the version/path mismatch.
4. Keep Recursive Circuit on deck A and start with the crossfader fully on A.
   Its Branching, Flow, Depth and Charge controls are live shape controls;
   make small changes and watch the actual output. A second deck can use
   Mirror Cathedral, Shardwell or Neon Orbs for a rehearsed contrast, but no
   particular B-deck pairing has been accepted for this alpha. Start the
   master macros and per-deck effects at **0%**, then introduce Motion,
   Warp, Trails and Color gradually. Do not push all four high together.
   These are rehearsal starting points, not validated safety limits. Reset
   macros before troubleshooting a surprising look.
5. Bring the Resolume PrismForge layer up only after the projector/LED preview
   and final-output cut are checked. Mix deliberately; an earlier 50/50
   crossfade looked dimmer than a full-deck cut, and black in Neon Orbs is
   not automatically transparent. Rehearse Arena's blend mode if layering
   clips. Keep a hand on the independent Resolume cut throughout.

## Abort and rollback

- The **Resolume clip/layer cut is the independent final-output stop**. If
  output becomes uncomfortable, unstable, or Control disconnects, cut that
  layer first. In Control, **Panic dim** reduces intensity and **Blackout**
  makes PrismForge output black; neither replaces the independent Arena cut.
- Do not assume closing Control stops the sender. Once PrismForge is off air,
  stop only the Engine whose executable path was verified in the current
  package, preferably with Ctrl+C in its own terminal. Never kill a healthy
  PrismBurst process or an Engine by name alone.
- If the new look or runtime fails, leave PrismForge off air and return to
  the existing PrismBurst route in Resolume. Preserve
  `dist/PrismForge-alpha-20261005-224222` as the prior Recursive Circuit
  technical comparison; its first look was rejected as too static. Earlier
  `dist/PrismForge-alpha-20260922-220042` and the historically preserved
  `dist/PrismForge-alpha-20260922-161109` are development/rollback artifacts,
  **not** show-approved alternatives. Do not replace shortcuts, install
  anything, delete the NestDrop clip, or change the saved Arena composition
  during rollback.
