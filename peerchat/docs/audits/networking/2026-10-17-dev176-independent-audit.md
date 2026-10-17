# PeerChat networking and Android lifecycle audit

## Decision and scope

Android has two reproducible cache-readiness defects: capability processing can precede accepted local mailbox registration, and native registration reset can disagree with the Kotlin mailbox executor's cached state. Temporary backgrounding also immediately closes registered sockets when the last runtime lease ends. Attachment selection has no lease or readiness barrier covering the interval before a transfer ID exists.

Fix scoped registration/readiness first, attachment preparation and control scheduling second, and then consider a **60-second best-effort background grace period**. Grace alone cannot repair missing registration. A complete networking rewrite is not justified. Server cache disk/authorization lock separation requires its own security review and deployment approval.

This checkpoint adds only audit documentation and isolated characterization tests. Production code, live Pi state, Android installation, accepted packages, identities and histories are unchanged. No subagents were used. Proposed production changes require approval.

Repository: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`. Branch: `codex/native-unicode-emoji`. Audited production baseline: `c10ce8416df12e8f7d5531ebaf69134ecf188ce8`. Find the documentation checkpoint with `git log -- docs/audits/networking/2026-10-17-dev176-independent-audit.md`; no self-referential hash is embedded here.

## Physical evidence and timing boundaries

Source exports, all dated 2026-10-17, remain in the original `.codex-remote-attachments` locations:

| Filename time | Build | Observations |
| --- | --- | --- |
| 22-01-47.298062Z | Android 175 | Operation 13 rejected 212 bytes; old generic `jni_input` phase. Multi-second owner/gate waits. |
| 22-08-43.043125Z | Android 176 | Operation 13 failed at `cache_reply_validation_apply`, 212 bytes, 5 ms, private storage available. Capability preceded mailbox registration. Separate direct transfer completed. |
| 22-10-47.774293Z | Android 176 | Fresh remembered authentication, connected events, incomplete readiness. Same retained cache failure and completed direct transfer; no new native transfer ID identifies the last failed retry. |

Full exports, identities, filenames, credentials and message content are not copied into this report.

Direct transfer `d924cb…` went from Android account 4/device `61f70f…` to Windows account 3/device `0cb421…`. All **66,921 bytes** were confirmed, using **64 KiB × 16**. Two ACK samples totaled **5,762,814 microseconds**: mean **2.881407 seconds**, maximum **2.926680 seconds**. The user confirmed this successful attempt used direct fallback. It does not prove cache delivery or subsequent attempts worked.

`route_delay_ms=1845` and `first_payload_delay_ms=29148` are sender-session counters. In `TransferManager::diagnostics`, first payload is measured from first observation of the entry, not recipient acceptance or an isolated network request. Hashing, acceptance and scheduling may be included. `elapsed_ms` continues increasing after completion (65,181 to 189,158 between exports); it is **not** completed transfer duration and cannot support a throughput calculation.

A friend relay attempt reports rendezvous **349 ms**, native peer TLS open **738 ms**, and `PeerTlsReceive` **8,670 ms**. That last interval ends when the owner calls `observedAuthenticatedPeer()`, so delayed owner observation is included. It is not pure TLS crypto/wire time. Native receive/output wrappers also include gate waits. Retained output gate wait reached **3.464239 seconds**, receive gate wait **2.614433 seconds**, owner projection **4.355 seconds**, and mailbox work **8.159 seconds**. These maxima cannot be added as stages of one transfer.

The earlier physical **6.5–6.7-second ACK** and **5–10-second message delay** remain incompletely attributed. New reports prove contention and a 2.88-second ACK mean, not the old event's exact receiver/server/sender split. The nominal **25-second direct-readiness deadline** is a policy bound, not measured handshake time; probe lock waits may extend actual elapsed time. No unrelated device clocks are subtracted.

## Reproduced cache rejection

### Missing accepted local binding

The production chain is:

1. `MessagingBackend.configureMailboxEnrollment` installs the current account, generation, server and pin.
2. `portable/peerchat_core.cpp`, `MailboxBridge_configureEnrollment`, resets `recipientBinding` when that security scope changes. This is correct invalidation and must remain.
3. `dm_attachment_application.hpp`, `Application::reply`, processes accepted cache capability and asks the key callback for the local binding required by the existing device capability proof.
4. `prepareDmAttachments`' empty-key callback requires `instance.recipientBinding`. Missing binding causes operation 13 to return null at `cache_reply_validation_apply`.
5. `MailboxBridge_registered` installs the accepted binding. A server capability response is not a substitute for enrollment.

The 22:08 export records capability start `1792274902687`, completion `1792274903382`, and mailbox registration starting later at `1792274905779`. This matches the reproduced startup defect. The retained failure lacks its failed request ID and inner predicate, so it is a strong match, not proof that every 212-byte rejection has this cause.

The reply handler removes the dispatched capability request and sets `probeAt` to ten seconds later **before** invoking the binding callback. If that callback rejects, later registration does not by itself immediately rerun capability. This explains a concrete avoidable readiness retry delay in this code path, not the physical file-chunk ACK latency. A repair should wake the existing capability scheduler after current enrollment acceptance, without spinning or resubmitting a stale proof.

`CacheEnrollmentGenerationAudit.java` exercises unchanged production JNI/controller code with synthetic accepted replies and isolated host keys/storage. It reproduces startup-before-registration rejection, success after registration, identical rejection after a generation change, success after reconciling the existing binding, and rejection after another generation change. No key replacement or weakened validation is needed for the positive control.

### Kotlin registration survives native reset

`MailboxExecution.serviceAttempt` caches `registeredBinding` and skips native registration while non-null. `MessagingBackend.refreshLocked` can advance generation after an authoritative mapping change without replacing/resetting that executor. Native discards the old scoped binding, but Kotlin still reports `registered=true`.

The Kotlin characterization uses the real executor with a fake native boundary. The JNI test separately proves native generation-reset behavior. Full remembered-login cleanup creates a new executor; this mismatch concerns generation changes retaining an executor, not every login.

**Correction to approve:** scope accepted enrollment readiness to profile object, account, session, generation, server and pin. Reconcile executor registration on every corresponding native invalidation using existing authenticated registration and the existing key. Never replay an old accepted response across scopes. Schedule capability proof processing only after current native acceptance. Preserve pending attachment intent and return a bounded actionable readiness error instead of four blind synchronous preflight attempts.

Impact: removes a deterministic pre-upload failure, not a quantified throughput gain. Risk: medium. Android can fix orchestration without a Pi protocol change. Regressions must cover startup order, generation-only changes, full login, revocation, invalid/stale responses, failed registration and repeated selections.

## Reconnect and attachment readiness

The third export was captured shortly after fresh authentication/event setup. `mailboxAt=Long.MAX_VALUE` is the borrowed-request in-flight sentinel. `phase=native_identity` is assigned before the native call acquires the scoped application gate. Those values do not establish a permanently stopped mailbox worker.

`serverPresenceLive` is projected during refresh, while event connection state changes asynchronously. A recovery snapshot can show authenticated true, presence false and events connected. Authentication, event presence, mailbox enrollment, device eligibility and peer TLS are distinct readiness conditions and should remain distinct in diagnostics.

A conditional rescheduling hazard also deserves a complete-backend test: generation-only supersession can invalidate a borrowed mailbox scope while retaining its executor. Guarded finish can skip rescheduling even though the in-flight flag clears. Other event/eligibility work can reschedule it. The supplied snapshot does not prove this became a persistent stall; do not change it without reproducing that outcome.

The OpenDocument callback in `ConversationScreen` immediately dispatches its URI to `NativePresenter.sendAttachment`. Foreground return separately launches remembered authentication asynchronously. There is no barrier ordering attachment preparation after accepted mailbox/cache readiness. Picker return can therefore race reconstruction independently of TLS or Windows acceptance.

**Correction to approve:** preserve a bounded selection intent, await current scoped readiness, and fence cancellation/supersession so a private source remains recoverable without creating duplicate jobs. Distinguish reconnecting, registering this device, unsupported cache and genuine denial. The last failed retry lacks a new native job or retained intent ID; do not infer Windows journal corruption or wrong-recipient routing from that absence.

## Android lifecycle and background policy

### Current ownership

`MainActivity.onStop` calls `NativePresenter.setForeground(false)`, which updates its `AndroidMessagingRuntime` lease. Losing the last active lease calls `MessagingBackend.setInUse(false)`. `ServerChannel.OperationScope.configure(false, …)` closes registered sockets immediately.

Queued cleanup then advances generation, clears network authorization, closes event/mailbox/relay execution ownership and discards the account RPC owner. Local profile/history remain open and durable. A quick foreground return may skip queued cleanup, but cannot revive already closed sockets. The audit test confirms immediate closure and that `setInUse(true)` cannot reopen that socket.

There is no shutdown on `onPause` alone. A partial overlay may leave the Activity started; a fully covering picker/app generally stops it. File picker, share sheet and lock behavior therefore depend on callbacks, not their names. See the official [Android Activity lifecycle](https://developer.android.com/guide/components/activities/activity-lifecycle).

An established `PeerChatTransferService` **does** hold a finite transfer lease. Its existing foreground data-sync service and bounded CPU wake lock retain permitted work subject to thermal/system limits. It starts only after send/compatibility resolution returns an ID or acceptance returns. No equivalent lease covers picker selection, import or initial capability preparation. Archive/FCM leases have narrower policies and do not automatically grant friend-transfer work.

### Comparison

| Policy | Reuse and reliability | Cost and limitation |
| --- | --- | --- |
| Immediate shutdown | Current last-lease stop closes healthy sockets; short returns require repair/reconstruction. | Least idle network lifetime, but repeated TLS/bootstrap may cost more during frequent switching. Local profile memory is already retained. |
| 30 seconds | Covers short switches and some selections. | Lower retention; misses longer selections. No measured battery ranking. |
| 60 seconds | Recommended initial candidate for ordinary short interruptions. | Retains socket/TLS/session resources; must not keep the full 125 ms foreground loop running. |
| 120 seconds | More reuse across the requested 10–120-second window. | Longer idle/radio exposure and OS-suspension opportunity. Adopt only if measurement justifies it. |
| Genuine work only | Existing finite transfer service helps after a job exists. | Does not cover pre-job picker gap. Release on completion/cancel/deadline; failed queued work must not renew forever. |

### Recommendation pending approval

Use a **60-second best-effort passive connection grace**, separate from foreground processing permission and explicit transfer/wake leases. One monotonic deadline and scope token should control expiry; foreground return cancels it. Late expiry/completion must never retire a newer login/generation. Logout, revocation, supersession, identity/server-pin change and explicit shutdown override grace immediately.

Retain only healthy, currently authorized sockets where Android permits. Do not add a permanent messaging foreground service, new wake lock, frequent keepalive, history projection or 125 ms background polling. Preserve required protocol heartbeats and bounded event-driven work. Explicit transfers retain their existing finite service policy. On return, reuse only current healthy ownership; otherwise recover durable queues and revalidate enrollment. The picker-return readiness barrier is still required.

Doze/App Standby can suspend networking, and FCM offers bounded wake opportunities: grace is not an OS execution guarantee. Do not request battery exemptions as a workaround. See [Android Doze and App Standby guidance](https://developer.android.com/training/monitoring-device-state/doze-standby).

**Causality:** socket shutdown and the preparation gap are demonstrated code paths, with socket behavior characterized on the host. The physical reports lack Activity callbacks correlated to the failed selection, so picker teardown is not uniquely proven as that failure's cause. Battery, radio time, memory delta and physical reconnect latency remain unmeasured.

Lifecycle acceptance must cover paused/stopped overlays; picker success/cancel; share-sheet return; lock/unlock; 10/30/60/120-second returns; process kill; active/expired transfer lease; FCM overlap; logout/revoke during grace; rapid toggles; expiry after replacement login; duplicate callbacks. Measure wake-to-message and durable ACK separately from event connection/UI status.

## Architecture and shared contention

| Path | Actual transport and ownership | Shared bottleneck |
| --- | --- | --- |
| Discovery/mailbox | Authenticated control TLS; per-device ciphertext and signed receipts | Android reused BoundedRpc serializes profile requests; native state shares application gate. |
| Direct DM transfer | TransferManager v2, negotiated 32/64 KiB and 1/4/8/16; authenticated peer TLS, often through opaque Pi relay | Relay I/O workers share native dispatch, owner scheduling and storage. Direct does not necessarily mean LAN or no Pi. |
| Offline DM cache | Existing TransferManager adapter, 32 KiB/window1, authenticated JSON/base64 RPC, HPKE audience | One Android cache command per owner pass; server cache gate. Upload durable offset is not recipient completion. |
| Group cache | Signed descriptor, per-device HPKE, ciphertext blob RPC; server blocks bounded at 64 KiB | Same server cache/account locks. Not opaque TransferManager v2 packet parsing. |
| History Sync | Separate authorized archive relay/TLS, existing durable cursor/import engine | Shared Android owner, native and storage work; separate authorization is not scheduling isolation. |
| Voice/screen share | WebRTC encrypted media, TURN when selected; authenticated control | Separate media path but shared CPU/network and some state processing. TCP_NODELAY does not alter UDP. |

The Pi is **not globally one request at a time**. `DiscoveryServer.Run` accepts sockets and `QueueWorker` dispatches ThreadPool work per connection. Each reused control socket is sequential. Synchronous slow clients can occupy workers, but these reports do not demonstrate ThreadPool exhaustion.

`AttachmentCacheServer.ProcessAttachmentCacheRequest` holds `attachmentCacheGate` across execution and cleanup. DM `WithDirectCacheAuthority` holds the account database gate through its callback; append/read/progress callbacks can do disk work. Group authorization has analogous guarded work. Selected completion paths release the account lock for verification and recheck before commit, but remain within global cache request ownership.

The previous isolated **250 ms** storage barrier delayed an unrelated lookup **259 ms**. This retained result proves interference, not live Pi disk latency. Safe lock narrowing needs scoped immutable authorization plus leases/fences respected by every revocation/relationship mutator, fixed lock order and current-authority recheck before publication/ACK. Simply unlocking disk work is unsafe.

Android's scheduled owner still runs mailbox, cache and archive network scripts synchronously in sequence. Moving waits outside `gate` improved local admission but did not make control scheduling independent. `sendAttachment` still imports the source and performs capability RPCs inside `synchronized(gate)`. A delayed import/reply can block native dispatch/local work. Native encryption, persistence and projection also share ownership.

Healthy RPCs already reuse connections. Android `BoundedRpc` retires after 60 requests, 100 seconds age or 10 seconds idle; serial lock acquisition has a 10-second timeout. Do not add blind replay of failed RPCs: upload success can be ambiguous. Separate bounded control/bulk RPC resources may help after measuring queue delay and preserving session scope and response ownership.

### Cache cadence

The Android owner waits 125 ms after each pass. With one 32 KiB payload command per pass, ideal upload cadence is at most approximately **2.10 Mbps**; download with separate read/progress passes approximately **1.05 Mbps**, before network, disk or other work. These are calculated scheduling bounds, not physical bandwidth. Windows cadence differs; do not apply these bounds to it. Server cache parsers allow bounded ciphertext blocks up to 64 KiB; this is not a v1-only peer parser rejecting v2 frames.

Prioritize bounded event-driven continuations with control-first fairness before increasing concurrency. Preserve durable offsets, leases and verified completion. No third transfer engine or forced v2 packet format is needed.

## Fallback and receiver integrity

Android's compatibility choice comes after local capability readiness. Native capability failure can therefore prevent reaching the explicit direct option. Classified pre-job socket failures and unsupported capability can offer existing verified-device transfer; malformed/native security failures correctly do not become bypasses.

Automatic fallback after a cache job or ambiguous upload is not implemented. The direct API creates a new identity; blindly switching risks duplicate cache attachment and narrower scope. Reconcile retained cache status/publication first. Any account-wide to single-device change needs confirmation naming the device. Revocation, changed identity and genuine denial must remain failures.

Windows separates protected staging, verified content and selected-destination save. `ChatAttachments` distinguishes verified-but-saving/Retry Save from saved completion. The successful direct sample demonstrates that path for this transfer. The latest failure precedes any newly recorded native job, so it does not justify rewriting receiver journals, finalization or v2. Preserve durable ACK, digest verification, atomic export, chosen filenames and restart recovery.

## Ranked repair plan

| Order | Benefit | Risk and regression gate | Update boundary |
| --- | --- | --- | --- |
| 1. Scoped enrollment and picker readiness | Removes reproduced pre-upload failure | Medium; real JNI startup, reauth, stale/revoked binding and repeated selection tests | Android; no Pi protocol change |
| 2. Preparation outside shared gate and bounded control scheduling | Removes remaining inline blocking path; prioritizes text/ACK | Medium; held RPC/disk must allow durable text/control progress; late callbacks cannot affect replacement login | Android first; shared native only if independently justified |
| 3. Approved 60-second passive grace | Avoids unnecessary short-transition reconstruction | Medium; full lifecycle matrix, battery/OS checks; no immortal service | Android only |
| 4. Fair event-driven cache continuations | Removes fixed-delay cadence ceiling | Medium; interrupted upload/download, duplicate ACK, fairness, integrity and bounded memory | Client scheduling, same protocol |
| 5. Server disk/authority fences | Prevents cache I/O blocking unrelated control | High security sensitivity; revocation during disk work, crash commit, quota, lock order | Pi change and explicit deployment approval |
| 6. Identity-preserving post-job fallback | Safe recovery after ambiguous upload | High; no duplicate publication, scope narrowing requires consent, restart recovery | Review interface/design before code |

Ranking reflects reproducibility and likely impact, not invented percentages of physical latency. Fix correctness and scheduling before changing speed caps/windows. A dedicated new file transport or large rewrite is not supported by current evidence.

## Comparison with earlier audits

The [concurrency audit](../../NETWORKING_CONCURRENCY_AUDIT_20261017.md) and [dev.175 report](../../NETWORKING_LATENCY_DEV175.md) correctly identified scheduled wait scopes, hint-writer blocking, server disk/account locks and cache cadence. Their corrections remain present. The concurrency report explicitly deferred inline import/preflight; that remaining path matters here.

The [dev.176 report](../../DELIVERY_CONFIRMATION_DEV176.md) correctly distinguished account/device delivery, projection cost and explicit compatibility from unsafe automatic fallback. This audit advances its previously generic cache rejection to a reproduced local enrollment prerequisite, with supporting startup ordering in a newer export.

Existing JNI cache fixtures registered before capability and used stable setup, missing startup ordering and retained-executor generation reset. Scoped scheduled-RPC tests did not cover picker-driven inline preparation or last-lease Activity shutdown. Passing them was not proof of those UI/runtime paths. The new tests characterize these gaps without claiming a repair or physical acceptance.

## Validation and remaining evidence

**10 focused Android host tests passed**, zero failures/errors: NetworkingReadinessAuditTest (2), BackendLeasePolicyTest (3), ScopedBackendWorkTest (5). Robolectric uses the actual backend scope with a real Java Socket, not a phone TCP session. The mailbox executor test uses a fake native boundary. A separate Java/JNI test uses real native enrollment/cache validation with synthetic accepted replies and isolated CNG/SQLite fixtures.

The JNI run reused the dev.176 host DLL; its mirrored native source matched current source after the established host attachment-I/O include substitution. Production native was not rebuilt. Test commands, exact boundaries and small sanitized output are in [test evidence](dev176-audit-tests.md). Original logs remain in `build/tests/dev176-independent-audit/`.

No full new regression, package, Pi benchmark, phone test or deployment occurred. Earlier test/package results are prior evidence, not reruns. A repair must convert defect characterizations into recovery assertions while preserving negative security tests.

Next paired traces should correlate message/transfer/chunk ID and scope generation through local queue, persistence, connection, encrypted submit, server queue/store/forward, recipient dispatch/write/ACK, sender verification and UI publication. Use local monotonic durations and account for clock uncertainty. Add request ID, generation and time to retained native failure records to distinguish old failures from current attempts.

Then test text during large direct/cache transfers and History Sync, multiple clients, sender offline, interruptions, revoked recipients and ambiguous replies. Measure actual Pi CPU/disk/network pressure and Android battery/memory before claiming resource improvements. Group-control `relay_not_authorized` entries have a different purpose from successful friend relay attempts and require separate authorization correlation, not weakened relay checks.

## Stop boundary

No production registration, grace-period, fallback, scheduler or server lock changes are implemented. Approve the first three scoped repairs before development; review server authorization fencing separately before Pi changes. Existing accepted packages and durable state remain intact.
