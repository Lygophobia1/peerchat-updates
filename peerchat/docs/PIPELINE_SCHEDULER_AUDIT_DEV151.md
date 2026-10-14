# Unified baseline pipeline audit and bounded audio scheduling correction

Accepted parent: dev.150 `0709b5b`; all dev.148 and emoji work retained.
No scheduler branch was found to integrate. Other dirty worktrees were inspected
read-only and left untouched. See ACTIVE_DEVELOPMENT_BASELINE.md.

## Windows pipeline inventory

| Pipeline | Execution/ownership | Dependencies, bounds and measurements |
|---|---|---|
| Reliable peer TLS | Per-connection receive workers; native relay input wakes Node; managed outer TLS read/write tasks block independently | Message authentication/framing remain fail-closed. Relay writer blocks on native output, not WPF polling. Lifecycle/attempt generations and retained errors already exist. No physical socket RTT inferred. |
| Messages/outbox/ACK | One Node command/service worker, bounded 512 commands, condition-variable wake; authenticated peer receive schedules commands | Ordered durable message/ACK/deduplication/store operations share the Node worker with file/archive/control work, not the audio device worker. Storage latency can delay other Node commands; contention is not automatically media loss. |
| Files/journal | Shared TransferManager on Node service worker; real disk read/hash/protected journal/receive write+flush before ACK | Dev.147/148 journal and event-driven relay work preserved. 32-KiB/one-chunk/256-MiB rules unchanged. Existing ACK, submission, journal, receiver and confirmed-byte timings remain authoritative. |
| Voice capture | WinMM events, four input buffers, one duplex device worker; input gain then two 10-ms APM blocks and 20-ms Opus encode | Capture handler runs outside device mutex but synchronously on the same worker as playout. This is independent of Node's 20-ms wait. Existing capture work/interval/backlog counters apply. |
| Voice receive/playout | Verified-generation RTP callback -> NetEq sources -> AudioMixer, device-paced two 10-ms pulls and three output buffers | No new jitter buffer. Receiver uses try-lock/busy diagnostics. Final mixed/output-gain PCM is the existing AEC render reference. Callback/device generations remain guarded. |
| APM/Opus | Pinned existing WebRTC APM, high-pass/NS/adaptive digital AGC/AEC; fixed 16-kHz mono, Opus 20 ms | Capture/render DSP mutex and runtime calls can contend. AEC delay is an estimate of queued device/capture buffers, not hardware timestamps. OS enhancements are not observed/controlled by this code. |
| DM screen | Event-driven WGC/latest frame -> encoder condition variable -> bounded AU output -> Node service tick -> RTP/ICE/DTLS-SRTP | Node output handoff still has up to its 20-ms timer delay when no command arrives. Existing queue wait/capture/encode metrics show this independent handoff cost. No extra polling/priority/thread was added. |
| Group screen | Shared capture/encode, per-viewer routes, 60-ms managed pump with overlap guard and background tasks | Sequential configuration/signaling and UI scheduling can delay service. Existing three-viewer benchmark shows handoff delays, not proof of physical TURN loss. Unchanged pending verified RTP/IDR/recovery rules. |
| Screen receiver | RTP validation/reassembly -> bounded eight-AU queue -> decoder worker/MFT -> GPU blit/Present | Dev.148 low-latency property, separate arrival/decode/present gaps and costs preserved. Present sync is intentional pacing. GPU/MFT stalls and uneven arrival are distinct; no hardware acceleration claim. |
| WPF | Dispatcher timers/poll and background tasks; read-only diagnostic projections; optional clipboard retry | UI formatting/native snapshot waits can stall UI or group pump, not directly own audio PCM. Thread-pool starvation, priority inversion and physical driver delays are not measured by current retained data. |

Common CPU/GPU/disk contention can couple these otherwise distinct owners. No
single scheduler explains all symptoms. Existing host byte-pump benchmarks bypass
LAN/Pi/outer TLS; their throughput is not promoted to physical performance.

## Proven audio problem and small correction

The duplex worker drained every completed capture buffer before refilling any
completed output buffer. Four slow capture/APM/encode callbacks could delay already
ready playback by four callback costs. The mutex was released, but ownership was
still the same worker: releasing the lock did not make playback concurrent.

`audio.cpp::Impl::servicePlayback` now services ready output before capture and
between capture callbacks. Ring order, one worker, buffer counts, PCM/Opus framing,
render-reference samples, NetEq, gain, authentication and teardown guards are intact.
No busy spin, extra worker, priority escalation or buffer increase. One individual
slow capture callback can still delay playback; this is not a hard-real-time guarantee.

## Controlled before/after

`tests/audio_scheduler_test.cpp` reuses fake WinMM device functions, but executes
the real Audio worker, Windows events, device buffers and callbacks. Four capture
buffers and three output buffers are ready; each injected capture callback costs
25 ms. No microphone, network, recording, WebRTC codec or physical playback occurs.

Four alternating old/new runs:

| Metric | Accepted capture-first code | Interleaved code |
|---|---|---|
| Capture callbacks before first ready playout | 4 in every run | 0 in every run |
| Start to first playout, microseconds | 160605, 160081, 159704, 181536 | 40275, 65416, 33885, 75583 |

Start-to-playout includes fixture/device initialization and OS scheduling. The
deterministic result is the eliminated four-callback dependency, not a claimed
physical audio-latency improvement. The original baseline executable/log is retained
under build/tests/pipeline-audit. All four capture and three playback callbacks
still complete, all handles close, and no stale header is requeued.

## Validation and remaining comparison work

Focused PCM/audio lifecycle/scheduler tests passed, including callback mute, input
generation changes, partial startup failure and exact final-gain AEC reference.
Packaged voice/screen/transfer/emoji and concurrent regression results are recorded
with the final candidate. Idle/text/file/screen code paths are not changed for an
unproven benchmark win. Existing group scheduling/recovery observations remain
limitations, not claims of repair. No Android or Pi update.

Scheduling and voice changes are separate commits. Physical voice/screen/concurrent
load acceptance remains required even when all automated fixtures pass.
