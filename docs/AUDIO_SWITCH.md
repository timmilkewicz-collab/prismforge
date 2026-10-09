# Audio-source switching (alpha)

Audio capture uses WASAPI through miniaudio. The renderer never calls WASAPI:
`AudioSwitcher::Request` adds a source ID to a four-slot bounded queue and
returns immediately. A worker enumerates the current endpoints, starts a
candidate with its callbacks gated, then gates off the old stream and gates on
the candidate. An in-flight callback barrier waits for old callbacks to finish
before the switch event is published; the render thread then discards queued
old samples. The old device is stopped only after the new one has started.
If enumeration, identity validation, initialization or start fails, the old
stream and selected source remain in place. Eight bounded switch events carry
source lists, success, failure and disconnection to the render thread.

Selectable IDs contain the exact WASAPI endpoint string encoded as UTF-16 hex:
`input:wasapi:<hex>` or `loopback:wasapi:<hex>`. Friendly names and old
`input:N`/`loopback:N` enumeration positions are not accepted. The worker
re-enumerates an explicit ID before opening it and compares the opened
device's actual endpoint ID afterward. `system-default` deliberately follows
Windows' current default playback endpoint when a switch is requested.

The default startup capture also happens asynchronously. Until its success
event arrives, `audio.connected` is false even if the device list is already
available. `audio.receiving` separately reports fresh blocks within 250 ms;
an open but silent/inactive loopback may remain connected without producing
blocks. A disconnect or 250 ms delivery stall clears stale analyzer levels.
When a capture resumes after a device interruption, the worker publishes one
recovery switch event so the analyzer starts from the new stream clock.
`--no-audio` still enumerates sources but never opens a stream
unless an explicit switch is requested. Miniaudio stop/uninit and worker join
are permitted to wait during Engine shutdown, after the render loop exits.

The Release `prismforge_audio_tests` use fake captures only. They check stable
ID encoding, candidate callback gating and drain, failed-switch retention,
ordered handoff, disconnect/recovery event ordering and bounded nonblocking
request acceptance. These tests do not prove
FL Studio loopback, Maono signal quality, hot-unplug recovery, or venue latency;
those require supervised rig validation.
