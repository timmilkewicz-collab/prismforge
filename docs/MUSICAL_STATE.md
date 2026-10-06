# Musical interpretation contract

`MusicalStateEngine` is the deterministic boundary between PrismForge's raw
audio analysis and scene-level musical behavior. It is a source candidate on
`codex/musical-interpretation`; it is not part of the running
`PrismForge-alpha-20261005-233633` package.

```text
WASAPI / loopback input
        |
SignalAnalyzer -> SignalFrameV1
        |
MusicalStateEngine -> MusicalStateFrameV1
        |
scene-specific interpretation
        |
shader / renderer controls
```

The engine describes the music. It does not contain flow, palette, geometry,
density, shader, or renderer concepts. Recursive Circuit is the first consumer;
the other scenes retain their existing raw-audio path in this slice.

## `MusicalStateFrameV1`

The contract is a standard-layout, trivially-copyable 64-byte value. All
fields are finite. Unless noted otherwise, float values are clamped to
`[0, 1]`.

| Field | Meaning |
| --- | --- |
| `sourceSampleIndex` | Analyzer sample clock used for deterministic time. |
| `eventId` | Stable seeded ID minted only after a dwell-qualified slow-state transition. |
| `immediateEnergy` | Fast bounded energy from sanitized RMS, peak, and spectrum. |
| `onsetEnvelope` | Short envelope raised only by a new hit edge/counter. |
| `accentEnvelope` | Short high/accent envelope; it is not labeled as a snare classifier. |
| `grooveEnergy` | Medium memory of repeated, reasonably spaced hits. |
| `sustainedEnergy` | Slower attack/release energy used for persistence. |
| `energyTrend` | Smoothed rise/fall evidence in `[-1, 1]`. |
| `calm` | Confidence in sustained low-energy space. |
| `building` | Confidence in a sustained positive-energy trajectory. |
| `driving` | Confidence in sustained energy plus groove activity. |
| `peak` | Bounded impact confidence, strengthened by prior buildup. |
| `release` | Decaying confidence after a peak/high-energy section falls. |
| `slowStateAge` | Saturating normalized age of the committed slow state. |

The five slow weights overlap and do not sum to one. A reset represents
"unobserved" as an all-zero frame. Calm is inferred only after advancing
silent or near-silent analyzed frames.

## Determinism and bounded memory

`Advance` derives elapsed time only from the difference between consecutive
`SignalFrameV1.sampleIndex` values and the configured sample rate. It never
uses wall clock time or nondeterministic random state.

- A duplicate sample index returns the previous frame exactly even if the
  duplicate payload or counters disagree. Duplicate identity has precedence;
  a counter rewind is acted on only when a later sample index advances.
- An advancing frame with a rewound event counter, or any sample-index rewind,
  is treated as a source restart. The engine resets and anchors without
  replaying a stale hit/accent flag or generating a phantom peak.
- Large sample jumps are capped by a configured maximum state step.
- NaN, infinity, clipping, and out-of-range raw fields are sanitized before
  they enter state.
- History is a fixed set of scalar envelopes, counters, and bounded timers;
  the hot path performs no allocation and stores no unbounded frame history.
- The configured seed affects only event IDs. It cannot change energy or state
  confidence values.
- Extra 60 fps render publications over a slower analyzer sequence are exact
  duplicate-index holds, so they cannot alter the sequence observed at the
  analyzer timestamps. `MusicalStateEngine` accepts no renderer animation time;
  scene animation clocks exist only in the downstream renderer adapter.

Fast attack/release envelopes preserve transients. Repeated-hit timing builds
groove memory. Short and long energy followers produce trend evidence.
Buildup and peak memories give identical loud frames different meaning after a
rise, during a held loud section, and during a fall. Slow-state event IDs use
continuous confidences plus hysteresis and per-state dwell times. A strong,
buildup-qualified peak receives a bounded priority window over the slower
driving follower so the peak can become an observable event; it is retained
only while meaningful peak confidence remains.

## Extraction boundary

The core has no scene IDs, deck state, D3D, Spout, HLSL, Resolume, Control UI,
or renderer animation-time dependency. Its only project-specific input
dependency is the C++ `PrismForge/SignalAnalyzer.h` definition of
`SignalFrameV1`, plus the assumption that its monotonic `sampleIndex` uses the
configured sample rate. Reuse in PrismBurst therefore needs a small adapter or
a shared analyzed-signal contract; it does not require extracting renderer or
scene code. That integration is future work and this candidate does not modify
PrismBurst.

## Recursive Circuit integration and rollback

With the default source path, a renderer-owned Recursive Circuit adapter maps
the musical frame to scene-specific flow, density, topology, palette, impact,
release, and bounded event variation. The shader's high-level behavior no
longer reads raw bands or the one-frame hit flag in this mode. Flow Echo uses
the same interpreted density, impact, and flow values.

Raw `SignalFrameV1` remains available to the other scenes, metering, cue timing,
and the existing modulation system. Blackout, panic dim, adaptive tiers, fixed
1920x1080 sender output, and shader reload behavior remain downstream and
unchanged.

For an operator rollback through the packaged Launcher, first verify that no
PrismForge Engine is running, change into the candidate package, and run:

```powershell
.\PrismForge.Launcher.exe --legacy-recursive-audio
```

The Launcher forwards the flag only when it starts a new Engine and refuses to
silently accept it for an Engine that is already running. Direct Engine startup
for source/runtime comparison remains available with:

```powershell
.\PrismForge.Engine.exe --legacy-recursive-audio
```

That flag restores Recursive Circuit and Flow Echo's previous raw
`ReactiveMotion` path. It does not alter saved show data. Do not start a
candidate while another PrismForge Engine owns the sender, pipe, or OSC ports.

## Replay fixtures

`prismforge_musical_state_tests` reads complete analyzed feature frames from
`native/tests/fixtures/musical`. The checked-in CSV contract includes the
sample index, RMS, peak, all 32 bands, grouped bands, hit/accent flags and
counters, BPM, beat phase, and beat confidence. No raw audio is stored.

Fixtures cover:

1. silence / near silence;
2. steady low-level groove;
3. repeated transient groove;
4. gradual buildup;
5. buildup to peak/drop;
6. peak to release;
7. sudden isolated transient;
8. sustained loud energy.

Each replay compares two fresh engines field-for-field, repeats after reset,
checks a canonical explicit-field fixed-point FNV hash, and asserts musical
behavior over time. The buildup-only fixture deliberately stays below the peak
transition, the buildup-to-peak and peak-to-release fixtures contain explicit
level transitions, and sustained-loud has a quiet anchor before its sole onset.
Additional fault cases cover malformed input, duplicate-index/counter-rewind
precedence, bounded large jumps, held reconnect flags, counter resets,
sample-clock rewind, alternate seeds, render-cadence duplicates, sample-rate
reset, and long-run bounds. The hash deliberately does not read struct padding
or raw floating-point object bytes.
