# PrismForge weekend teaser — operator runbook

**Status: candidate, not show-cleared.** The first automated twelve-scene tour was
rejected as visually uninteresting. This teaser instead uses four original
looks: **Hex Vortex**, **Ferrofluid Reactor**, **Shardwell**, and **Neon Orbs**,
played by hand in PrismForge Control and mixed live in Resolume. The latter two
were art-directed using the operator's Resolume clips as references; the clips
are not included in PrismForge. Do not run the old automated tour. No
standalone backup video was requested. Keep PrismBurst as the known live
fallback; do not change its checkout, shortcuts, settings, or controller routing.
Arena recovered from a transient Spout interop warning during rapid sender
replacement and displayed both new scenes, but the fault is not root-caused.
Do not treat this candidate as on-air ready until the go/no-go checks below
pass on the intended show rig and output.

## Go/no-go before putting it on air

Use only a newly built, separate portable folder after **all** of these pass
for that exact folder, not just for source or a previous alpha:

1. Release build, native CTest, Control frontend/.NET tests, packaged IPC smoke,
   and all **16 scene shaders × 4 quality tiers** compile successfully. Verify
   every packaged file against its `SHA256SUMS.txt`; keep the prior folder
   `dist/PrismForge-alpha-20260922-161109` unchanged.
2. Confirm the running Engine executable path points into the new folder. One
   Engine owns the `PrismForge` sender; launching another folder may simply
   reconnect to the old process. Confirm the independent Spout receiver sees
   changing **1920×1080 BGRA8** frames, and Resolume itself displays all four
   featured scenes without freezes or shader errors. Check **Resolume's output FPS**
   during crossfades and macro sweeps; receiver cadence alone is not that test.
   Do not run an older same-name GPU probe or rapidly replace the sender while
   Arena is connected; the current isolated test probe uses
   `PrismForge.EffectProbe` instead. Do not hot-reload shaders while the
   PrismForge layer is on air: recompiling all scene/tier variants pauses the
   render thread.
3. Rehearse the intended short segment with the actual projector/LED preview:
   the intended scene pair, both decks, Control disconnect/reconnect, macro reset, Panic
   dim, Blackout, and the separate Resolume clip/layer cut must work. Observe
   for rapid luminance changes, discomfort, dropped frames, and overheating.
   If any occurs, keep PrismForge off air.
4. If audio reaction is part of the teaser, test the **actual** FL Studio/
   system-loopback or Maono/interface source with music: verify the Control
   meter and visible scene response, no clipping, source switching/recovery,
   and stable Resolume output. Otherwise run visual-only with `--no-audio` and
   manipulate the macros manually. Do not use Launchpad S for this teaser;
   its physical input/LED and coexistence checks are still pending.

These checks authorize, at most, a short supervised teaser. The one-hour
two-deck/overlay/audio/controller soak, end-to-end latency proof, venue-safe
profile, and GPU flash-risk meter have **not** passed or do not exist. The
controls below are conservative starting points, not a flash-safety guarantee
or medical/regulatory certification. The operator and venue must be able to
cut the output immediately. Engine FPS telemetry measures its render-loop
cadence, not Arena's final output rate; the adaptive governor currently uses
CPU submission time rather than GPU timestamp timing.

## Preflight and play

1. Leave the existing Resolume composition and PrismBurst fallback intact.
   Put `Sources → Spout Servers → PrismForge` in a spare clip/layer and keep
   that layer off air until checked. Do not overwrite the saved composition.
   If the source is absent, do not replace it with window capture and call that
   a Spout pass.
2. In a PowerShell window opened in the **new, hash-verified package**, run
   `.\PrismForge.Engine.exe --no-audio --no-persist` for the visual-only
   rehearsal; keep that window open. Omit `--no-audio` only after the real
   source test above passes. `--no-persist` protects the existing autosave and
   disables portable show saves for this run. Open `PrismForge.Control.exe`
   from the **same** package. Do not add `--launchpad`.
3. Confirm Control says connected, its sender name is `PrismForge`, and Arena
   visibly receives a changing image at 1920×1080. The UI's
   `spout.connected` field is not receiver telemetry; trust Arena's preview
   and the independent receiver. Only one named-pipe client can connect at a
   time: close Control before running an IPC smoke/CLI client, then reopen it.
   If the Performance macros say **Requires matching Engine**, the Control
   has connected to an older Engine: keep PrismForge off air and verify the
   exact Engine path rather than using those disabled controls.
4. In Control, try **Shardwell** on deck A and **Neon Orbs** on deck B for a
   dense-to-sparse crossfade. Keep **Hex Vortex** and **Ferrofluid Reactor** as
   alternate deck choices. Start with the crossfader on A, four Performance
   macros at **0%**, per-deck effects at 0%, and the Resolume clip/layer still
   off air. Bring the Resolume layer up only after the preview is acceptable.
5. Build movement gradually. Suggested *starting* bounds: Motion **0–35%**,
   Warp **0–25%**, Trails **0–20%**, Color **0–25%**; avoid pushing them all
   high together. Motion drives scene speed, Warp bends geometry, Trails adds
   bounded history, and Color intensifies the palette. Crossfade slowly
   between the two scenes. Neon Orbs intentionally leaves much of the frame
   black; if layering it over other Resolume clips, choose and rehearse the
   blend mode in Arena rather than assuming black is transparent. Use **RESET**
   to return all macros to neutral.
   These percentages are rehearsal guidance, not validated safety limits.

## Abort and rollback

- The **Resolume clip/layer cut is the independent final-output stop**. If
  visuals become uncomfortable, unstable, or Control disconnects, take that
  layer off air first. In Control, **Panic dim** reduces intensity; **Blackout**
  cuts PrismForge's output to black. Test both before the audience arrives.
- Do not assume closing Control stops the sender: Engine continues rendering.
  Once PrismForge is off air, stop only the Engine whose executable path was
  verified in the new package (Ctrl+C in its own terminal). Never kill a
  healthy PrismBurst process or another Engine by name alone.
- If the new build fails, keep it off air and return to the existing PrismBurst
  route in Resolume. The preserved prior PrismForge alpha can serve as a
  technical rollback for comparison, **not** as a show-approved replacement;
  its original look was rejected. Do not replace shortcuts, install anything,
  or alter the saved Resolume composition as part of rollback.
