# Networking latency corrections in dev 175

Dev.175 prioritizes local text admission and durable ACKs without changing encryption, authenticated device eligibility, transfer formats, chunk negotiation, durable storage or History Sync authorization. Windows and Android packages are physical test candidates. No Pi installation, service restart, configuration change or phone installation is included.

The server accepts concurrent requests; it is not a single global request processor. Contention exists inside particular client workers and server locks. The earlier audit and its measurement boundaries remain in `NETWORKING_CONCURRENCY_AUDIT_20261017.md`.

## Confirmed corrections

| Defect | Correction and checkpoint | Evidence and boundary |
| --- | --- | --- |
| Android scheduled cache RPC held the local-send gate | Borrow transport outside the gate, with exact profile, session and generation checks; `89ad859` | Delayed-response regression exceeded 250 ms before; post-fix admission 6 ms in the full suite. Coordinator fixture, not physical delivery. |
| Mailbox and periodic archive RPCs also held that gate | Reuse existing scripts with scoped native operations under the gate and network outside it; `ffd6931` | Both old patterns blocked admission beyond 250 ms. Post-fix modeled admission 0 ms while responses remained held. Stale responses and late authentication errors cannot invalidate a replacement login. |
| Durable mailbox store and ACK replies awaited event writers | One bounded, coalescing hint dispatcher after durable work; `022cc29` | Real authenticated TLS test: baseline processing 503.367 ms with durable storage already complete; corrected store 7.174 ms and ACK 5.922 ms while the writer remained held another 250 ms. Original hints then triggered fetch/receipt reconciliation before duplicate requests. Host fixture, not Pi latency. |
| Pending durable file control could wait behind unrelated source reads | Stable control-first partition preserving rotated fairness and save-before-ACK; `eafd49f` | A modeled slow source read preceded the ACK before the change; zero such reads precede it afterward. Failed journal writes still produce no ACK. |
| Bulk encrypted output could accumulate ahead of new text | Apply a 128 KiB bulk admission threshold to Windows Node and shared Android endpoint; `77f137d` | Real Schannel fixture admits two full v2 chunks while its consumer is held, then text and durable ACK complete in three forward 64 KiB drains. TLS ciphertext order remains intact. |
| Ordinary Windows and accepted server control sockets retained Nagle | TCP_NODELAY before TLS; `d7e1fa5`, `5bf97c0` | Earlier private Pi loopback TLS fixture: 47.659 ms versus 2.004 ms mean split-write round trip. This is not established peer-transfer or physical LAN throughput. UDP media is unchanged. |

Bulk backpressure is a local queue limit, not a new wire window or bandwidth cap. The existing 64 KiB by 16 negotiation remains supported. More frequent backpressure can reduce peak throughput on high bandwidth-delay paths; physical comparison remains required.

The dispatcher holds at most 256 pending account entries and coalesces two optional hint types. A busy event writer is retried fairly on the same worker after a bounded lock wait. Queue overflow can defer discovery until existing periodic fetch; hints never become delivery or ACK authority. A repeated account hint may wake an already notified device again, relying on existing durable deduplication. Subscriber writes retain existing timeouts and authorization checks.

## Validation

- Final shared endpoint regression: 102 real Schannel/shared-runtime lifecycles, durable text/ACK/deduplication, binary and image transfers, capability and winner rejection. Host local-append-to-durable-recipient-ACK median 4.078 ms, p95 4.998 ms, mean 4.211 ms for 100 messages, excluding handshake and using in-memory opaque carriage. Baseline median 4.221 ms is comparable noise, not an end-to-end speed claim.
- Actual Android JNI and Java TLS against Windows Schannel: mutual pinned Hello, bidirectional files of 2 MiB plus 123 bytes, 64 KiB chunks and window 16, exact committed content and completion ACKs. Host adapters, not a phone.
- Fresh production JNI/cache integration: both directions, two offline Windows recipients, interrupted upload/download and restart, unpinned-device rejection, verified completed history, direct compatibility fallback, and encrypted mailbox import/deduplication/signed ACK during cache upload. Native local text commit during upload measured 16 ms in this host run.
- Native transfer manager restart, durability failure, pacing, fairness, timing, budget, replay, cache scope, statistics and all negotiated pipeline sizes/windows passed.
- Packaged Windows pipeline and tuning tests passed. At 64 KiB by 16, the same 10 MiB host harness measured dev.174 at 66.55 Mbps and dev.175 at 73.65 Mbps, with exact integrity. These single runs used blocking in-process opaque forwarders and real native TLS/storage, no sockets or Pi, and ran alongside other host work. They show no observed host throughput collapse, not a proven speed improvement or physical network rate. Across the candidate settings, CPU totals were 906–1906 ms and peak working sets roughly 72 MB; these are process totals, not Pi/Android utilization or battery measurements.
- Server portability, full mailbox authorization/restart/tamper/quota checks, and six established-relay revocation cases passed. The final contention regression retains original hints under a temporarily held subscriber lock.
- Concurrent media/transfer lifecycle passed using the rebuilt production Node: authenticated peer TLS offer/answer, H.264 ICE media, GPU presentation, text coexistence, file transfer interruption/recovery, teardown and repeated decline. This is host nonregression, not physical smoothness acceptance. Evidence: `build/dev175-media-transfer.log`.
- Genuine group attachment application regression passed against the isolated dev.175 server candidate: upload once, two recipient downloads, HPKE/private metadata authorization, missing old group key, epoch changes, sender offline, lost responses, client/server restart, verified export/Retry Save, independent signed receipts, revocation/rejoin and disabled-cache rejection. Evidence: `build/dev175-group-application.log`.
- Android focused scope and orchestration suite: 44 tests passed. Final full run: 336 tests across 74 suites, zero failures/errors; lint zero errors and 17 warnings; arm64 debug APK build passed with the existing signing certificate. Full-run modeled local admission was 4 ms during a held mailbox reply and 58 ms during a held archive reply, both before release; these remain fixture admission measurements, not native commit or phone delivery.

Evidence is retained under `build`: `dev175-endpoint-final.log`, `dev175-direct-interop.log`, `dev175-jni-cache.log`, `tests/ack-priority-review`, `mailbox-hint-baseline.log`, `mailbox-hint-final.log`, `mailbox-hint-portability-final.log`, `mailbox-hint-relay-security-final.log`, and `tests/android-scoped-owner-final-175.log`.

## Remaining latency limits and recommendations

1. **Server cache disk operations share the account authorization lock.** A previous controlled 250 ms disk barrier delayed an unrelated lookup 259 ms. This is a confirmed interference mechanism, not the measured origin of the physical 6.5 second ACK. Safely separate disk and global account locking using scoped authorization fences, with every revocation mutator participating and a fixed lock order. Simply unlocking around append/read would weaken authorization and is not implemented.
2. **Android still has one synchronous scheduled owner.** Releasing the local gate permits text saving during mailbox/archive/cache waits, but cannot preempt a request already running on that owner. Bootstrap, eligibility, group and explicit UI network paths retain existing behavior. Next measure their queue contributions; split bounded control scheduling only if correlated traces justify it. Do not create another messaging engine.
3. **Cache cadence differs from direct v2.** Cache upload uses 32 KiB and one RPC per 125 ms tick; download adds progress RPCs. Ideal cadence ceilings are roughly 2.1 Mbps upload and 1.05 Mbps download before processing/network cost. Reusing bounded continuation budgets could help, but changing cadence without fairness and recovery tests would undermine the current priority and is deferred.
4. **The physical 5–10 second text delay, 25 second readiness and 6.5–6.7 second file ACK remain uncorrelated.** The readiness deadline is not a measured TLS duration. Use retained local monotonic queue, connect, TLS, exchange, durable processing and ACK timings with correlated IDs; never subtract clocks across machines as though synchronized. Existing physical ACK traces were commonly 31–36 ms, with a retained maximum 454 ms, not the reported slow event.

No large networking rewrite is recommended on current evidence. First deploy only separately approved server fixes, collect physical correlated traces, then address the measured authority-lock and single-owner contention with narrow checkpoints. Healthy control TLS connections are already reused; reconnecting every chunk is not the current architecture.

## Compatibility and integration

No exported native C/JNI API, message/encryption format, database schema, group format, identity mapping, cache audience rule or Pi configuration changed. The native queue and TransferManager scheduling headers require rebuilding both Windows and Android, which dev.175 does. The original dev.148 checkpoint and accepted packages, including dev.174, remain available.

The other project's clean `ac7ab34` interface plan in `ANDROID_GROUPS_VOICE_INTEGRATION.md` was inspected read-only. Its optional ReliableEndpoint group/voice admissions do not conflict with these transfer scheduling changes; preserve the new bulk-admission check if integrating future endpoint modifications. No other worktree was modified.

Server changes are tested candidates only. The live Pi remains dev.173, so its Nagle and mailbox-hint corrections are not active until a separately authorized deployment. Client improvements remain compatible with that server.

## Physical acceptance

Install the Android APK as a replacement with the existing signing identity; do not uninstall or clear storage. Run the matching Windows package with the same profile. Test bidirectional text while a large direct file transfer and then History Sync are active; compare local saving, Queued, Sent and Delivered timing. Test interrupted transfer resume and final integrity, background/reopen queued delivery, repeated reconnect/restart, and group attachment nonregression. Retain diagnostics from both endpoints around any long ACK. Physical latency, battery impact and smoothness are pending user results.

## Package handoff

Production checkpoint: `32830b8a0ae90a65f8423c9c10318dfcf204a6aa` on
`codex/native-unicode-emoji`. Both packages passed version, source, native
dependency, signing and byte-integrity audits. Existing compiler/deprecation
warnings are retained; there were no build or test failures in the final gates.

Under this worktree's `build/development/4.0.2-dev.175`:

- Windows: `PeerChat-Windows-4.0.2-dev.175.zip`, 12,475,197 bytes,
  SHA256 `f9815293d94e67449df51934a59b554006e1c19bffd2e6daa43e2bd7a4e2bf6d`.
- Android: `Android/PeerChat-Android-4.0.2-dev.175-debug.apk`, 27,348,333 bytes,
  SHA256 `48e7d2f742abe3aa0eb193785194bab9b940878ecc49d78bb647f677cb651b31`.
- Runnable Windows folder: `Package/PeerChat.Desktop.exe`.
- Package audit: `messaging-repair-package-audit.json`; validation log:
  `build/dev175-package-audit.log`.

No installation, deployment, merge or release publication was performed. Stop at
physical acceptance; the separately tested server candidate is not an instruction
to update the Pi.
