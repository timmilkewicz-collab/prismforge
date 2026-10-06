# Bounded live-audio signal check

This is a supervised diagnostic for the *actual* Windows audio route. It does
not save raw audio, change PrismForge's autosave, touch PrismBurst, start a
controller, or prove that a visual is musically responsive. It starts one
test-owned `PrismForge.Engine.exe` with `--no-persist` and a bounded lifetime,
connects to its local pipe, samples aggregate meter data, then stops only
that verified PID/path. Leave the PrismForge layer **off air** in Resolume:
starting this Engine still creates the `PrismForge` Spout sender.

Close PrismForge Control and any PrismForge Engine first. The script refuses
to run when either process already exists, and checks that the pipe belongs
to the Engine it started before issuing a source command. It does not close
Resolume/Arena, PrismBurst, FL Studio, or any audio application. One IPC client
can connect at a time, so Control must remain closed until this check exits.

In PowerShell opened in the **exact package** you intend to rehearse:

```powershell
$engine = (Resolve-Path .\PrismForge.Engine.exe).Path
.\tools\audio-signal-smoke.ps1 -EnginePath $engine -SampleSeconds 10
```

The default source is `system-default`, a loopback of Windows' current default
playback endpoint. Start FL Studio or other playback *through that endpoint*
before measuring. The first run prints all currently enumerated IDs. To test
the Maono/interface input or a specific loopback, copy one exact ID from that
list and rerun:

```powershell
.\tools\audio-signal-smoke.ps1 -EnginePath $engine -SourceId 'input:wasapi:<exact-hex-id>' -SampleSeconds 12 -SettleSeconds 3
```

Alternatively, use the complete exact name from that run's enumeration. The
script requires one unique exact-name match, then uses its enumerated ID; it
never picks a partial or first-listed match:

```powershell
.\tools\audio-signal-smoke.ps1 -EnginePath $engine -SourceName 'Maono MIC In 1/2 (Maono ProStudio 2x2 lite)' -SampleSeconds 12
```

The source switch is asynchronous: the script waits for a snapshot showing
the requested ID **and** `audio.connected`, drains pre-switch frames for the
settling period, then samples. A failed switch, disconnect, lost IPC, stale
sample clock, or a mismatched pipe owner is an error—not a silent pass. The
test will not guess a device by partial name or old positional index. The
Engine stops on its own after a short bound even if cleanup cannot reverify
its path.

Output contains only aggregate RMS/peak, grouped-band maxima, 32 per-band
maxima, hit-count changes, and frame counts. `ACTIVE_SIGNAL` means at least
three distinct audio samples had RMS ≥ 0.01 and peak reached 0.02 in the
sample window. `SILENT_OR_BELOW_THRESHOLD` means that simple meter threshold
was not met; it can mean no playback, wrong routing, low gain, or genuinely
quiet material. `NO_FRESH_AUDIO_SAMPLES` is a failure even if a device says
connected. These labels are not beat-quality, latency, clipping, musicality,
or scene-response certification. No audio waveform is recorded or written.
Silent/below-threshold and no-fresh-sample outcomes exit with an error so they
cannot be mistaken for a passed signal check.

The first supervised check on 2026-09-22 found `system-default` connected but
with no advancing sample index or level while no music was playing. `Maono
MIC In 1/2` delivered fresh blocks but measured only room-idle noise (RMS
about 0.0001, peak below 0.0004). Those are routing/baseline observations,
not a music-signal pass. Repeat with the intended show playback active.

On 2026-10-05, after the operator reported music and mic connected, the
exact-package check for `PrismForge-alpha-20261005-185711` passed the bounded
`ACTIVE_SIGNAL` threshold on both `system-default` loopback and the current
`Maono MIC In 1/2 (3- Maono ProStudio 2x2 lite)` input. The loopback produced
171 distinct sample indices in 10.8 seconds, mean RMS 0.0392 and peak 0.3924;
the Maono input produced 179 in 10.7 seconds, mean RMS 0.0178 and peak 0.1360.
No raw audio was recorded. This is the first real-input meter pass, **not** a
visual, venue, or recording acceptance. Resolve endpoint IDs anew for each
rig/session; the device display name changed since the earlier check.

After a clear signal, the separate visual rehearsal is still required: close
the probe, start that same package's Engine normally *without* `--no-audio`,
open Control, select the verified source, and compare a featured scene's
motion with music playing versus paused while watching Resolume's actual
output. Check both gentle passages and transients, clipping, source recovery,
blackout/panic-dim, and the independent Resolume layer cut. Do not declare
the weekend teaser show-ready from this meter script alone; see
[SHOW_TEASER.md](SHOW_TEASER.md) for the remaining go/no-go checks.
