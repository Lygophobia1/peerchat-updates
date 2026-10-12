# Transfer worker performance correction — dev.121

## Acceptance boundary

The user physically accepted Android background/locked transfers in dev.119,
including an 18,919,915-byte video. That acceptance is preserved. The phone is
unavailable through USB/ADB, not disconnected from Wi-Fi. No additional physical
test, network-disconnection test or deployment is requested in this pass.
Physical throughput improvement is **pending**, not inferred from host results.

## Route evidence and limits

Read-only Windows netstat showed dev.119 PID 16572 connected from 192.168.1.17
to 173.225.14.37:7443 on ports 57988, 58180, 58181 and 58182. Pi ss showed the
same ports arriving through the public address at the isolated server on
192.168.1.6:7443. This establishes NAT-loopback server connectivity despite the
devices sharing the NETGEAR LAN. The normal file route is an opaque server relay
carrying authenticated end-to-end peer TLS, not an automatic direct LAN route.
No active physical payload was captured, so these sockets alone do not establish
the precise cost of the last video or Android-side packet timings.

The running Pi binary was /opt/peerchat-test/server/4.0.2-dev.46/PeerChat.Server
(PID 23441; --version also reports dev.46). Dev.119 server TCP_NODELAY changes
are therefore not deployed there. It was not restarted or modified. No endpoint,
certificate pin, client default, TURN configuration or protocol was changed.
Matched Pi flows had minimum RTT approximately 1.2–1.56 ms. Linux ato:40 is not
proof that Nagle/delayed ACK caused the observed slow physical transfer.

## Proven host bottleneck

Windows Node's relay-only worker slept 20 ms after every pass. Source preparation
hashes one bounded 32 KiB block per pass, and the unchanged transfer protocol
waits for each chunk's durable ACK. Sleep and OS timer granularity therefore
delayed both preparation and payload/ACK servicing, independently of Wi-Fi.

The corrected existing worker wakes on queued commands/relay input and retains
its 20 ms idle/timer deadline. Preparing/recovering work runs another bounded
pass without sleeping, servicing normal commands and timers between blocks.
Direct-socket polling also avoids sleeping during preparation. Failed storage
still uses the existing 250 ms retry deadline; it cannot induce a busy loop.
There is no extra production thread, transfer owner, retry owner or window.

Unchanged: 32 KiB chunks, one outstanding chunk, source digest verification,
protected durable journals, receiver flush-before-ACK, atomic completion, native
SessionTransportManager/DeliveryManager, cancellation and restart ownership.
Android's accepted bounded dataSync service, leases, thermal handling and
notification lifetime are untouched.

## Reproducible measurements

Run tests/test-transfer-session.ps1 -Benchmark. It uses isolated profiles,
Windows CNG/Schannel mutual peer TLS, protected journals, real source/destination
files, blocking relay writer adapters and exact final-byte comparison. Both
sender and receiver must reach Complete. It is not a physical Pi/Android test.
UI snapshot polling is observational and does not pace the relay. Earlier
single-threaded pump results are excluded from the comparison below.

Reference: committed dev.119 Node plus the same read-only diagnostic operation.
Optimized: command wake and bounded preparation scheduling. Independent host
runs (including concurrent build load) yielded:

| Payload | Reference total | Optimized total |
| --- | --- | --- |
| 1 MiB | 2.93–3.76 s | 0.8–1.1 s |
| 3 MiB | 7.43–7.94 s | 1.6–2.1 s |
| 10 MiB | 22.43–34.67 s | 3.5–6.1 s |

For 10 MiB preparation alone: about 9.9–13 s reference versus about 56–73 ms
optimized. Hashing was tens of milliseconds; receiver writes tens of
milliseconds and flushes about 0.1–0.3 s. Protected journal costs remain
meaningful (several cumulative seconds across all cases) but were not removed
or batched past their durability boundaries. Variable host load prevents treating
these timings as a fixed product throughput guarantee.

A standalone standard-library TLS loopback framing benchmark compared split
header/payload writes, TCP_NODELAY and a joined write. Mean round trips were
0.12–0.23 ms; it did not reproduce a 40 ms Nagle stall. Accordingly there is no
production framing rewrite or unproven TCP performance claim.

The subsequent **actual Pi** loopback benchmark did reproduce a Linux-specific
stall. Plain TCP split-write ACKs averaged 47.527 ms with Nagle versus 0.401 ms
with TCP_NODELAY and 0.341 ms joined. The reproducible
tests/performance/PiTlsFramingBenchmark.py then used TLS 1.2 with a newly generated
RAM-only test key/certificate (never the server's key): split TLS writes averaged
47.684 ms with Nagle, 2.545 ms with TCP_NODELAY and 0.991 ms joined. Twenty samples
per mode; bounded loopback-only sockets, installed Python/OpenSSL, no persisted
keys, service changes or deployment. This establishes a second measurable
component bottleneck relevant to the old Pi relay's split framing. It does not
measure the physical video or prove every live record stalled. The existing
dev.119+ relay TCP_NODELAY correction is retained; no new framing rewrite is made.
Deploying the updated Pi server is outside this pass's authorization.

Metadata-only native diagnostic operation 11 reports payload/ACK progress,
route/first-byte timestamps, hash/read/write/flush/commit/journal costs and
backpressure. It contains no credentials, keys or file/message contents.
The host harness occasionally emits the existing contained protected-storage
retry warning; all final files and ACKs complete. It is retained as evidence,
not suppressed or reclassified as a physical device failure.

## Validation

Final-source gates passed:

- Native TransferManager: 1/3/10/32 MiB, exact final content, corruption rejection,
  atomic no-overwrite, cancellation, protected restart/recovery and retained
  journal failure behavior. Added preparation/idle/cancel and persistence-retry
  scheduling checks.
- Real paired Windows native workers: mutual peer TLS, opaque relay, Unicode
  binary, profile reopen/reconnect/resume, final hash, and message/ACK coexistence.
- Protected portable Storage, CNG/DPAPI compatibility, 1,000 opaque signing
  cycles, Profile reopen, history/outbox/ACK/dedupe and registry tests.
- 1,000 shared Hello/identity/Accepted lifecycles and network/presence
  generation/lease/restart regressions.
- Android 135 JVM/Compose tests, zero failures/errors; debug/test/release and
  lint. ARM64/JNI/runtime dependency and release destructive-hook audits.
- Windows full build, restricted packaged runtime load, import/containment
  audits, packaged transfer UI and exact Windows/Android version consistency.
- Local Development updater dev.119 to dev.121: manifest/hash/extraction/exact
  runtime checks. Unsigned Development policy; no release signature claimed.
- Existing group enrollment/recovery security gates passed with Windows and
  portable crypto/server databases; production enrollment remains disabled.
- Committed-reference and final Node/Schannel characterization: six concurrent
  peers, 15 mutual-TLS links, 30 messages/ACKs, trust and encrypted-history restart.
  Final server-first relay/P2P standby/promotion/fallback, asymmetric relay EOF,
  media recovery signaling, ACK/dedupe and identity/reconnect safeguards passed.
- git diff --check passed. Excluded three pre-existing untracked ScreenMedia
  object files; generated build/APK/benchmark SDK outputs are not source commits.

Artifacts under build/development/4.0.2-dev.121:

- DevelopmentUpdaterValidated/PeerChat-4.0.2-dev.121.zip SHA-256:
  23b0b3d293c535f889422dc360641e8d0cf7cc192f00b288386d9acb51e895b5
- Android/PeerChat-4.0.2-dev.121-debug.apk SHA-256:
  47f73860135139bc31f9b054a94255e61544d778905196bf69a06565987a259f
- Android/PeerChat-4.0.2-dev.121-release-unsigned.apk SHA-256:
  867207b433fee69f05c8f6b3a638a8de1128e27f0d23c6c486e7599bc3cbfdcb

The build tooling intentionally allocated an immutable dev.120 intermediate
and dev.121 final build; the final source/APKs were aligned to dev.121, rather
than overwriting the intermediate package. Host benchmark output is preserved
under build/tests/transfer-performance-blocking-{baseline,optimized}/ and
build/tests/transfer-session-dev120/benchmark-final*.txt. Baseline timing is
not an end-to-end physical throughput measurement.

Known pre-existing Windows long-message layout timing failure remains tracked;
its assertion is not weakened by this transfer-only correction.
No merge, publication, deployment, identity replacement or history deletion.
