# Standalone WebRTC audio receive/mix proof

This component is not used by live calls. Accepted VoiceMedia, audio.cpp,
APM settings/artifact, TURN, room/session ownership, and screen sharing are unchanged.
PeerChat.AudioProcessing.dll remains APM-only. PeerChat.AudioReceive.dll is a
separate proof artifact, never loaded by Client/Core. It is staged separately in
`build/development/<revision>/Proofs/AudioReceive`, not the runnable Client/Package.

## Dependency and format

Pinned shared source: `C:/Peerchatdep/deps/webrtc-complete-src/src`, revision
`6cf6bcb585b31414aac02377df2bc8fec770d0e8`.
The existing clang-cl, Visual Studio SDK compatibility and static runtime settings
are reused; only the new adapter target/dependency closure is built.
The inspected compiler revision is `llvmorg-24-init-7747-g62397f8b-31`, with
Windows SDK 10.0.26100.0, x64 release, custom libc++ disabled and exceptions
enabled only for the wrapper boundary so failures cannot unwind into MinGW.

Direct targets are api/neteq:default_neteq_factory,
api/audio_codecs/opus:audio_decoder_opus, api/environment:environment_factory,
api:make_ref_counted, modules/audio_mixer:audio_mixer_impl and system_wrappers.
Transitive components include modules/audio_coding:neteq, webrtc_opus,
webrtc_opus_wrapper, bundled Opus, CNG, common_audio, audio frame utilities,
the mixer fixed-digital limiter, Abseil, rtc_base and environment/logging utilities.
GN's full dependency list includes build-time code generators and header-only
video/RTP metadata definitions. These are not video capture/codec, PeerConnection,
ICE or DTLS implementations. The exact closure is retained beside the DLL.
The environment adds existing task-queue/event-log support; this is not a new
PeerChat scheduler. The proof adapter itself starts no background thread.

The constrained Opus factory selects 16 kHz mono PCM but registers the RFC Opus
SDP format with 48 kHz RTP clock and two SDP channels (stereo=0). NetEq performs
timestamp scaling. A test checks that two 160-sample pulls advance the RTP
playout timestamp by 960 ticks, not 320. Encoded input remains 20 ms Opus.

Each source owns NetEq/decoder state. AudioMixerImpl consumes 10 ms source frames
and uses its default limiter. No custom jitter buffer or summing mixer is added.
With no sources, the wrapper emits fixed 16 kHz silence instead of the upstream
empty mixer's default 48 kHz output. AEC remains absent; eventual actual mixed
render output could provide its reference, but delay alignment is not implemented.

## ABI and lifetime contract

Version 1 exports pc_rx_create/destroy, pc_rx_source_add/remove/reset,
pc_rx_insert, pc_rx_pull_10ms, pc_rx_get_source_stats/get_mixer_stats.
Integer opaque tokens are validated against live registries, never dereferenced
as user pointers. Source tokens are never reused and cannot cross engine contexts.
The registry is lifetime bookkeeping, not global audio state. At most 64 engines,
32 sources/engine, 200 packets/source, 4096 encoded bytes/packet, and 500 ms
configured maximum NetEq delay are permitted. Proof defaults are 100 packets and
200 ms maximum delay. Configurations/packets/stats require exact size and version.
Caller owns all buffers. Valid accessible pointer storage is a caller obligation;
the adapter cannot safely validate arbitrary invalid memory addresses.

Source mapping is supplied by the future authorized endpoint owner. SSRC is not an
account identity. Identical SSRCs on independent endpoint sources are supported.
Every packet must match that source's SSRC, payload type and endpoint generation.
Reset requires a strictly newer generation and replaces NetEq/decoder state;
other sources remain intact. Removed handles and stale-generation packets fail.

NetEq is thread-compatible, not independently thread-safe. One engine mutex
serializes all WebRTC operations, including mixer source callbacks. Normal calls
use try-lock and return PC_RX_BUSY rather than waiting behind audio processing.
Caller must handle BUSY explicitly without unlimited spinning/retry. No output
is valid unless the call succeeds. Creation/reset/removal/destruction are control
operations, not real-time callback work. Destruction retires the registry token,
waits for the current operation, then releases resources; shared ownership prevents
concurrent calls from observing freed state. No WebRTC callbacks escape the DLL.
Exceptions from wrapper operations are caught and become PC_RX_FAILED. This does
not promise recovery from access violations or upstream fatal assertions/OOM.

For this proof, the caller supplies one monotonic engine clock in milliseconds.
Insert arrival times and pull times must be submitted in chronological order.
Successful pulls are exactly 10 ms apart; first pull sets the origin. This API is
intentionally deterministic, not yet a device clock adapter. Clock values are
bounded to 1,000 days from origin. Never create an independent production receive
timer just to satisfy this interface; device pacing is the future integration task.

## Diagnostics and tests

Counters come from actual insert/pull calls and NetEq lifetime/operation statistics.
Current/target delay, concealed samples/events, FEC received/discarded, packet
discard, acceleration/deceleration, buffer flushes, playout timestamps, source
count and mixed-block count are exposed. No fabricated true-loss/reorder/clipping
counts: this revision does not expose those through the chosen interfaces.
Secondary decoded Q14 rate is sampled using NetworkStatistics every 100 ms;
observer reads return that last interval rate and do not reset NetEq counters.
CurrentNetworkStatistics alone does not fill that rate at the pinned revision.
No PCM, keys or credentials are logged. Limiter enabled is verified from the
upstream factory configuration; there is no exported internal limiter-hit counter.

The MinGW harness dynamically loads all nine C exports with restricted DLL search.
It tests guards, fixed output buffers, 100 lifecycle cycles, 30 concurrent teardown
cycles, independent same-SSRC endpoints, reset isolation, source limits, in-order
determinism, reordering, loss/burst loss, late/duplicate packets, sequence/timestamp
wrap, discontinuity, varying/increasing/decreasing delay, actual FEC decode with
a verified LBRR-bearing fixture, PLC, and 1/2/3/4/8-source mixing.
Each mix run exercises 30 simulated seconds with a stalled source while other
sources continue, then reset/removal independence. Measurements cover average/worst
pull wall time, total process CPU time, and warm/end process private memory.
Synthetic acceleration is not a wall-clock device soak or an allocation-free proof.
Source/mixer/NetEq allocate internally; source limits and packet-buffer limits
bound capacity, but hard real-time guarantees require later platform profiling.
Across repeated local runs, warm-to-end private-memory growth was zero for most
cases and 32 KiB in one one-source run; this is not evidence of unbounded growth.

## Commands

Run from the authoritative worktree under normal Windows user:

```powershell
.\scripts\build-audio-receive.ps1 -ToolRoot '<validated GN/depot_tools root>'
.\tests\test-audio-receive.ps1
$env:PEERCHAT_DEPENDENCY_ROOT='C:\Peerchatdep\deps'
.\build.ps1 -All
.\scripts\package-audio-receive-proof.ps1 -DevelopmentDirectory '.\build\development\<new revision>'
```

The build script reuses the proven ignored presets GN output/cache and records
input hashes, DLL hash, args hash and exact dependency closure. It registers only
the isolated new target in the ignored WebRTC root BUILD.gn; processing algorithms
are not patched. ToolRoot defaults to the existing original worktree's validated
GN/depot_tools tools (read-only); pass an explicit relocated tool root if needed.
No downloads or accepted APM rebuild occur. Packaging verifies source/DLL hashes,
runtime imports and absence of calling/transport/video implementation targets,
reruns the restricted-PATH proof, and refuses an existing proof destination.

Next step remains review, not live 1:1/group integration.

## Development evidence (dev.49 proof)

Pinned adapter DLL SHA-256:
`B5B18809371E6144AE07C7DF716E1DE875DA3CE4874A052E99E0E781151647A8`.
Size 754,176 bytes. Import audit: KERNEL32.dll, USER32.dll, WINMM.dll only;
all nine C exports present. Restricted-PATH MinGW consumer passes.
The accepted APM artifact still hashes to
`C324896121259FBCE10C6B8B15995606378CF281EEDD7515F0B4680181A75A08`.

One development run, with 3,000 pulls / 30 simulated seconds per source count:

| Sources | Average pull (us) | Worst pull (us) | Process CPU (ms) | Private bytes warm/end |
|---|---:|---:|---:|---:|
| 1 | 8.76 | 45.40 | 31.25 | 1,835,008 / 1,835,008 |
| 2 | 17.52 | 91.40 | 62.50 | 1,904,640 / 1,904,640 |
| 3 | 24.42 | 136.70 | 78.12 | 2,154,496 / 2,154,496 |
| 4 | 32.36 | 141.60 | 109.38 | 2,400,256 / 2,400,256 |
| 8 | 60.58 | 371.60 | 203.12 | 2,883,584 / 2,883,584 |

CPU figures cover insertion/mixing/statistics loop work in this single-test process,
excluding pre-encoded fixture construction. Timing includes wrapper/mixer/NetEq;
the figures are local synthetic evidence, not guarantees or a production benchmark.
Observed NetEq target delays were 20–160 ms across network scenarios, below the
configured 200 ms limit. FEC fixture produced a nonzero secondary decoded Q14
rate of 3276 after a verified single-packet loss. Burst loss without FEC increased
concealed samples; duplicate delivery produced the same output hash as clean input.
