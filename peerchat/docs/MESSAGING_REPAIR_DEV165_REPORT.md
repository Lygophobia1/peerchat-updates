# Windows and Android messaging reliability repair

## Outcome and security boundary

Dev.165 is a Phase 1 reliability repair candidate, not completion of independent all-device delivery. The implementation repairs bounded queue processing, receipt/fetch pagination, Android publication and wake completion, application ACK stalls, and diagnostic visibility. It preserves the existing live-first delivery policy.

Phase 2 was stopped by automatic safety review. Historical known device IDs and local pins do not prove current server ownership, recipient-key eligibility or revocation state at Send. The current mailbox key lookup is authenticated at lookup time, but cannot attest eligibility at an earlier queue commit. A device-signed key creation timestamp is not proof of server registration time. Neither unsafe target selection nor a generic nonempty-proof ACK persistence interface was retained.

All unfinished Phase 2 edits were removed without resetting the repository. The original counterexample proving that one live ACK can suppress an unprepared sibling copy remains an explicit known-policy test. **Do not describe this candidate as independently delivering to all verified devices, or Delivered as all-device receipt.** Same-account sent-message mirroring remains excluded.

No phone installation, live Pi access, deployment, account reset, key replacement, grant modification or remote Git push was performed. Group History Sync and Android Voice feature development remain paused.

## Repository and checkpoints

Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.

Branch: `codex/native-unicode-emoji`. Starting HEAD: `0846203`; audit test checkpoint: `5704589`. Accepted dev.162 and existing dev.163 packages/checkpoints are preserved.

| Checkpoint | Purpose |
| --- | --- |
| `4cfb611` | Authenticated owner-scoped full receipt/fetch pagination |
| `7cbcf19` | Windows per-item isolation and fair retry scheduling |
| `beeaaee` | Native application ACK deadlines and retained actionable attempts |
| `d42e1a9` | Android durable publication, notification boundary and wake recovery |
| `b132a8c` | Android invalid-row and receipt-page isolation |
| `25608b1` | Bounded candidate work and redacted retained diagnostics |
| `9d3f6cf` | Periodic job cancellation when credentials lose eligibility |
| `0a8eb58` | Explicit distinction between authenticated duplicate receipts and rejection |
| `396cd9b` | Dev.165 source version allocation |
| `c17d45c` | Passing text-only JNI/Profile and actual local server host harness, with explicit unsupported-platform exclusions |

Dev.164 was an intermediate validation build. It is retained as evidence, not the final handoff. Dev.165 includes the final receipt-validation correction and Android lifecycle cleanup.

Production HEAD for all delivered binaries is `396cd9b3710f29d32e97a71ef0b3a1bbd14f629d`. The final handoff commit adds only this report and the audited packaging script after `c17d45c`; its exact hash is supplied in the handoff message and can be obtained with `git rev-parse HEAD` on this branch. All implementation and host-test changes are committed; the final status check is expected clean. Build outputs, accepted packages and diagnostic evidence remain in their existing ignored directories.

## Repairs and effects

### Outgoing mailbox queues

`DeliveryManager::mailboxCandidates` and `Storage::mailboxCandidates` page through bounded native work instead of repeatedly returning only the first 64 candidates. Wraparound fills a bounded page without duplicating rows. Already stored envelopes are not selected as upload work merely because the logical outbox is still awaiting a recipient receipt. Existing unaccepted envelopes remain eligible even when a live route is present.

Windows and Android isolate outgoing item failures, rotate eligible work and back off refusals. Four failing candidates no longer prevent a fifth valid message from progressing. Retry maps are bounded and are not new message queues; protected history/outbox and immutable ciphertext remain authoritative. Managed retry timers reset on process recreation; native issue records and pending messages survive it.

Expired or changed-key attempts are preserved with protected actionable issue codes. The client does not automatically reseal an ambiguous attempt, delete the outbox, bypass a retired-key rejection or overwrite accepted ciphertext. Such attempts require verified reconciliation rather than a silent infinite retry. This is safe isolation and honest failure reporting, not automatic recovery of every old immutable attempt.

Cost: bounded scans and exception isolation, with fewer repeated uploads/polls after Store. New issue records are written only when a failure reason changes, not on every diagnostic refresh. No message or transfer-journal schema migration is required.

### Live application ACK deadline

`DeliveryManager` timestamps logical message submissions using monotonic time. Windows Node and the portable endpoint recognize a 30-second application ACK deadline independently of socket traffic. Normal healthy sends and ACKs have no new waiting period. On expiry, the existing transport owner closes/retires the route and retains the durable message for authenticated retry/fallback. Retirement clears obsolete submission ownership; a valid late ACK can only act while its exact authenticated submission authority remains valid.

Android's relay writer checks idle native status at most every 100 ms. A closed endpoint closes the existing socket, unblocking its reader and allowing the existing lifecycle/retry owner to recover. Without this adapter correction, a native timeout alone could leave the Kotlin socket task waiting forever.

Cost: a bounded in-memory timestamp per outstanding message and idle status checks on an existing active relay. No extra service, transport owner or permanent connection is introduced. A sufficiently stalled receiver can cause route recovery during concurrent activity; concurrent media/transfer regression is therefore part of validation.

### Android durable publication and notifications

`MailboxExecution` exposes the native committed-message boundary before server ACK and before unrelated receipt/outgoing phases. The production `MailboxPublicationGate`, used by `MessagingBackend`, projects committed history before marking wake output drained. `PeerChatWakeJob` reads drain state before the snapshot used for notification planning, and always performs that notification step before ending the job.

Fresh committed message IDs prevent initial notification baselines from treating a new conversation as already seen. Existing per-conversation notification markers suppress duplicates. A successful empty pass can end reconciliation promptly; it need not hold the backend for the full wake budget. Authentication/session invalidation still propagates through the existing handlers.

Cost: bounded fresh-ID memory and snapshot projection when an import actually commits. Host tests supply a native-history fixture but exercise the same production publication gate and wake-notification step. They do not instantiate Android Keystore/JNI or prove actual OS notification delivery.

### Complete authenticated receipt and fetch pages

The server adds opaque owner-scoped composite cursors and `nextCursor` to existing mailbox operations:

- Receipts order by logical message ID and recipient device; the cursor is also bound to the authenticated sender account/device.
- Fetch pages order by created time, message ID and sender device, bound to the authenticated recipient account/device.

Every page remains bounded by existing row and byte limits. Cursors select positions; they are not grants or ACKs. Foreign-scope and malformed cursors fail closed. Legacy `afterMessageId` requests and cursorless fetch remain supported. Clients detect older responses without `nextCursor` and fall back to bounded legacy behavior, retaining the legacy server's pagination limitations.

Both clients can continue beyond invalid/deferred rows without ACKing or deleting them. Kotlin also isolates unverifiable receipt rows rather than allowing one receipt to prevent later validation. Signature, exact-envelope digest, account and device checks remain native. A valid duplicate receipt is distinguished from invalid receipt rejection without inventing a new delivery event.

Cost: additive JSON fields and at most bounded continuation requests until an empty page ends a sweep. No database schema, encryption format or authorization change. The server-side improvement requires a separately approved Pi update; a new client cannot repair the old server's composite receipt boundary by itself.

### Android background reconciliation

FCM remains an opaque hint into the same backend and JobScheduler architecture. A remembered, eligible profile with mailbox storage enabled has one persisted, network-constrained six-hour reconciliation window with flex, not continuous polling or a permanent foreground service. JobScheduler handles reboot/connectivity deferral.

Eligibility is refreshed after remembered credential save and consent changes. Revocation, rejected saved credentials and sign-out cancel recovery jobs, including sign-out with no saved credential. Existing expedited wake coalescing and bounded retry behavior remain in place.

Cost: occasional bounded recovery wakes for eligible profiles. OS scheduling restrictions still apply; there is no promise of immediate delivery after force-stop, reboot, Doze or lost push. Actual Motorola background/locked behavior remains pending physical acceptance.

## Diagnostics and measurement boundaries

Windows Full Diagnostics includes measured local queue commit count/time, mailbox preparation/native selection timing, authenticated RPC last/max/count timing, a bounded 32-stage history, and retry reasons. Android diagnostics expose receive validation/publication stages and native retained issue reasons. Native local mailbox operation 10 is a read-only bounded issue projection; no peer/server message protocol uses it.

Plaintext, credentials, vendor tokens and private keys are not logged. Measurements stay in memory except failure-state records needed for actionable durable recovery. Diagnostics do not create new connections or a second delivery owner.

The real Schannel/shared-endpoint host fixture now reports a precisely bounded local queue-to-validated durable recipient ACK interval. Its carriage is in memory; it includes local protected SQLite work and TLS processing but excludes handshake, Pi/LAN RTT, FCM and Android OS scheduling. The archived baseline source is `0846203`, with the identical instrumented fixture over 100 messages: median 4,206 microseconds, p95 4,652 microseconds, mean 4,249 microseconds. The after measurement is recorded in final validation evidence below. This fixture uses the legacy single-target path and cannot establish the latency of unimplemented independent target selection.

Unavailable physical send-to-phone, wake-to-import, wake-to-notification and reconnect timings are not invented. Use the stage timestamps and the physical procedure below to collect them.

Coverage is not a complete end-to-end distributed trace. Queue commit, native preparation, RPC durations, retry classification and publication/wake boundaries are available; send-to-first-ACK is measured by the host fixture, not retained as a universal production latency statistic. Push attempt does not prove delivery or notification posting. No cross-host clock subtraction is reported as end-to-end latency. Fixed-target/per-target diagnostics remain absent because Phase 2 is not implemented.

## Shared interfaces and compatibility manifest

| Components | Change and compatibility |
| --- | --- |
| `storage.hpp`, `storage.cpp` | Additive candidate/issue operations using existing protected object storage; legacy message/outbox formats unchanged |
| `delivery_manager.hpp` | Submission timing, bounded candidate selection, actionable immutable attempts, explicit receipt authentication result; no wire-format change |
| `node.cpp`, `portable/reliable_endpoint.hpp` | Existing-owner ACK deadline handling; direct/archive/group authority remains separate |
| `windows_mailbox.hpp`, `portable/peerchat_core.cpp` | Matching native adapter behavior; JNI existing diagnostic projection extended, no identity/key export |
| `desktop/MailboxExecution.cs`, `App.cs`, `CoreAdapter.cs` | Fair per-item service, opaque cursors, due scheduling and bounded diagnostics; paired with this package's native DLL |
| Android mailbox/backend/relay/wake classes and manifest | Publication gate, fair pages/retries, persisted network job permission and lifecycle cleanup; signed replacement APK uses existing identity |
| `server/EncryptedMailbox.cs`, `EncryptedMailboxChannel.cs` | Additive scoped cursors and response fields; legacy requests remain supported; no live migration/deployment |
| Tests | Audit counterexamples converted where repaired; missing sibling-copy counterexample remains labeled as the blocked Phase 2 policy |

No PCGE/group encryption, attachment envelope/cache authorization, transfer chunk/window/limit, media protocol, emoji artwork or message-history format changes were made. Existing client/server authentication, TLS fingerprint checks, HPKE binding verification, revocation and historical-access rules remain required.

Historical records with unresolved device provenance remain protected. No old-server ownership was inferred, no unresolved rows were reassigned/deleted, and no identity migration was attempted.

## Phase 2 design requiring approval

Before implementing frozen target manifests, define an authenticated server-issued eligibility snapshot/revision available at queue time. It must bind account, device identities, active mailbox bindings/online eligibility, revocation state and bounded freshness. It must not turn historical contact membership into authorization, or let a device newly enrolled later inherit old live-message targets.

The receiving-device target set and per-target ACK state must be supplementary protected records. Storage mutation must be private to the existing delivery owner, reached only after exact authenticated live-submission checks or signed, digest-bound receipt verification. Target ACK, aggregate completion and outbox settlement must commit atomically. A healthy first recipient may receive immediately while its aggregate target-selection/completion remains pending; the choice of snapshot cutoff must be explicit rather than inferred from a client timestamp.

Approval is needed for that eligibility-snapshot contract and its freeze-time semantics. This is not a request to approve independent delivery again; it is the specific additional server/client boundary the current protocol does not establish. No such protocol, target state, all-device label or unsafe partial implementation is included in dev.165.

Proposed minimal review input, not an implemented protocol: an additive authenticated eligibility response binds requesting sender/account, recipient account, eligible recipient device IDs, verified key-binding generations, server eligibility/revocation revision, issuance time and bounded validity. Existing HPKE/signed binding verification remains mandatory. Persist the accepted snapshot and target manifest atomically before treating targets as frozen; recheck current revocation before delivery. The unresolved cutoff choice is whether offline queued Sends wait for a fresh snapshot (and how enrollment after the user pressed Send is excluded), or require a prior valid snapshot at that Send. A later lookup must not silently broaden an earlier target set. This choice and the exact server evidence/token format require security approval before coding, not a guessed client-only enrollment timestamp.

## Server rollout boundary

Linux ARM32, ARM64 and x64 candidates are prepared separately from Windows/Android. Cache support remains the existing explicit candidate feature, disabled by default; no storage path, quota, key, certificate, database or cache content was changed on the Pi.

Before any separately authorized deployment:

1. Read-only verify actual running PID/executable/version, architecture, configuration path, database path, cache mount/free space and current process supervisor. The older deployment report's paths are references, not current observations.
2. Back up the exact running binary/configuration and create a consistent SQLite backup that includes committed WAL state. Verify hashes, restrictive permissions and reopen integrity. Preserve existing certificates, credentials, keys, TURN configuration and cache files.
3. Stage the matching architecture, verify package hashes/runtime requirements, and retain the previous executable/configuration.
4. Stop/restart through the verified supervisor only after approval. Smoke-test authentication, discovery, legacy mailbox requests, new scoped pages, relay pairing and cache authorization using isolated test identities where possible.
5. If critical checks fail, restore the previous executable/configuration and verify recovery. Do not automatically restore an old database over newer writes. Pagination is additive and requires no schema rollback.

## Physical acceptance checklist

Install only when the user chooses, using Android `adb install -r` and the same Windows profiles. Never uninstall, clear data or regenerate keys. Keep the accepted packages available for rollback; do not restore stale databases over new messages.

1. With the other same-account device closed, send Windows → Motorola and Motorola → Windows. Confirm actual history rows, durable ACK and restart persistence, not only Online.
2. Repeat with the phone normally backgrounded and locked; distinguish notification display from durable receipt. Check duplicate wake/notification suppression and foreground catch-up.
3. Restart each app with queued messages; interrupt network before Store, during an uncertain response and after confirmed Store. Confirm message retention and dedupe.
4. Exercise a deliberately stalled test recipient beyond 30 seconds; verify automatic route recovery, accurate pending state and later delivery. Do not deliberately corrupt real profiles.
5. Verify retained invalid/expired/key-changed attempts show actionable diagnostics while a later valid message progresses. Do not reset bindings to clear a queue.
6. After an approved Pi update, test more than 64 receipts including two destination receipts for a boundary message, and valid messages behind retained invalid rows.
7. Recheck group attachments, clipboard screenshot Send, DM file integrity/save, emojis, voice/screen share and authorized History Sync. Independent all-device delivery is not an acceptance criterion for this Phase 1 candidate because Phase 2 remains blocked.

## Automated validation

All evidence below is isolated host/build evidence, not physical Motorola acceptance. Logs are retained under this worktree's `build` directory unless an explicit isolated path is listed.

| Validation | Result and evidence |
| --- | --- |
| Windows/native build and packaged DLL loading | PASS; `dev165-windows-build.log`, `dev165-package-audit.log`; restricted dependency loading, native warning-as-error build, HPKE/audio/group media ABI and lifecycle checks |
| Android ARM64/JNI compilation, debug APK, JVM unit tests and lint | PASS; 233 tests across 53 suites, no failures/errors/skips; lint 0 errors and 25 existing warnings; `dev165-android-build.log` |
| Mailbox cryptography and authenticated server compatibility | PASS; `dev165-mailbox-regressions.log`; real local pinned TLS, offline fetch, signature/tamper/expiry/key rotation/revocation/quota/reopen, authenticated duplicate versus forged receipts |
| Native durable queue and receipt races | PASS; `messaging-repair-native-final.log`; before/after-commit failures, lost ACK/replay, uncertain Store marker gap, actionable immutable attempts, 500-row bounded fairness across conversations |
| Server fetch/receipt pagination | PASS; `dev165-pagination-regression.log`; 65-row boundaries, shared logical IDs, retained invalid records, foreign-owner cursor rejection and sweep termination |
| Windows production mailbox executor | PASS; `dev165-windows-mailbox-regression.log`; four failures cannot starve the fifth, invalid rows cannot block continuation, retained receipt boundaries, bounded redaction |
| Android publication/wake integration | PASS in the production gate and wake-step JVM tests included above; later failure cannot hide committed messages, initial baseline and repeated wake suppression; not an actual OS job/notification or Keystore proof |
| Shared endpoint live/restart/replay | PASS; `dev165-endpoint-latency.log`; 102 real Schannel/shared-runtime lifecycles, durable text ACK and file integrity |
| Production JNI/Profile and actual local server relay | PASS; `dev165-jni-host-integrated.log`; three socket cancellation deadlines, eight profile reopen/lost-ACK cycles, wrong-pin/chain rejection, remembered-device resume, authoritative friendship snapshot, superseded presence rejection, three relay/reopen cycles, six bidirectional deliveries, two offline queued IDs drained once, exact ACK/dedupe and nonfriend rejection |
| Protected storage and existing History Sync | PASS; `dev164-storage-regressions.log`, `dev164-group-history-regression.log`, `dev164-archive-security.log`; 74 group and 67 archive imports, replay/provenance/revocation/cursor/live-import races. Tested code unchanged by the final dev.165 receipt/lifecycle corrections |
| Existing transfer and concurrent media lifecycle | PASS; `dev164-transfer-regression.log`, `dev165-media-transfer-regression.log`; 10 MiB interrupted transfer around actual screen stop, rejected mailbox object, fresh relay pairing, prefix recovery and final integrity |
| Group screen sharing | PASS; `dev164-group-screen.log`; one/two/three viewers, real RTP/MFT receiving, generation/route lifecycle. Production media code unchanged |
| Emoji and clipboard | PASS; `dev164-emoji-regression.log`, `dev164-clipboard-regression.log`; color/favorites/multi-insertion, Unicode round trip and explicit screenshot Send. Feature code unchanged |
| Group output and filenames | PASS using a short isolated fixture path; `dev165-group-output-shortpath.log`; restart/export/overwrite identity/collision tests. Long-workspace-path invocation failed with Win32 error 3; no production output rewrite was made |
| Actual Application → packaged server group attachments | PASS unchanged fixture against dev.165 server; `F:\PeerChat-isolated-cache-tests\application-7c831c2b1ce34939b1665ee0327598ed`; authenticated capability, upload once/two independent recipients, native HPKE, real TLS, restart, key ACK, verified finalization/receipt/open/export, Unicode and epoch 5→7 |

Two important environmental failures are retained rather than disguised:

- The first concurrent-media test used the incomplete intermediate `Client` folder and could not load ScreenMedia. Repeating with the complete `Package` runtime passed. The delivered ZIP contains that complete runtime.
- The Application/cache fixture on C: was refused because the filesystem had approximately 14.6% free space, below the existing 20% reserve. The unchanged test passed on the existing isolated F: test volume with ample free space. Cache admission/authentication was not weakened to pass it.

The older Windows-host JNI runner had platform drift after newer attachment/media code: POSIX-only file includes, missing media/group link dependencies, a missing attachment-directory facade method and a reader that omitted the existing relay scope blob. Its relay helper also omitted current managed compile dependencies and incorrectly read nested device presence as a flat account row. Test-only adapters now explicitly refuse attachment file operations and media activation; production messaging/authentication/ACK/SQLite/TLS code is retained. The entire corrected runner passes from fresh isolated storage (`dev165-jni-host-integrated.log`), including the actual packaged local server's resume/supersession/relay cases. Earlier compile/runtime failures are retained in `dev165-jni-host-final*.log` and `dev165-jni-server-runtime*.log`; one reused test database caused a disposable registration conflict, resolved by a fresh fixture, not a user-data reset. These tests use host SQLite/DPAPI and Java TLS with disposable identities, not Android Keystore, POSIX storage, notifications or actual OS jobs. The MinGW harness retains existing misleading-indentation warnings as nonfatal without modifying production source; other compiler errors remain fatal.

Independent final security review found no bypass in authenticated duplicate receipt handling, owner-scoped cursors or ACK deadline ownership. It confirmed the Phase 2 eligibility-snapshot boundary described above. Some requested matrix cells therefore remain open: independently targeted two-device delivery, real Motorola FCM/locked/reboot/network switching, physical simultaneous reconnection, and execution/health of the ARM32 server on the Pi.

## Latency comparison

The same 100-message Schannel/SQLite fixture was run on archived baseline `0846203` and repaired source, alternating three trials. Values are microseconds; this is local queue append through validated durable recipient ACK, with in-memory opaque carriage and no handshake/network/FCM delay.

| Alternating trial | Baseline median / p95 / mean | Repaired median / p95 / mean |
| --- | --- | --- |
| 1 | 4,381 / 5,946 / 4,626 | 3,880 / 4,650 / 3,940 |
| 2 | 4,277 / 4,704 / 4,302 | 4,166 / 4,691 / 4,251 |
| 3 | 4,103 / 4,915 / 4,158 | 4,144 / 5,507 / 4,295 |

An earlier unpaired repaired run during concurrent builds was slower: median 4,594, p95 7,764 and mean 5,003, versus the earlier baseline 4,206 / 4,652 / 4,249. These are retained loaded-host results, not excluded failures. Alternating trials show no consistent normal-path regression, but variable tails and the third trial's slower repaired tail preclude claiming a measured speedup. No physical latency or independent-device latency claim is made.

## Packages and source identity

Both client artifacts use production source checkpoint `396cd9b3710f29d32e97a71ef0b3a1bbd14f629d`, version **4.0.2-dev.165**. Subsequent handoff/test-harness commits do not change packaged production source.

Paths relative to this report's worktree root:

| Artifact | Path | SHA-256 |
| --- | --- | --- |
| Windows ZIP | `build\development\4.0.2-dev.165\PeerChat-Windows-4.0.2-dev.165.zip` | `1b4add80a62619b6285ec4a8ffd1c71ee704e145997eff9740c14890dc8ee552` |
| Android ARM64 debug APK | `build\development\4.0.2-dev.165\Android\PeerChat-Android-4.0.2-dev.165-debug.apk` | `a213c86ea3e1c6afc1295b8f2e41bb94665077f4d05e4c4c0a4083569a01e1f3` |
| Linux ARM32 self-contained candidate | `build\linux\4.0.2-dev.165\PeerChat.Server-4.0.2-dev.165-linux-arm.zip` | `8665eacc2af6467e2293213005e464f31d4821edbf541109e34a1a81e133575a` |
| Linux ARM64 framework-dependent candidate | `build\linux\4.0.2-dev.165\PeerChat.Server-4.0.2-dev.165-linux-arm64.zip` | `5e9e4b568f0e9f6c46c4a021c898aa27610ffc324e6c7cb9ed49797b096ef2bd` |
| Linux x64 framework-dependent candidate | `build\linux\4.0.2-dev.165\PeerChat.Server-4.0.2-dev.165-linux-x64.zip` | `1e1f0f0250a34a58fdf8140bf19833af3ffd5a41e23120d7f6ae6e58fc403420` |

Windows unpacked executable: `build\development\4.0.2-dev.165\Package\PeerChat.Desktop.exe`. Use the complete Package/ZIP, not the incomplete intermediate Client directory.

Package checks verify the desktop version, restricted native dependency loading, every ZIP payload hash and unchanged production source. APK checks verify versionName, ARM64 ABI, signature and the unchanged signer SHA-256 `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`. The APK targets API 35, minimum API 26, has versionCode 2 and FCM configured. ARM32 ELF architecture and contents were audited, but no Linux execution or current Pi health claim is made. ARM32 uses the bundled .NET 10.0.12 runtime; framework-dependent variants require a compatible installed runtime.

Machine-readable audit: `build\development\4.0.2-dev.165\messaging-repair-package-audit.json`. Packaging script: `scripts\package-messaging-repair.ps1`. It refuses to overwrite existing handoff ZIP/APK artifacts. Accepted dev.162/dev.163 artifacts remain untouched; no installation or deployment was performed.

## Resume and acceptance boundary

1. Physically test Phase 1 using the checklist above, preserving existing profiles and signing identity.
2. Review/approve the specific authenticated target-snapshot contract before any Phase 2 code; do not reinterpret local pins or creation timestamps as that contract.
3. Separately authorize a compatible Pi rollout if complete server pagination is to be physically tested. Use the backup/rollback plan above, not a database rollback over new writes.
4. Retain host JNI platform exclusions separately from the successful Android build/JVM tests and physical validation.

**Stopped for physical acceptance and the new authorization-contract boundary. No unrelated roadmap work is authorized by this handoff.**
