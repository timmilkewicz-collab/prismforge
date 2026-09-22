# PrismForge Launchpad S alpha profile

PrismForge does **not** open the MIDI port on startup. The operator must
explicitly enable MIDI in the engine; `MidiBridge` construction and device
enumeration are read-only. Opt in with `PrismForge.Launcher.exe --launchpad`
or `PrismForge.Engine.exe --launchpad`; a normally started Engine keeps MIDI
closed. If the Launchpad S input/output is busy, opening
fails instead of taking over the live PrismBurst controller. A fresh PrismBurst
status file or visible PrismBurst process is a further conservative refusal
gate. WinMM does not provide a universal cross-application ownership guarantee,
so do not run both apps against the same Launchpad during a show.

The profile binds 50 distinct physical controls exactly once. The remaining
30 are dark. An unsaved cue's recall pad is also dark even though that pad is
reserved for recall; it lights when the cue is actually available.

| Physical control | MIDI | PrismForge action | LED |
| --- | --- | --- | --- |
| Grid row 1, columns 1–8 | Notes 0–7 | Select scenes 1–8 on deck A | Green; bright if selected |
| Grid row 2, columns 1–4 | Notes 16–19 | Select scenes 9–12 on deck A | Green |
| Grid row 3, columns 1–8 | Notes 32–39 | Select scenes 1–8 on deck B | Green |
| Grid row 4, columns 1–4 | Notes 48–51 | Select scenes 9–12 on deck B | Green |
| Grid row 5, columns 1–8 | Notes 64–71 | Recall cues 1–8 immediately | Yellow when saved; dark otherwise |
| Right-edge buttons, top to bottom | Notes 8, 24, …, 120 | Hold 1.2 s, then release to save cues 1–8 | Red guarded action |
| Top buttons, left to right | CC 104–111 | Crossfader positions 0, 1/7, …, 1 | Orange; bright at nearest position |
| Grid row 6, column 1 | Note 80 | Toggle blackout | Red, bright when active |
| Grid row 6, column 2 | Note 81 | Toggle panic dim | Amber, bright when active |

Only channel 1 (MIDI channel zero in wire notation) is accepted by this
profile. Note Off and Note On with velocity zero both release. Duplicate press
events while held are ignored. Save cue requires a complete 1200 ms press and
release; a short tap cannot overwrite a cue. The raw WinMM callback only
enqueues bounded messages; the engine thread interprets them and changes show
state. LED output is diffed on a separate worker so the render loop never
waits on MIDI I/O.

The mapped colors follow the same semantic language used by the user's
existing PrismBurst Launchpad S: green selection, yellow cue action, orange
visual mix, amber toggle, red safety/guarded control, with bright for active
and dim for available. The velocity values were checked read-only against
PrismBurst's `LaunchpadMapping.pde` and the
[official Launchpad S Programmer's Reference Guide](https://fael-downloads-prod.focusrite.com/customer/prod/s3fs-public/novation/downloads/10753/launchpad-s-prm.pdf):
velocity `0Ch` (12) means off, and top LEDs receive CC 104–111. On an explicit
open with feedback enabled, PrismForge selects the documented X-Y note layout
and simple LED buffer mode, then sends all 80 LEDs once; every unmapped pad is
explicitly sent off. This is a separate PrismForge layout, not a modification
to the PrismBurst project.

Source tests verify unique binding keys, guarded holds, scene/cue/crossfader
resolution, LED colors, dark unmapped controls and no device open by
construction. Physical presses, actual LED colors, disconnect/reconnect, and
input/output contention with PrismBurst are **not yet hardware-validated**.
