# Real-time media audit and physical candidate — 4.0.2-dev.78

## Outcome / evidence boundary

This is an instrumented physical-test candidate, not a claim that the reported
intermittent garbled audio or screen lag has been reproduced or cured. The
original physical diagnostics did not identify the responsible stage. No
video-call work, transport redesign, permanent peer route, second jitter buffer,
reconnect owner or adaptation loop was introduced.

Two concrete code issues were corrected:

1. Audio capture copied every frame into a legacy deque even when the real
   capture callback consumed it and no queue consumer existed. The callback
   path no longer makes that redundant queue copy; queue-based capture remains
   available when no callback is installed. Before: up to 8 x 640 bytes of
   redundant PCM (5,120 bytes plus deque overhead) per device, repeatedly
   copying/evicting at 50 frames/s. After: zero queued frames in callback mode,
   confirmed during active capture by the fake-device lifecycle test.
2. Failure to open an explicitly selected speaker tried the default and then
   other speakers silently. Explicit selections now try only that device and
   return the existing detailed WinMM failure. Default-selection policy remains
   unchanged. A simulated unavailable selected speaker fails and leaves no
   input/output handles. This fixes silent substitution, not a proven cause
   of physical voice corruption.

## Voice findings and timing budget

- PCM remains 16 kHz mono, 320 samples / 20 ms; APM remains two 10 ms calls;
  Opus/RTP remains a 48 kHz timestamp clock with 960 ticks per packet.
- NetEq/WebRTC Opus/AudioMixer owns receive timing, adaptive delay, FEC/PLC and
  mixing. No competing receive queue was added. Source identity, generation and
  SSRC checks still precede insertion. Counters reset on a new receive lifetime.
- WinMM has four 20 ms capture buffers and three 20 ms playback buffers.
  Capture/APM/send and device-paced playout run on the device worker, so long
  capture work can delay playback servicing. This is a hypothesis to measure,
  not grounds for a second audio owner or speculative thread rewrite.
- Capture callbacks execute outside the Audio device mutex. Receive insertion
  and playout use existing try-lock failure/silence semantics; previously hidden
  insertion contention drops are now visible.
- Existing AEC reference is actual decoded/mixed, gain-adjusted remote speaker
  PCM. It is unchanged. Render-reference processing is timed separately.
- Native media callbacks do not acquire UI/SQLite/history/thumbnail locks.
  Diagnostic formatting is on snapshot requests, not file logging in callbacks.
  DSP, WinMM/device operations and library send/receive can still stall; peaks
  now expose those boundaries. No global scheduler priority rewrite is justified
  by the current evidence.
- Desktop stores device index plus name and checks both on enumeration, reverting
  missing stored entries to the documented Windows default. WinMM still lacks
  a stable endpoint identity here; an enumeration-to-open removal remains
  possible. Native input failure preserves explicitly reported receive-only
  playback and cleans partial input state; no new silent input fallback was added.

## Screen queue / ownership audit

- Capture pending slot: one latest uncoded frame, replacing stale work.
- Encoded output: four AUs; preferential delta eviction protects bootstrap.
  Sender continuity detects encoded-ordinal skips, waits for bootstrap/IDR and
  requests recovery. It does not treat skipped reference frames as valid.
- Receiver: existing bounded RTP ordering/FU-A assembly; incomplete AUs never
  enter the decoder. Refused decoder AUs enter existing IDR recovery.
- Decoder: eight complete AUs, refusal of new work when full; no unbounded
  render queue. Decode and DXGI presentation run on the existing decoder worker.
  Eight queued frames can still cause visible delay when the worker stalls.
  Oldest queue age and per-frame wait/decode/present peaks are now observable.
  No blind delta dropping, buffer enlargement or dependency-unsafe flush added.
- Group sharing still captures/encodes once and uses independent viewer routes.
  Backend negotiation, Native/Compatibility selection, TURN-only guard and the
  accepted D3D11 multithread protection are unchanged.
- Existing hardware/software MFT name, adapter/driver, low-latency/CBR/keyframe
  control support, HRESULT/stage, rates, SPS/PPS/IDR/recovery and RTP loss/drop
  diagnostics remain available. Hardware variance requires physical evidence.
- Screen texture/sample lifetime and per-frame presentation views were reviewed;
  no pooling/reuse rewrite was made without measurements. No borrowed surface
  lifetime was changed to reduce allocation.

## New diagnostics

`MediaPerformance` is an observer only: fixed metrics plus a 16-event anomaly
ring per observer, with the recent 60-second window shown on request. Callback
event insertion uses try-lock and records skipped-contention events rather than
waiting for a diagnostic reader. No media contents or filesystem writes.

- Capture/playback worker-service intervals, capture work, backlog peak,
  unused-queue drops, all-output-buffers-returned events, device starts/generation.
- APM capture and render cost, Opus encode cost/failures, send acceptance/failure
  counters and library send-buffer bytes. Successful API submission is not
  represented as proof of actual remote delivery.
- Receive/decode/mix pull peaks/deadlines, insertion lock drops, RTP arrival gaps,
  64-packet sequence/reorder/duplicate/late observation, unresolved gap estimate,
  arrival jitter and timestamp discontinuity. NetEq delay/PLC/FEC/discard/error
  statistics remain authoritative. Gap estimates are not network-loss percent.
- Screen capture callback/interval, conversion/ProcessInput and output-collection
  timing; decode-plus-present, presentation and queue-age peaks/deadlines.
- Small readable summaries in existing Voice Details and Screen Media panels;
  expanded raw statistics and Copy Diagnostics retain the detailed snapshots.
- On-demand cumulative process CPU time, working set and managed GC counts;
  compare successive snapshots, not a fabricated instantaneous CPU percentage.

Budgets are diagnostic thresholds, not scheduling changes: voice work 20 ms,
worker interval warning 35 ms, RTP arrival gap 40 ms; screen processing one
target-frame duration, capture gap two durations, decoder queue age 100 ms.

Limits: separate AEC/NS/AGC costs are not exposed by the pinned APM ABI; capture
cost is combined. NetEq pull includes decode/mix, not separate Opus decode time.
Screen input/output spans are boundary costs, not an asynchronous hardware
encode end-to-end measurement. RTT, congestion estimate, GPU/per-thread usage,
and clock-synchronized cross-device frame latency remain unavailable. A WinMM
all-buffers-returned event is possible starvation, not proof of a driver underrun.
Service intervals may be batched; they are not physical capture timestamps.
The richest new readable summaries are currently on one-to-one diagnostics.

## Automated evidence

- Observer: four concurrent writers / 40,000 events, fixed ring, reset,
  sequence wrap/reorder/recovery/duplicate/loss/jitter observation.
- Audio: PCM/clipping/bounded queues; real worker/events behind fake WinMM
  boundary; unavailable speaker, callback mute, partial failures, repeated
  teardown, zero handle leakage and exact mixed far-end AEC reference.
- VoiceReceiveEngine: 60-frame paired playout, authenticated source/generation
  rejection, telemetry/reset, same-SSRC isolation, deafen and 50 lifetimes.
- Pinned APM/presets: persistence, two 10 ms blocks, AEC reference/reset and
  unchanged framing. Actual injected Opus over host ICE/DTLS-SRTP; 100 call
  generations, concurrent RTP/stop, retained stale callbacks, missing-DLL containment.
- NetEq: deterministic reorder, single loss/FEC, burst loss/PLC, late packets,
  timestamp wrap/discontinuity, jitter, rising/falling delay, duplicates and
  silence, plus 30 concurrent teardown lifetimes and real-clock device pacing.
- Host receive measurements for simulated 30 seconds: 1 source mean pull
  8.86 us / worst 77 us; 8 sources mean 60.53 us / worst 289.70 us.
  Warm/private memory unchanged at 1,835,008 / 2,838,528 bytes respectively.
  These are host library measurements, not physical microphone/driver results.
- Group voice: one-member room, incremental members, encode-once fanout,
  mute/deafen, retired callbacks and 30 lifetimes.
- Screen: extracted source/receiver and bootstrap/loss/late/IDR/queue recovery,
  40 decoder lifetimes; Native/Compatibility/mixed real host RTP/MFT rendering;
  backend selection; 1/2/3-viewer fake-source ownership and two real receivers
  (14/8 presented frames), viewer removal and 15 route lifetimes.
- Screen processes ran alongside protected archive/profile/attachment workers;
  voice host stress and concurrent lifetime tests passed. This is bounded host
  contention evidence, not proof of all same-process physical workload mixtures.
- Native transfer: peer TLS/opaque relay, restart/reconnect/resume, final hash
  and message ACK coexistence. Mailbox blind store/fetch/signed ACK, duplicate,
  restart, tamper, quota/TTL/revocation pass. Chat attachment/long-text and the
  dev.77 History Settings regression pass.
- Packaged WPF: readable summary formatting and repeated details open/close,
  no duplicated summaries. The test uses an explicit UI-state fixture, not a call.
- Full Windows build/import audit, unified version/updater/extraction/hash audit,
  Android JVM/debug/test/release/lint and ARM64/JNI audit pass. Android media
  source is untouched; builds keep unified version metadata consistent.
- `git diff --check` passes. Unrelated three pre-existing `.obj` files excluded.

## Candidate artifacts / next physical gate

Windows: `build/development/4.0.2-dev.78/Client/PeerChat.Desktop.exe`

Updater: `build/development/4.0.2-dev.78/DevelopmentUpdater/PeerChat-4.0.2-dev.78.zip`

ZIP SHA-256: `9FD3AF7855A7994B49146D9D56B9E2C8F2339BA19175C0C36F22D9F23D36D65A`

Android: `build/development/4.0.2-dev.78/Android/PeerChat-4.0.2-dev.78-debug.apk`

APK SHA-256: `A87F8D00DB4A93C5890B9E8B36960B822FA9FB49A2142A736E4487AE142D65D0`

Development unsigned policy; not published, no phone/Pi installation this pass.

1. Both Windows endpoints use this matched candidate. Voice 10+ minutes,
   speech/silence and simultaneous talk-over, AEC ON using speakers.
2. End, allow idle route retirement, call again several times without restarting.
3. Test default/device change or unplug/replug if practical; check device errors
   and no unintended speaker substitution. End call before changing preferences.
4. Screen share 10+ minutes, both mostly static and rapidly moving windows.
   Check Native and Compatibility where practical; preserve current TURN policy.
5. Voice + screen, then file transfer/messages during both; include history sync
   during voice and sharing. These physical combinations remain pending.
6. Immediately after a garbled clip/lag spike copy full Connection & Diagnostics
   on both sides, preferably within 60 seconds. Note approximate glitch time,
   call/share generation and which side heard/saw it. Compare timing peaks,
   recent anomalies, RTP/NetEq recovery, queue ages and backend/HRESULT state.

Ten-minute, actual TURN, multi-PC hardware, 3+ participant stress, Intel/AMD/
NVIDIA comparison and physical echo-quality acceptance are NOT claimed by host tests.
