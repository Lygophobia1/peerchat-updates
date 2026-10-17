# Networking concurrency and latency audit

The server is not globally limited to one request at a time. However, shared
application locks, serialized cache requests and bulk queues can delay otherwise
independent work. Two narrow corrections are implemented: Android releases its
local-send gate during the scheduled cache RPC, and Windows/server control TCP
sockets disable Nagle before TLS. These changes do not establish that the reported
physical 5–10-second message delay or 6.5–6.7-second file ACK is resolved.

The active branch remains `codex/native-unicode-emoji`, based on dev.174 checkpoint
`82e12cf`. Handed-off dev.174 packages are untouched. The Pi remains dev.173, with
no installation, restart, configuration, database, certificate or TURN change.

## Actual networking paths

| Path | Scheduling and serialization | Relevant boundary |
| --- | --- | --- |
| Server listener | Each accepted connection is queued to a ThreadPool worker | Concurrent connections; synchronous workers can still be occupied by slow clients |
| Authenticated control RPC | Sequential requests on each reused TLS connection | Not one global request; each client RPC owner serializes its own requests |
| Encrypted DM mailbox | Authenticated store, fetch, signed ACK and receipt operations | Durable recipient import precedes its signed ACK; server storage is not recipient delivery |
| Peer file transfer | TransferManager v2 through authenticated peer TLS, normally inside opaque Pi TCP/TLS relay | Direct compatibility remains exact-device delivery; accepted 64 KiB/window16 negotiation unchanged |
| Cached DM attachment | Existing TransferManager/cache adapter, 32 KiB chunks/window1, authenticated JSON/base64 RPC, independent recipient progress | Not the same payload protocol or window as peer v2; append ACK is durable ciphertext offset, not recipient plaintext completion |
| Group cache | Existing signed descriptor, per-device HPKE and cache authorization | Unchanged; cache global gates can contend with DM cache operations |
| History Sync | Separate authorized archive relay and peer TLS, shared client/native/storage workers | Separate authorization does not imply complete scheduling isolation |
| Voice and screen share | WebRTC ICE/DTLS-SRTP; TURN UDP where selected | TCP_NODELAY changes do not apply to UDP media |

Healthy RPC connections are already reused, normally up to 60 requests, 100 seconds
age or 10 seconds idle on the clients. The server bounds the channel to 64 requests
and 120 seconds. Opening a new TLS session per file chunk is not the current design.
Peer relay routes have separate lifecycle owners and can retire when demand ends.

## Confirmed defects and corrections

### Android cache wait blocked local send admission

`MessagingBackend` previously held `gate` across `DmAttachmentExecution.service`:
native command extraction, synchronous server exchange and native reply application.
The same gate protects `send`, so a delayed cache response postponed the new text's
local durable-save operation before network delivery even began.

The same scheduled owner now borrows one command under the gate, performs its
network exchange outside it, then re-enters to apply the response only if the exact
profile object, account owner, session object, generation and runtime authorization
remain current. Stale responses cannot apply native state or log out a replacement
login. Native command/receipt validation and durable ACKs remain authoritative.
There is no new worker, queue owner, retry timer, socket or request replay.

The deterministic pre-fix regression could not admit the concurrent local operation
within 250 ms while a cache reply was held. The final full-suite post-fix run admitted
it in 6 ms (22 ms total test-side scheduling) while the reply remained held. A focused
run measured 1 ms admission / 4 ms total. These are modeled coordinator-admission
tests using the production orchestration, not physical Android disk commits or
end-to-end delivery measurements.

Disadvantage/limit: an already in-flight RPC is not preempted. Mailbox, eligibility,
group and archive stages can still hold the coordinator gate, and inline file
selection/import/preflight is unchanged. This is not a claim of complete control
traffic isolation.

Independent review found no blocker in the new DM result guard. It also noted a
pre-existing outer tick-exception handler that reacquires the gate without a
captured-generation check. A replacement-login race there remains a hypothesis
requiring a deterministic backend lifecycle test; it was not changed speculatively.

Checkpoint: `89ad859`.

### Control TCP sockets retained Nagle delays

The server previously enabled TCP_NODELAY only after dispatching a relay attachment.
Ordinary RPC/event/mailbox sockets kept the default, as did the Windows control
client. Both write frame headers and payloads separately. Android ServerChannel
already enabled TCP_NODELAY.

All accepted server sockets and all Windows discovery/control TCP sockets now
enable NoDelay before TLS. Existing certificate/fingerprint checks, framing,
authentication, peer encryption and relay semantics are unchanged.

The existing Pi RAM-only loopback TLS1.2 fixture reproduced the transport effect:

| Mode | Samples | Mean round trip | Maximum |
| --- | --- | --- | --- |
| Split writes, Nagle enabled | 20 | 47.659 ms | 50.101 ms |
| Split writes, TCP_NODELAY | 20 | 2.004 ms | 3.132 ms |
| Joined write, Nagle enabled | 20 | 1.748 ms | 1.921 ms |

This is a private ephemeral loopback test with generated RAM-only keys, not a live
PeerChat request, phone transfer or physical LAN throughput measurement. No framing
rewrite was made. Disadvantage: small control writes may produce more TCP packets.
Neither mode changes UDP, encryption or durable storage requirements.

The established opaque relay already enabled NoDelay before this audit. Therefore
this correction primarily affects control/cache/mailbox exchanges and pre-relay
setup; it is not proof of a fix for long ACKs on an established peer-file route.

Server checkpoint: `5bf97c0`. Windows changes are separately checkpointed below.

## Reproduced server contention retained for a security reviewed correction

Cache append/read/progress are admitted under the same AccountDatabase gate used by
mailbox authentication and friend-relay per-frame authorization. That gate remains
held across cache disk/SQLite work. The cache uses FULL synchronous durability.

An isolated production-admission fixture held one append at its test-only
`before_commit` barrier for 250 ms. A lookup for an unrelated account was blocked
for 259 ms, then both completed with the correct durable offset. This proves the
coupling; it does not measure Pi SD latency or prove a 6.5-second disk stall.

Do not simply release the authorization lock: a revocation or membership change
could race publication, returned bytes or an ACK. The recommended next correction
is a staged operation with an explicit current-generation authorization fence at
commit/return, preserving durable offsets and refusal after revocation. It needs
focused concurrent revocation and crash tests before replacing the current guard.

Other confirmed structural contention points, not assigned a measured physical
latency contribution in this audit:

- `AttachmentCacheServer` holds its cache gate across one whole request, including
  completion verification and periodic cleanup. Other cache clients wait.
- `Notify` and `Broadcast` hold global `eventGate` while waiting on subscriber gates
  and writing TLS. Store/ACK notification happens before its RPC result returns.
  One event write has a one-second timeout, but waiting for a gate is not bounded
  by that socket timeout. Multiple slow subscribers can accumulate delay.
- The portable peer endpoint can queue up to 2 MiB of already-encrypted TLS output;
  transfer admission uses a 1 MiB threshold. Text/ACKs appended afterward cannot
  overtake those encrypted bytes. Queue size and actual drain rate must be captured
  before assigning a delay. TLS records must never be reordered.
- Synchronous pre-authentication workers/initial request reads lack the later RPC
  deadlines. Many stalled clients remain a potential ThreadPool/resource pressure
  source; the bounded three-client fixture is not a stress/capacity proof.

## Cache scheduling ceiling

Android services one scheduled cache command per completed 125-ms fixed-delay pass.
For 32 KiB chunks, the zero-network/zero-processing upload ceiling from that cadence
alone is about 2.10 Mbps; read plus progress needs two passes, about 1.05 Mbps.
Real processing, setup and control operations reduce those ceilings. These are
calculated limits, not measured Wi-Fi speeds or user bandwidth caps. Windows pumps
successful cache commands immediately, so this is a real platform scheduling
difference. Direct v2's 64 KiB/window16 path is unaffected.

Blind batching under the old gate would have increased message blocking. The next
performance step should drain a bounded cache budget outside the application gate,
yield promptly to control work and preserve existing command/ACK ownership. Measure
durable commit cost, read lease updates and progress RPC cost before reducing round
trips or changing any durability policy.

## Timing and correlation

Windows keeps 32 in-memory RPC rows: queue wait, TCP connect, TLS authentication,
open-to-rpc-ready, request/response exchange and total duration, in local monotonic
microseconds. Reused connections have zero new-open/connect/auth cost. Exchange
includes server processing and transport, so it is not network RTT.

Android's existing bounded 64-row connection history now distinguishes DM-cache
operations and links native command IDs to the independently generated actual RPC
request IDs, with monotonic completion and elapsed time. Existing owner timings
separate send wait/local commit and mailbox/archive/native-relay stages.

Server timings retain 64 in-memory worker-queue and RPC processing/response-write
rows. Processing starts after the complete frame was read, not at first-byte arrival.
Slow samples (at least 250 ms) are globally log-throttled to one per second. Fast
rows are available through existing local management STATUS `networkTimings`.
These are diagnostic metadata, never authority or a new persistent history.

No credentials, private keys, message plaintext, filenames or request bodies are
included. Operation names are whitelisted; correlation IDs are validated. Existing
diagnostics copying/exporting exposes the Windows rows without taking its RPC lock.

The server cannot see logical message/chunk IDs inside end-to-end peer TLS. Its
relay lifecycle pair IDs are useful correlation boundaries, not plaintext access.
Per-frame relay forwarding cost and a complete physical same-chunk trace remain
unmeasured. Do not subtract monotonic clocks across machines. Compare local
durations linked by IDs; wall-clock offsets require a separately measured uncertainty
bound before claiming a physical one-way latency.

## Controlled message trace

The enhanced fixture uses a real Windows HPKE sender, actual pinned TLS server
mailbox store/fetch, Android production JNI authenticated decrypt/durable import,
signed receipt, real server ACK and sender receipt retrieval during an active cache
upload. It checks exact logical message ID, receipt signature and duplicate suppression.
Cache transfers still exercise two recipients, both directions, interrupted upload/
download, restart and verified final bytes. Host files/CNG/DPAPI are used, not physical
Android POSIX storage or Keystore.

Two runs measured 101.549 and 107.815 ms from sealing through retrieval of the signed
recipient receipt. The final trace's boundaries were:

| Stage | Host duration |
| --- | --- |
| Seal and fixture setup | 3.404 ms |
| Store RPC including host bridge | 12.179 ms |
| Fetch RPC including host bridge | 24.076 ms |
| JNI decrypt, durable import and receipt, including bridge | 24.717 ms |
| Server ACK RPC including bridge | 14.046 ms |
| Sender receipt lookup including bridge | 29.369 ms |

Matching actual RPC IDs gave server processing of 6.773 ms for store, 1.979 ms for
fetch, 4.542 ms for ACK and 1.096 ms for receipts; response writes were 0.032–0.053 ms.
These values are not physical phone-to-Pi timings. The cache job was active and
interleaved; this fixture does not model every production coordinator queue or a
simultaneous slow append. The separate held-reply regression covers that gate case.

Five independent server lifecycle cycles completed cold authenticated requests in
128.126, 24.075, 30.922, 38.042 and 23.576 ms while idle pre-TLS and partial-frame TLS
clients remained connected. Thus the server is concurrent; this is not a high-load
ThreadPool benchmark.

## The reported long delays

The retained physical reports available here had direct ACK means around 31–36 ms
and maximums up to about 454 ms. A matching 6.5–6.7-second sample with both endpoint
stage counters was not available. ACK totals must be divided by `ack_samples`;
`last_ack_ms` is a monotonic timestamp, not ACK latency. No exact cause for the
reported 6.5-second turnaround is claimed.

The approximately 25-second compatibility wait is a client route-readiness deadline,
not the cost of one TCP/TLS handshake. Server relay pairing is bounded at 15 seconds;
peer authentication and reconnect/backoff have separate bounds. An unreachable,
wrongly selected, stale or unserviced peer may exhaust the deadline. Diagnose the
pair/attempt and owner stage rather than increasing these timeouts.

5–10-second text delays can arise from a synchronous network timeout under a client
gate, control RPC backlog, storage-held authorization gates or bulk data ahead of
control bytes. The fixes and traces make these distinguishable; no single one is
declared the physical explanation without a correlated capture.

## Live Pi observations

Read-only inspection found the manually supervised dev.173 executable at
`/opt/peerchat-test/server/4.0.2-dev.173/PeerChat.Server`, PID 5939, listening on 7443.
It is not managed by a `peerchat-test` systemd unit. At the idle snapshot it had
10 threads, about 104 MB RSS, roughly 3.3 GiB available system memory, 104 GiB
filesystem free and 99–100% sampled CPU idle, with no observed active 7443 transfer.
Those observations exclude a disk-full/RAM-exhaustion condition at inspection,
not pressure during the user's earlier transfer. No physical Android CPU, thermal,
battery or Wi-Fi/mobile measurement was made.

## Recommended order

Measured contribution ranks below concern controlled failure mechanisms, not
estimated fractions of the user's physical 6.5-second delay:

| Priority | Evidence | Remaining boundary |
| --- | --- | --- |
| 1 | Android cache reply prevented admission for over 250 ms; fixed admission 6 ms with reply still held | Physical disk/ACK benefit not yet measured; other synchronous stages remain |
| 2 | Cache commit barrier blocked unrelated authorization for 259 ms | Requires reviewed commit fencing, not simply removing the lock |
| 3 | Split TLS loopback delay 47.659 ms versus 2.004 ms | Corrected TCP setup; no equivalent full-application physical A/B yet |

Cache pacing, notification lock waits and encrypted bulk backlog are confirmed
structural limits, but their contributions to the reported physical delay are
unranked until matching stage/queue samples are captured.

1. Use the new phase traces on the next physical failure, distinguishing local save,
   mailbox acceptance, recipient durable ACK and sender receipt observation.
2. Isolate cache disk work from the server account-authorization gate using a
   reviewed commit fence; keep notifications off the critical durable-result path
   with bounded, coalesced hints and existing polling recovery.
3. Extend the same scoped out-of-gate pattern to measured mailbox/archive waits,
   then replace cache timer pacing with a bounded event-driven work budget. Avoid
   a second session/retry engine or unbounded concurrency.
4. Prioritize text/control before encryption and bound bulk bytes admitted ahead
   of it. If a separate file channel is necessary, negotiate and authenticate it
   explicitly; do not reorder TLS ciphertext or bypass existing route ownership.
5. Measure durable SQLite/filesystem costs on the actual cache volume. An SSD may
   help verified I/O-bound latency, but current idle capacity alone does not justify
   buying hardware. Reuse healthy pinned connections and inspect LAN/public relay
   routing before changing protocol or bandwidth settings.

A wholesale networking rewrite, larger transfer windows, QUIC migration, weakened
flushes or indiscriminately adding threads is not justified by these results.
Async bounded server I/O may be a later scalability improvement if measured worker
queue delays appear under representative load.

## Validation and remaining coverage

Passed: Android 326 unit tests, lint (zero errors, 17 warnings), APK compile;
Windows native/managed/server/admin build and package dependency audit; server
portability/security and six real-TLS relay revocation cases; cache admission/
durability/recovery/revocation tests; actual Windows/server/JNI cache and text/ACK
trace; diagnostics and attachment regressions. Test-only contention barriers do
not ship. Existing profile/history/grant/cache schemas and protocols are unchanged.

Evidence: `build/tests/android-cache-gate-before.log`,
`build/tests/android-cache-gate-final.log`, `build/network-audit-cache-contention.log`,
`build/network-audit-jni-final.log`, `build/server-nodelay-portability-validation.log`,
`build/server-nodelay-relay-security-validation.log`,
`build/network-audit-windows-build.log`, `build/network-audit-final-managed-build.log`,
`build/network-audit-diagnostics.log`, `build/network-audit-attachments.log` and
`build/network-audit-control-phases-final.log`.

Remaining: physical message/file ACK timing and throughput before/after, actual
Android background reconnect/battery behavior, concurrent active History Sync
latency, slow-subscriber and high-load ThreadPool tests, and per-frame relay timing.
The priority guarantee is not yet complete. Validation binaries are isolated under
`build/network-audit-windows` and `build/network-audit-windows-final`; they are not
a replacement dev.174 release. No package was handed off, installed or deployed in
this audit. Server activation requires separate deployment approval.

Checkpoints: `5bf97c0` server accepted TCP and bounded tracing; `89ad859` Android
scoped network wait; `d7e1fa5` Windows control TCP and phase diagnostics; `6484a56`
real mailbox/ACK tracing and server-contention characterization. None changes a
native C/JNI API, encrypted format or network protocol. Matching Windows managed
components must be rebuilt together; the other project/worktree is untouched.
