# Local OSC and PrismBurst bridge

PrismForge listens only on IPv4 loopback (`127.0.0.1`), not the LAN. UDP port
`12100` accepts PrismForge control messages. UDP port `12002` accepts the
existing PrismBurst gesture message. PrismForge can optionally send status to
`127.0.0.1:12101`. No authentication is provided, so these ports must remain
local-only; LAN control is a separate, deferred feature with pairing.

OSC values are big-endian as required by OSC. The parser accepts `i` (32-bit
integer), `f` (finite 32-bit float), `s` (string), and `T`/`F` (boolean tags).
Messages and immediate bundles are supported. Timed bundles, other argument
types, packets over 4096 bytes, strings over 256 bytes, or bundles with more
than 16 messages are rejected. Recognized controls reject out-of-range values.
Malformed bundles are discarded as a whole. Callback threads put validated
events into bounded queues; the engine/render thread applies them.

| Port | OSC address | Type tags | Value |
| --- | --- | --- | --- |
| 12100 | `/prismforge/crossfader` | `,f` | 0–1 |
| 12100 | `/prismforge/blackout` | `,i`, `,T`, or `,F` | integer 0/1 or boolean |
| 12100 | `/prismforge/panic-dim` | `,i`, `,T`, or `,F` | integer 0/1 or boolean |
| 12100 | `/prismforge/deck/A/scene` or `/prismforge/deck/B/scene` | `,s` | scene ID from the current manifest |
| 12100 | `/prismforge/deck/A/effect/0` through `/3`, likewise B | `,f` | effect amount 0–1 |
| 12100 | `/prismforge/cue/save` | `,i` | cue index 0–31 |
| 12100 | `/prismforge/cue/recall` | `,i` or `,is` | cue index 0–31, optional `immediate`, `beat`, or `bar` |
| 12100 | `/prismforge/request-snapshot` | `,` | request current pipe snapshot; UDP response is not promised |
| 12002 | `/prism/gesture` | `,sfff` | name, strength, normalized center X and Y |
| 12101 (out) | `/prismforge/status` | `,fifi` | fps, quality tier, RMS, Spout-ready integer 0/1 |

The exact gesture payload was checked against the read-only PrismBurst source:
`/prism/gesture <name> <strength> <centerX01> <centerY01>`, sent to
`127.0.0.1:12002`. Current accepted names are `punch`, `swipe_left`,
`swipe_right`, `lift`, and `drop`. All three numeric values must be finite
floats in 0–1. A queued gesture can feed modulation only while it remains
fresh; a stale `lastGesture` label is not continuing performer motion.

PrismForge also reads `%APPDATA%\PrismBurst\status.json` without modifying it.
Only a small allowlist of health fields is exposed to the UI. `available`
means the file parsed; `fresh` means its timestamp is at most five seconds old.
Neither proves current Spout pixels, correct body tracking, or audio output.
The file read is capped at 128 KiB. If a port is in use, the bridge reports
the conflict and does not steal or reuse the socket. The rest of the engine
can continue with OSC unavailable.
