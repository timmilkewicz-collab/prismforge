# Recursive Circuit musical-state live audition

This is a supervised comparison, not show clearance. The operator's visual
judgment is authoritative. Do not change the Resolume composition, PrismBurst,
Windows audio routing, or Maono configuration during this procedure.

## Before the switch

1. Close PrismForge Control so the read-only telemetry monitor can own the
   alpha's single IPC client.
2. Record the full executable path, command line, PID, and SHA-256 of every
   running `PrismForge.Engine.exe`. Proceed only if the sole Engine is the
   expected known-good package `PrismForge-alpha-20261005-233633`.
3. In Resolume, visually confirm the intended unsaved composition, Recursive
   Circuit clip/layer state, and current output. Do not save or edit it.
4. Verify the new candidate's `SHA256SUMS.txt` before launching anything.
5. Gracefully stop only the verified known-good Engine from its owning console
   with Ctrl+C. If its identity is ambiguous or a graceful stop is unavailable,
   stop the audition rather than force-terminating an unknown process.

## Candidate pass

1. From the exact candidate folder, run:

   ```powershell
   .\PrismForge.Launcher.exe --engine-only
   ```

2. Verify that the running Engine path is the exact candidate, that the
   `PrismForge` Spout sender recovers in Resolume, and that output remains
   1920x1080. Do not replace or reselect Resolume content.
3. Start Control only long enough to select the already-configured current
   Maono input, confirm capture is connected and receiving, then close Control.
   Do not change the Windows or physical route.
4. From the candidate folder, observe the existing Engine without sending
   commands:

   ```powershell
   .\tools\watch-musical-state.ps1 -SampleSeconds 120
   ```

   Confirm RMS is nonzero during music, sample/time advance, Engine FPS is
   plausible, groove and buildup rise when expected, peak/event IDs change
   deliberately, release persists after impact, and calm returns in quiet.
5. Play representative music and judge Recursive Circuit through quiet,
   steady groove, repeated kicks, buildup, drop/peak, sustained loud material,
   and release. Pause playback and watch the state decay; resume and confirm
   that reconnect/resume does not create a bogus peak.

## Legacy comparison

1. Gracefully stop only the verified candidate Engine.
2. From the same candidate folder, run:

   ```powershell
   .\PrismForge.Launcher.exe --engine-only --legacy-recursive-audio
   ```

3. Repeat the same musical excerpts and compare musical participation,
   repetition, twitchiness, persistence, anticipation, impact, release,
   visual fatigue, and whether the new path feels materially closer to
   NestDrop-style musical participation without copying NestDrop.

Do not assume the musical-state path is better because more values move. If
the comparison is inconclusive or worse, return to the hash-verified known-good
package and keep every other scene on its existing raw-signal behavior.
