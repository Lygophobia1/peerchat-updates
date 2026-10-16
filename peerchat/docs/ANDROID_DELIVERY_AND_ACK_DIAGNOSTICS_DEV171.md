# PeerChat Android delivery repair and ACK diagnostics dev171

Dev.171 repairs a confirmed Windows eligibility-snapshot initialization failure and adds the Android timing observations needed to isolate slow transfer ACKs. Windows-to-Android delivery still requires physical retesting. File transfers in both directions are functionally accepted by the user in dev.170; their performance is not accepted. No physical throughput improvement is claimed for dev.171.

## Confirmed mailbox destination and clock mismatch

Read-only inspection found the live Pi process running `/opt/peerchat-test/server/4.0.2-dev.166/PeerChat.Server` with its existing configuration and database. SQLite was opened with `-readonly`. Queries selected only public message/device identifiers, account IDs, envelope/receipt lengths and registration state; no payloads, tokens, private keys or ciphertext were exported.

The failed messages `0b41a75328514d87bc94c756eefb80a6`, `94e64b700b9e41d089bd5cde8265d5d0` and `30dcf11e3c7c4cdd9a80900ca8b2ea62` were stored for recipient account 4, device `35049f3bf26f…`, with pending encrypted envelopes and no receipt. That device is the account's Windows device. The Motorola is `61f70fb01a60…`, also account 4, with a separate active mailbox binding. The phone's empty fetch does not show a failed decrypt of these copies: those envelopes were not addressed to it. Windows diagnostics also show these message target sets frozen as unresolved single-device selections. This is not evidence of routing to the wrong account.

A bracketed clock measurement returned local UTC 1792144841744 through 1792144842389 ms and Pi UTC 1792144879573 ms: approximately 37,506.5 ms ahead of the Windows midpoint, with 645 ms round trip. This measurement was made without changing either clock. Windows' managed snapshot codec allowed at most 30 seconds of future issuance, while native validation correctly rejected any snapshot issued after its current UTC. Android previously deferred future snapshots only up to five seconds. Thus the measured skew prevents immediate Windows snapshot installation and explains the captured unresolved selection state. A new device audience is frozen only when a valid cached snapshot is available at send time.

## Messaging correction and security boundary

`desktop/MailboxExecution.cs` now retains at most 32 exact authenticated responses pending issuance, for skew up to 60 seconds. The application schedules validation at issuance instead of sleeping or repeatedly fetching the same response. It submits the original bytes to native operation 11 only after local UTC reaches issuance. Native account, device, generation, signature, mapping, revocation and freshness checks remain unchanged. Deferred evidence expires on the original freshness boundary or a 120-second monotonic deadline. Reset, authenticated-scope change and recipient removal discard it. Diagnostics retain a bounded last outcome so a later routine polling message does not hide a failed or deferred install.

Android uses the same bounded 60-second deferral policy through its existing owner. It also submits the unchanged response only after issuance and retains all native checks. No server protocol, authentication protocol, message format, grant or database schema changed. No system clock was changed.

`storage.cpp` diagnostics now expose each retained manifest's canonical recipient and per-device status, using only public identifiers. This makes a wrong-device expectation distinguishable from a phone receive failure without exposing message text or encryption material.

Existing unresolved queued messages are deliberately **not** expanded to newly selected devices. A later snapshot cannot prove the original historical audience, and the existing regression explicitly prohibits that expansion. Their original envelopes, destinations and pending receipts remain intact. These copies still await their original recipient; this patch does not make them arrive on the Motorola. Recovery must use the original authorized recipient and, where admitted, existing History Sync. No recovery was performed on real profiles. New-message delivery after a valid snapshot is installed is the immediate physical acceptance test.

Checkpoint: `cc818c8`.

## Transfer performance investigation

The user's dev.170 Windows-to-Motorola test involved 98,715,413 bytes, v2, 65,536-byte chunks, window 16, 1.014 Mbps average, 1.573 Mbps current, mean ACK turnaround 6,468 ms, peak about 10,056 ms, and ACK-to-next-TLS-submission 7.329 ms. Backpressure observations were zero. Matching receiver timing for that transfer ID is needed to attribute the delay. The mailbox destination defect does not establish the cause of this transfer delay.

The Android relay has independent reader and writer workers. Main-worker server RPC delays alone therefore do not establish that transfer ACK dispatch is blocked. Native history projection, transfer work and other JNI calls share the existing native instance mutex, which must be measured separately. No speculative worker migration or lock bypass was implemented.

The added observations reuse `windows_transfer_timing.hpp`, whose existing implementation has no Windows OS dependency, at Android's authenticated ordinary-file receive and peer-TLS submission boundaries. They retain the existing bounded per-transfer rows and report ACK turnaround, receiver dispatch processing, receiver dispatch-to-ACK submission, sender idle and TLS submission costs. Existing write, hash, flush, commit and journal counters remain available. Archive traffic is excluded from this ordinary-file observer.

`portable/operation_timing.hpp` separates cumulative and maximum native instance-lock wait from processing for RECEIVE and OUTPUT. `AndroidRelayExecution.kt` and `RelayIoTimings.kt` add fixed per-attempt counts, sums and maxima for JNI receive/output and outer socket read/write. Socket-read time includes idle waiting and is not RTT. Peer-TLS submission means local TLS output enqueue, not delivery, server acceptance or the remote ACK. Receiver dispatch-to-ACK includes intended download-cap pacing and journal work before enqueue. The existing relay attempt and public transfer identifiers correlate the stages.

Counters stay in memory, reset with the profile/attempt and are read through existing diagnostics. No continuous database writes, payload logging, new threads or retry owners were introduced. Small additional clock reads, counter updates and transfer-header parsing are the observation overhead. No transfer window, chunk size, bandwidth setting, rate pacing, storage flush, durable ACK, encryption or transport lifecycle was changed. Windows remains uncapped by default; existing user-selected settings and Android 5/10 Mbps/unlimited options are preserved.

No physical after-fix ACK or throughput measurement exists yet. The production-JNI host fixture validates the measurements rather than emulating a Motorola benchmark: its 16 KiB transfer observed 23,394 microseconds ACK turnaround, 1,769 microseconds receiver dispatch and 374 microseconds receiver flush on Windows-host storage. These numbers must not be compared as an Android or Pi performance improvement. A Windows-host native fixture uses real Java TLS and protected Windows storage but not Motorola Keystore, POSIX storage, Wi-Fi or the Pi relay.

Checkpoint: `28d7670`.

## Validation

- Windows mailbox regressions passed: 37.5-second skew, no premature install/refetch, unchanged bytes after issuance, expiry, reset, recipient removal, generation replacement and native rejection. Existing source/account/recipient and authentication rejection cases remain passing.
- Authenticated local server/native integration passed two independently encrypted recipient copies, unknown unpinned sibling exclusion, durable imports, independent signed ACKs, sender/recipient restart and reconciliation. A first device ACK leaves the other pending. An unresolved historical target set is not expanded.
- Mailbox reconnect tests passed real HPKE, durable protected storage, lost-ACK replay, identical receipt regeneration and deduplication. That test's transport/server spool is simulated; the preceding integration uses actual authenticated TLS RPC.
- History/mailbox integration passed archive-first overlap, one durable signed receipt, restart deduplication, content/account/tamper rejection, and no live ACK from a history import alone.
- Android host suite passed 287 tests with zero failures/errors. Lint completed with zero errors and 25 warnings. Final APK compilation passed.
- Production-core JNI host tests passed 16 KiB and 2 MiB files, both directions, five authenticated rejection cases, interrupted 1 MiB protected-journal resume, native owner teardown, and eight reopen/Hello/trust/message/ACK cycles. Additional assertions verify nonzero receiver/ACK timing samples and bounded rows on actual production callbacks.
- Fixed native timing counter tests, existing packet timing/duplicate/stale-route tests, and concurrent Kotlin counter tests passed.
- Windows client, updater and screen-media builds passed. Packaged native-load, voice/screen ABI/dependency checks passed. Final ZIP contents match the validated runtime.
- The final APK reports `4.0.2-dev.171`, arm64-v8a, and the unchanged signing certificate `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`. Native libraries match the final build. A compact repackage preserved every uncompressed APK entry; the larger incremental artifact is retained in `build/dev171-incremental-apk-retained.apk`.

Evidence is retained under `build/dev171-*.log`, `build/tests/dev171-eligibility-production`, `build/tests/dev171-history-mailbox`, and `build/tests/reliable-profile-jni-11803fc101dc4b0da8cc9c1e5cf9acf3`. The package audit is `build/development/4.0.2-dev.171/messaging-repair-package-audit.json`.

## Packages and manual acceptance

Both packages are under this worktree's `build/development/4.0.2-dev.171`:

- `PeerChat-Windows-4.0.2-dev.171.zip`, SHA-256 `d5cb86f16006b1e2892450e2e4fdda6864ff69fbb959e788e8a290cb693f1a18`.
- `Android/PeerChat-Android-4.0.2-dev.171-debug.apk`, SHA-256 `28e731c4fcb1814c831716b59ccdcba7136d60bc36210e4fcdc5432b7079f6a6`.

Close older client instances. Install Android using `adb install -r` without uninstalling or clearing storage. Use the existing profiles and unchanged trust. No install or deployment has been performed by the agent.

1. Leave both clients active for about one minute, then send a **new** Windows-to-phone message. Check phone display and signed receipt, not merely Stored. Capture full diagnostics if it stalls; the eligibility last-outcome and target-device lines should identify the selected scope.
2. Check phone-to-Windows text, restart/reconnect and offline queued delivery. Check background/locked-screen delivery separately. Those OS behaviors remain physical acceptance items.
3. During a representative large transfer, export both reports for the **same transfer ID**. Compare native lock wait, receiver processing/flush, dispatch-to-ACK enqueue and outer socket writes. Confirm text messaging remains responsive, integrity passes and restart/resume preserves confirmed progress.
4. Keep physical transfer performance pending until the matching phone measurements demonstrate the cause and any correction succeeds. Do not infer that these timing observations themselves improve throughput.

The existing accepted dev.170 artifacts and all real identities, history, grants, bindings, pending messages and journals are preserved. The Pi was inspected read-only and was not modified or restarted. No unrelated roadmap work is included.
