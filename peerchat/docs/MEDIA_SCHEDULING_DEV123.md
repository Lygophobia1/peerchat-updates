# Voice / screen scheduling audit and correction — dev.123

Group recovery was first checkpointed at f14f3ae (dev.122), enrollment OFF.
Dev.121 file-worker correction and dev.119 physically accepted Android locked
transfers are unchanged. No phone access, service deployment, merge or publish.

## Proven correction

WGC callbacks arrive on this host around 60 Hz. The old capture limiter compares
each callback against the last accepted callback and then resets its clock to
that callback. Slightly early/jittered callbacks repeatedly cost an extra display
refresh. This reproduced 23.6–24.0 captured/encoded/presented FPS at target 30;
changing the pump interval from 20 to 1 ms did not restore the missing frames.
In a deterministic 600-callback jitter trace it accepts 201 rather than 300.

CaptureCadence preserves the target deadline phase, admits only one frame per
callback, and resets after a stall without catching up in bursts. It preserves
the configured rate cap, latest-pending slot, both existing backend selections,
one encoder/group, bounded output and decoder queues, reference/IDR recovery,
encryption and timestamps. There is no timer priority, buffer/thread enlargement,
new jitter buffer or change to WebRTC pacing/congestion control.

The identical real WGC/software-H.264 -> host UDP ICE/DTLS-SRTP -> MFT/DXGI
benchmark now measures approximately 30 captured/encoded/presented FPS with
one or three receivers. These are local Compatibility tests, not physical TURN
or multi-PC acceptance. The selected pair was host UDP on 192.168.56.1, not Pi
TCP, not inferred from a configured server address. No native crash investigation.

## Actual paths / remaining costs

Voice: WinMM four 20-ms input/three 20-ms output buffers; existing Windows events
wake one audio worker; capture APM runs two 10-ms blocks then Opus once/20 ms.
RTP goes directly to libdatachannel ICE/DTLS-SRTP, production relay-only TURN/UDP.
The verified-generation RTP callback feeds the existing NetEq/AudioMixer; device
playout pulls paired 10-ms blocks and provides the actual mixed AEC reference.
No fixed Node sleep lies in PCM/Opus/NetEq payload delivery. Capture/APM/send and
playout share a worker: long capture work can still delay playout. This physical
distortion has NOT been reproduced or claimed fixed. No audio scheduling change.

Screen: event-driven WGC -> latest slot -> condition-variable encoder -> four-AU
output -> existing Node poll (1:1) or group service -> H.264 RTP/DTLS-SRTP ->
bounded RTP reorder/reassembly -> eight-AU decoder queue -> condition-variable
MFT worker -> DXGI Present(1,0). Presentation intentionally synchronizes to the
display; it is not an unnecessary polling sleep. Decoder cost includes present.
Both screen capture backends use this WebRTC media transport, not reliable TLS
file transport. TCP relay carries authenticated control/signaling only here.
Pi framing/Nagle can delay that control, but does not explain UDP payload latency.

The Node's remaining bounded 20-ms timer wait services screen output; dev.121
already wakes it on commands/relay input. WGC/encoder output is not a new command.
Group Screen Pump is driven by the existing 60-ms WPF pulse with overlap guard,
bounded signaling and one capture feeding independent viewer routes. Formatting
per-viewer snapshots and sequential setup can contend at startup. No SQLite,
history, journal or synchronous storage in media capture/receive callbacks was
found. Decoder/device/GPU locks, synchronous MFT work and presentation can stall.

New bounded observer stages screen_capture_wait and screen_encoded_wait measure
latest-slot residence and encoded-output residence to pop. Existing ABI structs
and media protocol are unchanged; observers contain no payload/disk logging.
They are local queue measurements, NOT hardware end-to-end encoding latency,
network RTT or clock-synchronized capture-to-display latency.

## Measurements / reproducibility

tests/test-screen-scheduling.ps1 builds a bounded animated synthetic window,
one shared capture/encoder, 1/3 real receivers, 30 FPS target, 12 seconds/run,
four-second warmup/eight-second counter deltas. Pump intervals 1/20/60 ms are
controlled harness factors, not changes to production scheduling. The harness
still dispatches window messages; it does not emulate a blocked WPF dispatcher.
CPU is process CPU milliseconds across all local endpoints, not machine CPU%.
Peak pump and observer metrics include startup; FPS/CPU deltas exclude warmup.

Preserved baseline evidence: build/tests/screen-scheduling-f167ca69c1a94c2998f96169ac43d2e5.
Isolated correction: build/tests/screen-scheduling-257b2f5369b14cacb92ee08b4cfa4779.
Packaged correction: build/tests/screen-scheduling-d43fbdfe419b4d10b4ec237cbbd3dc99.

At 20-ms servicing, one receiver: capture/encode 29.996 FPS, presented 29.996,
capture wait mean 31 us / max 1753 us, encoded wait mean 14.847 ms / max 31.718,
decode+present mean 9.130 ms / max 36.920, queue age mean 46 us / max 7.121 ms;
zero final queue/refusals. CPU 3515.62 ms per eight seconds vs baseline 2968.75;
more real frames cost more CPU, this is not a CPU-reduction claim.
Three receivers: 30.021 capture/encode/present FPS, CPU 7718.75 ms/eight seconds
vs baseline 6656.25; capture wait 44 us mean, encode input 2.184 ms mean;
encoded wait 14.767 ms mean, startup max 134.092 ms. All three final queues empty.
At 60-ms servicing, three corrected viewers still approximately 30 FPS, but
encoded wait mean 29.873 ms, max 125.838 ms, 155 over-budget observations. Decode
queue age peaks 53–60 ms. Faster polling reduces handoff delay, not this capture
rate bug; no speculative extra media owner/worker was introduced.

A concurrent-build 60-ms baseline run showed 11–14 presented FPS, decoder queue
age peaks 0.70–0.90 seconds, three queue refusals per viewer and IDR recovery.
It is retained as host contention evidence, NOT attributed solely to 60-ms
servicing. A sequential unloaded baseline repeated ~23.37 FPS without refusals.
No claim that all physical lag/startup issues are explained by capture pacing.

NetEq controlled simulated 30-second tests: one source mean pull 9.25 us / max
69.8 us; eight sources 66.10 us / max 408.8 us. Warm/end private memory stable
(1,785,856 / 2,777,088 bytes). Reorder, FEC, burst PLC, jitter, timestamp wrap,
delay rise/fall, duplicates/silence and irregular real-clock playout passed.
Injected local Opus/WinMM run: capture service mean 20.003 ms / max 20.411;
playout service mean 20.009 / max 20.842; capture work mean 320 us / max 540.
No claim of physical microphone echo quality or remote capture-to-playout delay.
NetEq target/current delay and concealment counters remain authoritative, not
a fabricated network-loss percentage. Separate Opus decode cost unavailable.

## Validation / preserved candidate

Passed: deterministic jitter/stall/no-burst 15/30/60 cadence; fixed observer ring;
audio worker/buffer/device failure lifecycle; reusable receive engine; Opus/APM
presets, AEC reference/framing, 100 receive generations/concurrent teardown;
group voice encode-once and 30 lifetimes; TURN-only topology/security guards;
Compatibility startup during bidirectional voice; group screen one/two/three
viewer ownership, real two-receiver fanout, removal, 15 route lifetimes;
backend defaults/preference selection; native transfer/journal/restart/hash/
atomic completion/message ACK and 1000 presence epoch regressions.
Full Windows build/package/import/restricted load, Android 135 JVM tests
(zero failures/errors), debug/test/release/lint, ARM64/JNI/dependencies and unified
dev.122 -> dev.123 Development updater extraction/runtime-hash audits passed.
Known earlier long-message layout performance failure is unrelated and not
reclassified green by this focused media gate. No broad stale harness cleanup.

Windows candidate: build/development/4.0.2-dev.123/Client/PeerChat.Desktop.exe.
DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.123.zip SHA-256:
368bec45008ad8fca87571814a35e95fbbf6a31fb0f3545ebac503d81f4dcf3f
Android debug APK: build/development/4.0.2-dev.123/Android/PeerChat-4.0.2-dev.123-debug.apk
SHA-256: 4a2307bd978ee22ccc632fe66a56b83b4bbdaaf5f8aea2a109313a57c7db543b
Unsigned Development only, no publishing/deployment. Physical multi-PC voice
distortion, sustained TURN, group startup/queue stress and actual throughput
acceptance remain pending. Existing dev.119 background-transfer acceptance stands.
