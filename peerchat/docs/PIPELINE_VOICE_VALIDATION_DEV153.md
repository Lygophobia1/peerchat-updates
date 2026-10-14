# Windows pipeline and local voice comparison validation

Dev.153 continues the physically accepted dev.150 emoji branch. Scheduling
checkpoint `93b31f7` follows accepted `0709b5b`; dev.148 `f444265` remains an
ancestor and its rollback package is unchanged. The pipeline inventory and exact
four-run scheduling comparison are in PIPELINE_SCHEDULER_AUDIT_DEV151.md.

## Proven correction

The WinMM duplex worker previously drained all ready capture callbacks before
refilling ready playback. The correction services playback before and between
capture callbacks, without adding threads, raising priority or enlarging buffers.
With four ready capture buffers and injected 25-ms capture work, four baseline
runs executed four captures before first playout; all four corrected runs executed
zero. Start-to-first-playout was 159704–181536 us before and 33885–75583 us after.
These include fixture initialization and Windows scheduling, not physical sound
latency. One slow individual callback can still block the duplex worker.

## Voice processing findings

Existing capture remains 16-kHz mono, with input volume before APM and Opus. The
existing NS presets select Low, Moderate and High. Adaptive digital AGC retains
its existing target, limiter and preset compression settings. No production DSP
default or saved preference was changed. Input gain can saturate before APM;
OS-level enhancements are outside the current diagnostics boundary.

NetEq retains separate authenticated sources and mixes them for device-paced
playout. AEC still receives final mixed, output-scaled render samples and the
existing estimated device-buffer delay. This estimate is not a hardware timestamp.
The scheduler regression preserves exact render-reference samples and device
generation checks. No additional jitter buffer or independent retry owner was added.

A deterministic 20-second synthetic fan/voiced-signal fixture exercised all nine
existing NS/AGC combinations through the packaged APM. In its final fan-only
segment, raw RMS was 671.014 PCM units; processed RMS was 634.116 with Low,
309.778 with Moderate and 144.723 with High NS. All three AGC presets produced
the same result within each NS setting, with zero full-scale samples. This fixture
demonstrates suppression of that signal, not physical fan removal, speech quality
or an explanation of reported excessive AGC loudness. Those require listening.

## Explicit local microphone comparison

Voice Settings now offers **Test microphone locally**. Opening the dialog does
not open the microphone. Record captures at most eight seconds into two bounded
in-memory streams; Stop recording mutes/closes capture before listening. Raw and
processed playbacks use the same 50-percent output gain. Raw includes input volume
and any device/OS processing. Processing uses the existing selected NS/AGC settings;
AEC is disabled for this comparison because no remote render reference exists.

No PCM is uploaded, written to storage or exposed in diagnostics. Clear, close,
profile change and backend destruction stop/join the worker and zero retained
sample buffers. A native 400-frame bound and UI elapsed-time cutoff stop capture.
Actual recording and playback still require physical device acceptance. Full-scale
sample counts and RMS are measurements, not a perceptual distortion score.

The tool cannot run alongside a DM/group call. It uses the existing Audio/APM
runtime and adds only a Windows presentation ABI and a narrow WPF adapter. It
does not change Android, the server, encrypted messages or profile formats.

## Participant volume limitation

Independent receive sources exist internally, but the pinned published receive
ABI exposes neither source gain nor its complete authenticated control chain.
Correct per-participant control would require an additive source-control ABI,
verified peer-to-source mapping, group bridge/UI/persistence integration, clipping
policy and reconnect/revocation tests. This candidate does not substitute master
gain for participant volume and does not add misleading sliders. That integration
is deferred; no external processing dependency or engine replacement was introduced.

## Automated validation

All fixtures use isolated profiles or fake devices, never existing user history.
Evidence is retained in build/tests/pipeline-audit.

| Validation | Result and boundary |
|---|---|
| Audio scheduler and lifecycle | PASS real worker/events with fake WinMM; four captures/three playouts complete; callback mute, generation retirement, startup failures and handle cleanup |
| Local microphone controller | PASS real Audio/APM with fake devices; explicit capture/stop/raw/processed, exact bypass, 400-frame bound, clear and invalid settings |
| Packaged microphone WPF/ABI | PASS no automatic recording; invalid settings rejected before opening devices; controls, clear, close and profile invalidation |
| Group voice and receive | PASS existing encode-once/fanout, authorization/lifecycle and NetEq tests; no physical listening |
| Packaged emoji | PASS color/favorites, repeated insertion, keyboard/outside dismissal, protected preferences, native mutual TLS DM/reconnect/restart, portable/JVM Unicode |
| Presence and mailbox | PASS server/account-context ordering, composer and 100 active stop/drain/restart cycles; malformed record validation preserved |
| Media and unified diagnostics | PASS DM/group ownership, stale lifetimes, real/preview isolation, bounded graphs, read-only refresh, copy/export and cleanup |
| Concurrent screen and file | PASS actual host peer TLS, WebRTC H.264 UDP/GPU presentation, 10-MiB durable verified transfer across screen stop and local mailbox rejection, fresh relay pairing and confirmed-prefix recovery; no Pi |
| History Sync | PASS paired Node workers, 25 committed imports and three preserved unresolved records; five profile-worker restarts and exact registration/consent/account checks |
| Native package containment | PASS package/system-only dependencies, HPKE runtime/notices, core load and group voice/screen ABI lifetime checks |

The unchanged packaged screen adapter also passed the controlled animated-window
fixture using host UDP, actual WGC/H.264 and GPU presentation. The measured interval
was about eight seconds after warm-up, with a 30-FPS target and a 20-ms host pump:

| Measurement | One viewer | Three viewers |
|---|---|---|
| Captured and encoded FPS | 29.99 | 30.08 |
| Presented FPS | 29.99 | 30.08 each |
| Final decoder queue and oldest age | 0 and 0 ms | 0 and 0 ms each |
| Recent presentation-gap p95 | 62.32 ms | 60.87, 59.97, 60.00 ms |
| Peak host pump duration | 52.57 ms | 166.88 ms |
| Process CPU time in measured interval | 3859.38 ms | 6828.12 ms |

No decoder queue refusals or media failures occurred. The three-viewer sender
reported one output drop. Average FPS does not imply even delivery: roughly
60-ms p95 gaps remain, and the group fixture is not the managed production group
pump or a physical TURN path. This is a preservation check, not a before/after
screen-performance claim. The standalone eight-source NetEq fixture measured
59.13-us average and 262.80-us worst pull time over 30 simulated seconds, with
stable 2891776-byte warmed private memory; it does not model physical packet loss.

The packaged mutual-TLS 10-MiB host byte-pump fixture measured 5.56/5.58 Mbps
with diagnostics closed and 5.41/5.55 Mbps open; accepted-to-complete times were
15.10/15.03 s and 15.49/15.12 s, respectively. Process CPU time was
1843.8/1437.5 ms closed and 1828.1/1515.6 ms open. These are regression observations,
not an optimized physical network comparison. No file/relay scheduling change was
made in this candidate. The old 42–44 Mbps native benchmark still does not describe
LAN/Pi or packaged outer-TLS throughput.

Dev.153 Client and Updater compile successfully. An intermediate dev.152 compile
failed strict indentation warnings in the new helper; it was corrected before
dev.153 and is not a test package. Existing managed compiler warnings remain.

## Measurement boundaries and deferred work

The audit found independent ownership and timer costs, not one common scheduler.
Node's 20-ms media handoff and the managed 60-ms group pump remain potential
contributors, but changing them without equivalent workload evidence could affect
pacing or ownership. They are unchanged. Physical GPU/driver stalls, thread-pool
starvation, priority inversion, full CPU/disk stress and simultaneous real voice,
screen and file quality have not been established by the available fixtures.
No physical throughput, voice quality or screen smoothness acceptance is claimed.

File chunks remain 32 KiB with one outstanding chunk and a 256-MiB limit. Durable
ACKs, encrypted history, authenticated mappings, AEC, saved media choices and all
accepted emoji features remain intact. No Pi deployment, Android package, account
reset, identity/grant change, merge or release publication occurred.

## Physical acceptance checklist

1. Extract dev.153 separately; use the existing profile, without resetting data.
2. Confirm colored emojis, favorites, repeated picker insertion and compact composer.
3. In Voice Settings, open Test microphone locally. Confirm no capture starts until
   Record. Use headphones; compare raw/processed quiet, fan-only and speech-with-fan
   samples at the same settings. Check onset, loudness/pumping and device selection.
4. Close/reopen the comparison: the previous recording must be gone. Leave the tool
   closed during calls. Verify mute, deafen, normal end/restart and AEC during calls.
5. Compare DM and group voice alone, with 30-FPS screen motion, and with a large
   file transfer. Check audible interruptions, presentation gaps, CPU and confirmed
   transfer progress. Stop sharing while the transfer continues; verify integrity.
6. Repeat a call/share without restarting. Capture real diagnostics if quality or
   connection behavior regresses. Physical media acceptance remains pending.
