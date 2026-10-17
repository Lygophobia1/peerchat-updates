# PeerChat dev.177 registration readiness and connection grace

## Scope and result

This release implements the three approved Android repairs from the [dev.176 networking audit](audits/networking/2026-10-17-dev176-independent-audit.md): reconcile accepted registration before cache capability work, move attachment preparation waits outside the application gate, and retain healthy connections for a best-effort 60-second background grace period. Physical acceptance remains pending. These changes do not establish that all previously reported message or ACK latency is resolved.

Development remains on `codex/native-unicode-emoji` in `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`, starting at audit checkpoint `714d893`. The accepted dev.176 packages and checkpoint `c10ce84` are retained. No Android installation is performed by this milestone. The user separately approved the compatible Pi update; deployment results are recorded below after the validation gate.

## Confirmed causes and corrections

### Scoped registration and cache readiness

Native correctly clears the accepted recipient binding when account, generation, server or certificate pin changes. Kotlin previously retained an executor that considered itself registered, and cache capability processing could also run before the first accepted registration. The audit's JNI fixture reproduced operation 13 rejection at `cache_reply_validation_apply` in these conditions.

`MailboxExecution.registrationAccepted` becomes true only after authenticated server acceptance, native installation and reconciliation against the current binding. `MessagingBackend.configureMailboxEnrollment` replaces the executor on profile, owner, session, account, generation or endpoint changes. Borrowed old work remains fenced by executor identity and the existing scoped-work checks. An authoritative mapping change immediately schedules the replacement mailbox executor. Attachment cache borrowing waits for accepted registration; native binding validation is unchanged.

The selected attachment waits for current readiness for up to 30 seconds using monotonic time, outside the application gate. It is bound to the original local profile, account, server and recipient. Timeout gives an actionable retry message; cancellation or supersession cannot create a stale offer. This selection intent is process-local before a native job exists; process death at that point requires reselecting the file. Existing durable native jobs retain their original recovery behavior.

### Attachment preparation and shared locks

The old `sendAttachment` path held `gate` during content-provider import, copy/fsync and synchronous cache preflight. These waits could block local text admission and native relay dispatch. The new path captures and validates local state under the gate, performs import and network preflight outside it, then rechecks ownership before creating a native job. An in-memory preparation token prevents concurrent selection from creating duplicate jobs. Private imported sources remain retained after ambiguous errors; no incomplete file is published.

The focused delayed-import/preflight test verifies another local operation can proceed while external work waits, and replacement ownership prevents the old result from committing. This is a lock-availability result, not a physical throughput or end-to-end ACK benchmark. The existing account RPC serializer and sequential owner scheduling remain potential bottlenecks; this release does not introduce new transport queues or rewrite the transfer engine.

### Passive background connection grace

Previously, ending the last active runtime lease immediately closed registered sockets. Returning to foreground could not reuse them even if queued cleanup had not run. The runtime now starts one monotonic, generation-fenced 60-second deadline after ordinary UI backgrounding. Foreground return cancels it and can reuse healthy ownership. Explicit logout, reset, close, authorization invalidation and supersession override retention.

Passive grace cancels the 125 ms owner loop. Relay writers park when idle and wake on received data, foreground work or closure. Existing blocking readers and required protocol heartbeats remain. Queued events are drained at the transition so an already-received authority change is not stranded. Expired or replaced event sources cannot continually reschedule passive work. Security-sensitive events and terminal session failures retire retained connections; they do not authorize a new session. Current transfer/wake leases retain their existing bounded policies and do not renew ordinary grace indefinitely.

The registered socket deadline closes connections even if the application cleanup task is delayed. Android may suspend or kill the process sooner; this policy adds no battery exemption, permanent messaging foreground service or new wakelock. It does not guarantee networking while asleep. Returning after expiry uses existing authenticated recovery and durable queues.

## Security and integration manifest

| Files | Change and boundary |
| --- | --- |
| `MailboxExecution.kt`, `MessagingBackend.kt` | Accepted enrollment readiness and scope replacement; existing device keys and native verification remain authoritative. |
| `AttachmentReadiness.kt`, `NativePresenter.kt` | Bounded scoped preparation and actionable readiness failure. No automatic recipient narrowing or security-denial fallback. |
| `AndroidMessagingRuntime.kt`, `BackgroundConnectionGrace.kt` | Process-local 60-second retention policy, timer cancellation and ownership fencing. |
| `AndroidRelayExecution.kt`, `RelayWorkSignal.kt`, `ForegroundEvents.kt` | Passive event-driven output and terminal notification; no peer TLS, framing, ACK or transport protocol change. |
| Android regression tests | Readiness failure/success, cancellation, stale scopes, gate availability, socket retention/expiry and passive wake races. |
| `shared/VersionInfo.cs` | Version 4.0.2-dev.177 only; matching Windows/Android/Pi packages. |

No native API, database schema, enrollment signature format, HPKE envelope, message or transfer protocol changes. No new private keys or trust grants. Cache audience, revocation, integrity checks and durable ACK rules remain unchanged. Windows and Pi behavior is unchanged except the release version; their packages preserve the existing candidate cache features. No changes were made to the other worktree.

## Validation and packages

Source checkpoint: `111c27018c081ba710c72698ca25c892637a3167`. Host fixtures exercise real protocol components but do not constitute physical Android acceptance. The existing audit characterization remains a negative control: native must still reject capability processing without a current accepted binding.

| Gate | Result and evidence |
| --- | --- |
| Android focused tests, then final full unit tests/lint/debug APK | PASS, 367 tests across 80 suites, zero failures/errors/skips; `build/dev177-android-final-reviewed.log`, XML in `android/app/build/test-results/testDebugUnitTest`. |
| Windows full candidate build and runtime dependency checks | PASS; `build/dev177-windows-final.log`. Existing compiler warnings remain. |
| Windows mailbox reliability | PASS, fair outgoing/incoming/receipt scans, eligibility refresh and source/recipient/expiry rejection; isolated fixture `peerchat-windows-mailbox-d629aa4f95dd483e98e102b3814500fe` in the user temporary directory. |
| Group attachment UI | PASS; `build/tests/group-attachment-ui-b8ca9fcc1aa641b89e779e5b7de6ad53`. |
| Windows updater and Pi update helper | PASS, 42 assertions and 5 isolated tests respectively; no live deployment in these tests. |
| Android JNI and Windows authenticated cache/direct interoperability | PASS; `build/tests/dev177-jni-cache/results-cbe876e031f84f3e855b550a5e972367.log` and `direct-compatibility.log`. Both directions, two offline Windows recipients, unpinned-device rejection, lost-upload-response/restart, interrupted download, Unicode bytes, signed notice ACK, concurrent mailbox dedupe/durable ACK and offline history reopen. |
| Portable server compatibility | PASS; `build/dev177-server-parity.log`, fixture `build/tests/server-portability-9b0cc5ae9a0340ffae693a10a54b2282`. Authentication, pinned TLS, canonical JSON/signatures, groups, durable per-device mailbox, restart, expiry/quotas and revocation. |
| Encrypted cache runtime | PASS; `build/dev177-cache-runtime.log`, fixture `build/tests/cache-runtime-179450f0d9934baabb97fb4e84242789`. Actual process-kill recovery, quota/reserve, protected leases, expiry, concurrent publication, revocation and metadata rollback. |
| Linux packages | PASS, x64/ARM64/ARM32 version/runtime/file manifests; ARM32 self-contained runtime 10.0.12. Live ARM32 execution and health are checked during deployment. |
| Immutable Windows ZIP and APK audit | PASS, version, ARM64 APK ABI, original signing identity, native dependencies and every ZIP payload hash; `build/development/4.0.2-dev.177/messaging-repair-package-audit.json`. |

The first JNI cache fixture ran on C:, which had approximately 14% available space, below the unchanged 20% reserve. It correctly returned `cache_storage_unavailable`. A fresh isolated F: fixture passed without relaxing the reserve. The first integrated compile exposed a duplicate local variable name; it was corrected before both final successful test/build runs. All evidence is retained.

The concurrent cache/mailbox fixture measured **31,000 microseconds for one native local text commit during upload**. It does not measure phone dispatch, radio/network latency, recipient delivery or ACK round trip, and has no paired physical before/after comparison. The older 6.5-second ACK observation remains incompletely attributed.

Artifacts, relative to this worktree:

- Windows: `build/development/4.0.2-dev.177/PeerChat-Windows-4.0.2-dev.177.zip` — SHA-256 `7d37fcb14a5af5638779c79a1762b04481171c2f906a3e995f1ea8d0f90726a2`.
- Android: `build/development/4.0.2-dev.177/Android/PeerChat-Android-4.0.2-dev.177-debug.apk` — SHA-256 `a565213ac6a4ed40db0fba35aee98175b0acf4320f8f781c536bf7865b92c5a9`.
- Pi: `build/linux/4.0.2-dev.177/PeerChat.Server-4.0.2-dev.177-linux-arm.zip` — SHA-256 `cd7e24b5b75b77b8bef34fd33d15dc3c2144c813f5ec5f6ef7395c5f32c91c0a`.

The sealed client candidate manifest accurately records no deployment/installation at packaging time. Subsequent Pi deployment is recorded separately; the immutable package is not rewritten.

## Approved Pi deployment

On 2026-10-17 the checked update completed from dev.176 to dev.177. Read-only readiness inspection confirmed ARM32 (`armv7l`), ext4 on the existing 117 GiB filesystem with approximately 104 GiB available, and about 3.3 GiB available memory. No storage assumption or cache policy was changed. The pre-update pinned TLS/login/discovery check passed in 1,343 ms.

The staged ARM32 executable, manifest allowlist, file hashes, dynamic dependencies and reported version were verified on the Pi before switching. The existing helper backed up the old binary/configuration, referenced private configuration files, online and stopped databases, and the stopped ciphertext cache/index. Cache backup: 106,528 bytes in three files, matching hashes and SQLite integrity `ok`. No prior backups were removed.

The startup health sequence passed in 5,988 ms with no retry or rollback. The final process is PID 26620 at `/opt/peerchat-test/server/4.0.2-dev.177/PeerChat.Server`. Three subsequent pinned TLS/login/discovery sequences passed in 1,317, 1,284 and 1,303 ms. These are serial diagnostic health-call durations, not message delivery or TLS-only timings. Database quick check returned `ok`; protected identity/grant table contents, certificate and configuration match the preserved state. Cache remains enabled with verified filesystem binding and its unchanged reserve/configuration.

Rollback backup: `/opt/peerchat-test/rollback/pre-4.0.2-dev.177-20261017T232924Z`. If rollback is authorized, use `/opt/peerchat-test/server/pi_runtime_validation.py` to stop version `4.0.2-dev.177` on port 7443 and start version `4.0.2-dev.176` on port 7443, then rerun pinned health checks. Roll back binaries only against the **current** database/cache; never overwrite newer messages or ciphertext with a backup automatically. Existing dev.176 packages and backups remain available.

Evidence: `build/dev177-pi-preflight-health.log`, `build/dev177-pi-deployment.log`, `build/dev177-pi-final-health.log`; Pi package/helper are retained at `/opt/peerchat-test/incoming/dev177-deployment/`. This server refresh contains no networking or cache behavior changes. It does not address the separately proposed server disk-lock narrowing.

## Remaining work and physical acceptance

### Physical follow-up, 2026-10-17 (not accepted)

Physical testing did not accept dev.177 as a complete reliability fix. Android-to-Windows cache sending failed; one explicit direct transfer succeeded; a subsequent attachment selection was blocked. The user subsequently clarified that text arrives in **both** directions, while Windows continues displaying "Stored · awaiting recipient". Do not report this as a continuing total receive failure.

Read-only Pi inspection found current active/allowed mailbox keys without matching DM-cache capability records for secondary devices `35049f3bf26f…` (account 4) and `0612f2586407…` (account 3). The current phone `61f70fb01a60…` and sender Windows `0cb421c4f102…` have matching capability records. `PrepareDirectCache` requires every eligible recipient binding to be capable. These secondary devices therefore block account-wide cache admission in the current state; they must not be silently removed or auto-trusted. Explicit, verified-device direct transfer remains a different, online-only delivery scope.

Two recent Windows message IDs, `40c66d5f1179486087d3aa853c2cbe93` and `9e947d31e400454e810e8db1e86e7e77`, have phone-target receipt records (318 bytes, ciphertext envelope no longer pending) on the Pi. Their secondary Windows PHONE-device copies remain encrypted and unacknowledged. This supports incomplete **account-wide** delivery, not missing phone acknowledgment at the server. The supplied Windows export is truncated before delivery-target/mailbox execution details, so sender-side receipt validation/reconciliation is not yet verified. Do not promote aggregate status to Delivered without the remaining required receipts. Current bubble wording does not communicate partial-device delivery.

The Android 23:36:36 export records a mailbox transport error and a disconnected event stream, with finite retries scheduled. Approximate correlation of its monotonic/UTC samples places those deadlines about 7.5 seconds before the exported snapshot. Long owner/gate/projection work is recorded, but the samples do not identify a single responsible stage or prove a permanently stuck socket. Existing failed-RPC handling retires the socket and schedules continuation. The phone was unavailable to ADB during this follow-up; the read-only log request was canceled without installing, launching or changing anything.

Two isolated characterization tests passed with JDK 22 (2 tests, zero failures):

- `AttachmentPresenterOrphanTest`: real presenter lifecycle with a retained fake backend reproduces loss of the choice token when the presenter is replaced; the replacement cannot resolve the retained choice and a fresh selection is blocked. This demonstrates a reachable ownership defect, **not** that the physical test took this path. Normal successful direct commit clears the pending choice. Also review non-cancellation exceptions after a choice is assigned. Recommended narrow correction: an explicit new selection may supersede only an uncommitted choice after validating its scope, while failed preparation clears only its own token. Keep staged bytes and durable jobs intact; reject stale resolutions and concurrent preparations.
- `PersistentRpcIdleAlarmCharacterizationTest`: a simulated channel alarm with the real RPC owner demonstrates a possible reuse-boundary race and subsequent successful reopen. This is not the demonstrated physical failure: an archive RPC on the same owner completed only 3.78 seconds before the 8 ms failed fetch. Do not modify timeout/security behavior based solely on this model.

The first combined test invocation used Android Studio's JDK 25 and failed before test execution due to Gradle/runtime compatibility; rerunning with the compatible installed JDK 22 passed. No production implementation, live Pi state, APK or Windows release package changed during this follow-up. These tests characterize unresolved behavior; they are not fix-validation or physical acceptance.

Resume by obtaining the exact second-selection error/UI state and full Windows delivery-target report, then implement/test token ownership recovery and an accurate per-device status projection without weakening aggregate acknowledgment requirements. Correlate current message IDs before changing mailbox or session recovery. Preserve the dev.177 and earlier packages and backups.

1. Install the APK with `adb install -r`, retaining the existing signing identity and data. Run the matching Windows package using the existing profile.
2. Send text in both directions, checking recipient persistence and accurate confirmation. Repeat while preparing a file and transferring it.
3. Select cached attachments immediately after startup and after returning from the system file picker. Confirm no binding rejection, one offer per selection, correct filename and integrity verification. Exercise explicit direct compatibility choice without narrowing the audience silently.
4. Switch apps, use the picker/share sheet, and lock/unlock for 10, 30 and 60 seconds; return and check healthy connection reuse. Repeat after 120 seconds and after a process restart to check recovery rather than reuse.
5. Verify active transfers across these transitions, recipient offline recovery, cancellation and repeated attempts. Confirm no duplicate messages or attachments.
6. Exercise logout/account switching during preparation and grace; old work must not affect the replacement session. Revocation testing must use disposable test devices, not reset real identities.

No physical battery, radio, reconnect or message-latency improvement is claimed yet. Server account/cache disk-lock separation, fair independent control scheduling and richer cross-device trace correlation remain separate reviewed backlog items. A major networking rewrite is not justified by these three defects.
