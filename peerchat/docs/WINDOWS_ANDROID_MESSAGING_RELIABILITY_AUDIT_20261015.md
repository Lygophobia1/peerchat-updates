# Windows and Android messaging reliability audit

## Decision summary

PeerChat has useful durable and authenticated messaging foundations, but its current routing policy is **live first, mailbox fallback**, not guaranteed delivery to every authorized device. An account can therefore look online and receive a message on Windows without Android receiving its own copy. Separately, reproducible queue and receipt-processing defects can delay delivery indefinitely, and an Android publication boundary can hide an already committed incoming message from the wake job.

Recommend independent delivery to a fixed set of eligible, verified destination devices, with per-device progress. Prefer an active device's live transport for speed, not as a reason to omit other authorized devices. Keep History Sync for older history and recovery. Implement the bounded stall fixes first; obtain approval before changing target ownership or introducing sent-message mirroring.

The audit did not demonstrate plaintext loss from a successful durable receive commit. It did demonstrate missing device copies, queue starvation, and an unreachable per-device receipt. Neither Online nor Delivered currently proves all-device delivery or display. These counterexamples explain possible inconsistencies; they do not identify which one caused every previous physical incident.

## Scope and preserved baseline

- Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.
- Branch: `codex/native-unicode-emoji`.
- Audited source HEAD: `20b6b2271df9fb8e613305f3d0c95fe2b4c0fe8c`, initially clean.
- Preserved accepted dev.162 checkpoint: `42da2cf`; dev.163 package source: `1ebc47ae16dc5eb96728d2d80da448ed36e43301`; signed public group attachment history references: `46e2d2d`.
- This milestone adds isolated tests and this report only. Production C++, JNI, Kotlin, WPF, protocol, database schemas, and packages are unchanged. Group History Sync remains paused.
- No live Pi or phone inspection, installation, deployment, mailbox fetch, account change, or user-data mutation was performed. The local server fixtures use disposable databases and TLS credentials. The running Pi version/configuration was not reverified; the last deployment report documents dev.158, which must not be treated as a fresh observation.
- Independent Android, Windows routing, and server/security reviews informed the findings. They are code reviews and host tests, not physical background acceptance.

## A Current architecture

### Identity and device selection

An account ID is not a device identity. Windows and Motorola devices can belong to the same account while retaining different identity keys, recipient HPKE keys, enrollment credentials, and authenticated session leases. Historical physical evidence in `ANDROID_DEV161_ROUTING_AND_WAKE_FINDINGS.md` identifies account 4 with distinct Windows and Motorola devices. That evidence showed separate mailbox ciphertext copies and independent ACKs; it does not establish every later message had two copies.

The server's account presence projection selects a live device lease, falling back to a registered device; it also exposes online devices (`server/PeerChat.Server.cs`, `LiveProfile`, around lines 439–460). That projection is a route candidate, not authority to replace an existing fingerprint. Native account/contact mapping and existing verified pins remain required.

Android's `AndroidStoragePlatform` delegates schema, transaction, outbox and dedupe policy to native Storage; its platform adapter executes bound SQLite statements. Protected record bodies use profile-bound AES-256-GCM keys in Android Keystore (`SecurityProviders.kt`), not whole-database-page encryption. Identity signing and recipient P-256 agreement use separate Keystore aliases; provider checks reject exportable/wrong-purpose recipient keys. The separate device-key-registry database avoids self-contention while a key lease spans decrypt and durable message acceptance. An existing profile database with missing identity metadata/key fails with recovery required rather than silently generating a new identity. JNI/scoped enrollment validates account, authentication generation, server, pin, approved source binding and HPKE possession before accepting scoped recipient registration. Reliability repairs must reuse those checks.

Windows Send starts in `desktop/App.cs` around 4500–4514. The native account mapping canonicalizes the conversation (`node.cpp` around 1501–1514; `reliable_peer_protocol.hpp` around 160–179). Android uses the same portable durable send machinery through JNI, scoped to its authenticated account, generation, server and certificate pin (`portable/peerchat_core.cpp`, `application/MessagingBackend.kt`). A friend account may have multiple known peer IDs; current Android history projection collects across those IDs rather than displaying only the currently selected device.

### Delivery paths

1. **Send and local persistence.** `reliable::queueText` creates a stable message ID/order and appends protected message plus outbox in a SQLite transaction. The caller returns after append succeeds. Encryption/submission follows; transport acceptance is not delivery.
2. **Live peer path.** The authenticated reliable endpoint sends over peer TLS. Ordinary relay presence uses opaque TCP relay carriage; peer authentication is end to end. Native receipt validation/import precedes sending the message ACK. WebRTC media transport is separate and is not the DM mailbox.
3. **Mailbox fallback.** With storage consent enabled, the application pump obtains signed recipient bindings through authenticated `mailbox_keys`. Native code verifies eligible devices and prepares individual HPKE ciphertext envelopes before submission. A usable live winner or pending first attempt suppresses fallback. Each server row is scoped to sender device, logical message ID and recipient device.
4. **Server storage.** `StoreMailbox` checks account ownership, friendship/block state, signed envelope, TTL and quotas. New stores require an active recipient binding; exact retries of an already stored envelope can succeed after that key retires. It commits before returning Stored. The Pi holds ciphertext, not recipient decryption keys. Store completion triggers an opaque event/FCM hint after commit (`EncryptedMailboxChannel.cs` around 25–34).
5. **Recipient fetch and ACK.** The recipient re-establishes its own authenticated session/binding, fetches device-addressed ciphertext, verifies sender/account/key scope, decrypts, commits protected history, persists a signed digest-bound receipt, then submits `mailbox_ack`. The server clears that device's envelope and retains its receipt. Sender logout does not prevent fetch or ACK.
6. **Sender reconciliation.** The sender later fetches signed receipts and validates them natively before changing local delivery state. Server storage ACK and recipient delivery receipt have different meanings.
7. **Publication.** The application projects durable history into snapshots and UI. Android notifications consume these snapshots. A durable ACK does not attest that a bubble was displayed or a notification posted.

### What ACK ownership currently means

| State or signal | Actual meaning | Does not prove |
| --- | --- | --- |
| Queued | Protected local row/outbox committed | Server acceptance or recipient receipt |
| Stored | Server reports ciphertext committed for an envelope | Recipient decryption, all devices stored, or display |
| Delivered | A valid live ACK or signed mailbox receipt from a recipient device was accepted | Every eligible device received it; user read it |
| Transport ready | Authenticated connection is usable | Application ACK progress |
| Android Online | Authenticated and server presence projected live | Recipient-key registration, mailbox processing, delivery, notification |
| History import | Independently authorized history was verified/imported | A new live delivery receipt or sent-message mirror |

`Storage::append` (storage.cpp 365–380), `Storage::acknowledge` (534–543), and `DeliveryManager::acceptMailboxAuthorized` (delivery_manager.hpp 155–183) enforce the main durability boundaries. SQLite uses WAL/FULL synchronization; actual power-loss durability still depends on the platform/filesystem honoring I/O. This is at-least-once retry with durable idempotent import within the retained identity/receipt scope, not exactly-once network delivery.

The server already keeps separate per-device envelopes and receipts. However, the sender's global Delivered state removes its logical outbox after the first device ACK. A sibling envelope prepared earlier survives this ACK and can still upload; a sibling copy not prepared before that ACK cannot normally be created afterward. `tests/mailbox_protocol_test.cpp` already covers the prepared-sibling case. The new audit test covers the missing-sibling case. These are different boundaries.

### Android lifecycle and wake ownership

`AndroidMessagingRuntime` shares one backend among UI and bounded wake leases. When the final active lease ends, the backend quiesces network/authentication work without deleting protected profile/history. Foreground start/resume reconciles authentication, server presence, friends and native state. Server-confirmed event-session supersession remains distinct from a recoverable invalid session; the implementation does not continually reclaim a superseded event lease. Superseding an event subscription is not itself revocation of the account session token: separately authenticated mailbox RPCs check token/device ownership and do not use the event-supersession check. Keep these authorities distinct when diagnosing reconnects or login transitions.

`PeerChatFcmService` accepts an opaque two-field hint and schedules `PeerChatWakeJob` through `JobScheduler`/`OpaquePushWake`. There is no current WorkManager owner. The job has a 30-second budget, uses the existing backend and remembered credentials, drains mailbox work, and publishes notifications. Token ownership is bound to authenticated device sessions; the server sends high-priority, collapsed, finite-TTL hints without message plaintext. Push submission is best effort and is not a message ACK.

Application startup requests the current Firebase token; `onNewToken` saves its replacement locally using the profile record protector and schedules reconciliation. Authenticated backend refresh submits the token to the server. Server registration upserts a device-owned token with 30-day expiry; lookup also requires the matching remembered device and current device ownership (`PeerChat.Server.cs` 122–126, 820–824). An expired token is not evidence of message receipt and must be renewed through normal authenticated registration, never by trusting a push payload.

Storage consent OFF suppresses receiving-key registration/offline storage; it is not a promise of background mailbox delivery. Live authenticated delivery remains a separate path. Current dev.162/dev.163 APKs have FCM configured; the old dev.161 missing-FCM build and old sender-device history projection defect are historical findings, not newly discovered defects in this source.

### History Sync and groups

DM History Sync validates independent grants, source/reader roles, provenance and durable cursors. It imports without inventing recipient ACKs or automatically granting ordinary multi-device delivery. Unresolved historical device ownership remains protected; it must not be reassigned to make delivery tests pass.

Group text currently uses authenticated ciphertext fanout to online sessions with a short-lived in-memory delivery cache (`server/GroupCiphertextFanout.cs`), not the durable DM mailbox. Do not extrapolate DM offline guarantees to groups. Dev.163's authorized group history recovery is preserved, not extended here. Group attachments and cache protocols are unchanged.

## B Confirmed defects and policy mismatches

### B1 Expired pending envelopes can poison the outgoing mailbox pass

`Storage::pendingMailboxUploads` (storage.cpp 570–575) selects old saved unaccepted envelopes without removing expired candidates. `DeliveryManager::prepareMailbox` (94–108) reuses existing immutable wire but rejects expired wire. `desktop/MailboxExecution.cs` executes a whole pass without per-message failure isolation; Android's service also aborts on a thrown preparation error. The same row stays pending after the exception. Repeated passes can encounter it indefinitely and block later work.

The native audit creates real signed/HPKE wire, advances its validation clock to expiration, observes rejection, and confirms the identical row remains pending. It does not expire or delete a real message. Impact: indefinite delay/unactionable queue state, not demonstrated deletion of plaintext history.

### B2 Key rotation before first store can strand immutable ciphertext

Existing-envelope reuse checks account/expiry but does not replace it with ciphertext for a newly verified key generation. The server correctly allows a retired-key exact retry if the same digest is already stored, but rejects a new upload to a retired key (`EncryptedMailbox.cs` 56–66). A sender envelope created before rotation and not accepted by the server can therefore repeat forever.

The native audit proves old generation-1 wire is returned after a signed generation-2 binding is supplied. Existing server tests prove the retired exact-retry/new-store distinction. Rotation must not be “fixed” by weakening that distinction or overwriting an accepted immutable envelope.

### B3 Fixed outgoing batches can starve later messages

Both clients select up to four pending messages per service pass. Rejected `mailbox_store` replies leave those candidates pending without per-item scheduling/backoff. Four stable rejects can occupy every pass while the fifth is never attempted. Kotlin `MailboxAuditFaultTest` reproduces two passes attempting the same first four IDs and never the fifth. Quota, unavailable/revoked key, or another permanent refusal can produce this condition. Its fake server/native boundary proves service fairness, not encryption or a particular phone incident.

### B4 Android can ACK a committed import before publishing it to the wake snapshot

In `MailboxExecution.kt` 126–129, the acknowledgement callback runs after the real server ACK. `MessagingBackend.kt` 585–592 updates the ACK marker in the callback, but publishes native history only after the entire service returns. A subsequent receipt lookup/outgoing failure enters the catch path before publication. `backgroundOutputDrained` can observe the ACK marker; `PeerChatWakeJob` can then finish with a stale snapshot. A later notification baseline can make that received message appear already seen.

The Kotlin audit reproduces receive → ACK callback(true) → receipt lookup exception. Snapshot/job consequences follow the reviewed production control flow; this test does not run the full Android job, JNI, FCM or notification OS. The message remains in durable native history. Impact: delayed/hidden presentation or notification despite safe durable receipt.

### B5 Receipt pagination can permanently hide a sibling device receipt

Server receipts have per-device row identity, but `MailboxReceipts` orders and cursors only by `message_id` (`EncryptedMailbox.cs` 103–106). A 64-row page can end inside a message with two destination receipts. The next `afterMessageId` excludes the remaining receipt; resetting the cursor repeats the same first page.

The production-query fixture stores 65 receipt rows with the final two sharing a message ID: page one returns 64; the next page returns zero; restarting the sweep returns the same 64. Receipt blobs in this test are synthetic and inserted directly into an isolated DB; this is a pagination counterexample, not a signature acceptance test. Real signed receipt authorization is separately covered by the accepted mailbox fixture. Impact: incomplete per-device receipt reconciliation; not proof of lost recipient content.

### B6 Current live-first policy does not meet independent device delivery

The native audit submits a message on an authorized live route, accepts that recipient's valid ACK, then requests preparation for a verified sibling device. Preparation returns empty and no sibling ciphertext exists. This is current policy, not an identity bypass: the first ACK correctly proves one device received the message, but the global outbox no longer owns an unprepared sibling delivery.

Same-account sent-message mirroring is also absent. Current mailbox key lookup rejects self-account destinations (`EncryptedMailbox.cs` 39–42); normal sends target a friend account. A new self-mirror lane cannot safely be implemented by removing the self-account check alone.

## C Conditional risks and coverage gaps

| Condition | Evidence and consequence | Status |
| --- | --- | --- |
| Live TLS remains connected but stops application ACKs | DeliveryManager's submitted IDs lack a per-message ACK deadline; Node has a bounded in-flight set; usable live route suppresses mailbox fallback. Socket write timeout does not cover a successfully flushed but unacknowledged message. | Code-supported stall condition; no physical reproduction in this audit |
| First fetched page is entirely undecryptable/unverifiable | Cursorless first-64 fetch can repeatedly return those rows; Windows aborts on first receive error, Android continues validation but cannot reach beyond the page. | Conditional head-of-line risk; no forged-message exploit established |
| FCM delayed/lost, process not active | Foreground reconciliation exists; no independent persistent reboot/periodic reconciliation receiver/job was found. No active lease means no ordinary backend pump. | Background recovery coverage gap, not proof of a currently lost push |
| Duplicate session on same account and same device | Supersession deactivates the old event subscription, not its account session token. Different verified devices must retain separate leases. | Test repeated foreground/wake transitions and event versus RPC authority before altering behavior |
| Revocation after valid local import but before server ACK | Server may reject ACK; local verified history must stay protected and retained. | Safe recovery/state-reporting case needing targeted integration coverage |
| Local disk failure after durable receive but before receipt creation | New native fault test retains row and retries receipt without duplicate import. | Tested safe boundary, not absolute power-loss proof |
| History import and live mailbox refer to same logical message | Existing immutable-ID/provenance/dedupe checks apply. | Preserve; cross-platform simultaneous physical run still pending |
| Both devices authenticated but only one online | Existing pre-prepared copies remain independently fetchable. Later/new targets are not guaranteed after global Delivered. | Confirmed policy boundary B6 |

The reviewed ownership, account/pin checks, authenticated signatures, HPKE key possession and server authorization did not justify automatic trust, fingerprint replacement or cross-account redirection. No confirmed account hijack or server decryption defect was found. This is not a formal cryptographic proof or live deployment penetration test.

## D Recommended delivery architecture

| Model | Reliability and consistency | Battery and cost | Security and complexity |
| --- | --- | --- | --- |
| Independent verified devices | Each eligible device can recover even when another ACKs; consistent conversation coverage | More encrypted copies; coalesced hints and bounded fetches keep wake cost bounded | Explicit target set and per-target state; no automatic trust; recommended |
| Windows first with Android fallback | Android intentionally misses messages Windows ACKs; needs later history recovery | Fewer phone wakes | Requires ambiguous timeout/priority rules; stale Windows presence can stall phone |
| Active preference with fallback | Good latency, but fallback alone still does not give independent history | Can favor active live transport | Useful transport preference only; do not make it delivery exclusion |
| One primary plus History Sync | Simple immediate destination; other devices depend on source availability and history authorization | Lowest routine fanout | Poor independent availability; couples messaging to history recovery |

Use a durable target manifest frozen from currently eligible **already verified** devices when sending. Track sealed, server-stored, recipient-committed, retry time and terminal/actionable state independently for each target. Preserve a logical message ID for conversation dedupe; give envelope attempts their own key generation and digest identity. A first ACK may update “Delivered to one device,” but must not delete unfinished required targets.

Do not add future/newly enrolled devices silently to old live-message targets. Authorized history access decides catch-up. Revocation cancels pending authorization, not retained user history. Retention expiry must be visible as an undelivered target, not silently relabeled Delivered. Distinguish account aggregation from per-device detail in diagnostics/UI.

Sent-message mirroring needs a separately reviewed same-account authorization contract and origin/direction semantics, using existing verified keys and enrollment proofs. The current inter-account mailbox RPC cannot simply be reused without scope changes. This is the main approval boundary after narrow repairs.

### Android background policy

Keep FCM opaque, idempotent and best effort. Reconcile on foreground, network recovery, token refresh and bounded wake. Publish committed receives independently of unrelated outgoing/receipt failures. Consider one coalesced, network-constrained, persisted reconciliation job for opted-in profiles and reboot recovery, rather than a second backend owner, permanent foreground service or continuous polling. Reuse JobScheduler unless a concrete requirement warrants WorkManager.

Normal backgrounding, OS process death, force-stop and reboot need different acceptance cases. Doze restricts normal background network; high-priority FCM provides only a limited processing opportunity, and Android does not guarantee timely execution in every condition. User force-stop must not be advertised as ordinary background delivery. Notification permission affects visibility, not cryptographic receipt. See [Android Doze guidance](https://developer.android.com/training/monitoring-device-state/doze-standby), [FCM Android priority](https://firebase.google.com/docs/cloud-messaging/android-message-priority), and [Android 15 stopped-state behavior](https://developer.android.com/about/versions/15/behavior-changes-all).

## E Concrete repair plan

1. **Isolate outgoing failures and schedule fairly.** Modify existing Windows/Kotlin MailboxExecution and native pending selection, not a replacement queue engine. Preserve safe server refusal codes, attach bounded per-target retry/actionable state, and rotate through eligible candidates. Never repeatedly let four permanent rejects starve unrelated messages. Keep authentication failures distinct from transient item errors.
2. **Recover stale and expired unaccepted envelopes safely.** Extend existing DeliveryManager/storage reconciliation. Preserve immutable accepted ciphertext/digest and old receipt verification. Establish whether the server accepted an old attempt before changing attempt identity. Never reseal into an existing conflicting server row or discard plaintext/outbox. If current RPC cannot establish safe status after an ambiguous store, design a minimal versioned attempt/status extension for approval first.
3. **Publish durable Android progress independently.** Change MessagingBackend's phase/result handling so committed incoming rows refresh snapshots and notifications even when later receipt/outgoing work fails. Keep the ACK after durable import/receipt; do not swallow session supersession. Test the real wake job's drain/baseline behavior as well as MailboxExecution.
4. **Bound live message ACK waits.** Add bounded per-submission application deadlines to the existing delivery owner. On timeout, re-evaluate/fallback safely while retaining dedupe and one transport owner. Distinguish a healthy connection from application progress. Reuse event-driven wakeups and current reconnect budgets.
5. **Use a complete receipt cursor.** Add stable composite pagination across all receipt identity fields, or an explicit stable sequence with a versioned RPC capability. Update Windows and Android readers while retaining old-client handling. Do not drop sibling receipts, trust a cursor supplied for another account, or order on formatted timestamps.
6. **Prevent blocked fetch pages.** Design bounded authenticated pagination/reconciliation for deferred rows; retain invalid ciphertext for authorized diagnosis/expiry, never ACK it as delivered or delete it to clear the queue. Keep per-device quotas and avoid unbounded scanning.
7. **Implement independent targets only after approval.** Existing per-device HPKE and server storage are reusable; most work is sender ownership, aggregation and compatibility. Sent mirrors additionally require a same-account contract. No key export, auto-enrollment or weakened revocation.
8. **Close wake/health visibility gaps.** Separate authenticated presence, live peer readiness, mailbox registration/last success, pending validation and notification permission. Add bounded reconciliation if approved; do not redefine Online as guaranteed message receipt.

## F Priority and risk

| Priority | Repair | Risk today | Change scope and benefit |
| --- | --- | --- | --- |
| P1 | B1/B2/B3 outgoing poison and fairness | Indefinite queue delay; no actionable state | Medium; shared native plus thin clients; protects all later messages |
| P1 | B4 Android publication boundary | Committed message can miss timely UI/notification | Small to medium; Android only, full wake regression required |
| P1 | Connected-but-unacknowledged deadline | Conditional indefinite delay | Medium; preserve route/ACK ownership and bounded retries |
| P2 | B5 composite receipt pagination | Per-device completion evidence can remain unreachable | Small server query plus coordinated client compatibility |
| P2 | Explicit independent target ownership | Confirmed missing-copy policy gap | Medium to high; approval and additive durable state required |
| P2 | Fetch-page fairness and wake reconciliation | Conditional backlog/delayed recovery | Medium; retain all security refusals and OS limits |
| Separate review | Same-account sent mirrors | Feature absent, not an existing malformed-packet bug | Account authorization/protocol scope decision |

No measured physical likelihood percentage or latency improvement is assigned. P1 means reproducible or strongly supported blocking behavior with a bounded corrective path, not a claim that every physical failure shares one cause.

## G Migration and compatibility

Narrow fairness/publication fixes can be released independently without changing encryption. Delivery deadlines change retry timing and require concurrent live/mailbox duplicate tests, but must not change wire contents. Receipt pagination and any attempt-status extension need explicit compatibility negotiation or a backward-compatible parallel RPC; old clients must continue operating without accepting unverified receipts.

Keep old outboxes, protected envelopes, keys and receipts readable. Add supplemental versioned target/attempt state using existing protected storage mechanisms rather than rewriting history. Migrate only authenticated relationships; classify unknown targets as unresolved, not trusted. Crash between committing message state and marking a server upload is already retry-safe and must remain so.

Current limits are finite: maximum envelope TTL seven days, fetch/receipt page 64, outgoing and accepted incoming work generally four per pass, recipient device bindings up to 32, server per-device pending payload quota 256 rows/8 MiB and global 4096 rows/64 MiB. A sender permanently offline after Store does not prevent receipt, but expiry/revocation can intentionally end availability. Do not promise indefinite delivery under finite retention.

Server device ACK converts only its matching ciphertext row into a retained signed receipt. Receipt expiry cleanup can remove completion evidence before a long-offline sender returns. A future all-target cleanup rule must explicitly cover accepted-but-unreconciled state; do not delete the sender's remaining targets merely because one device is done. Existing completed user history remains untouched.

### Performance boundaries

The application makes multiple authenticated sequential RPCs per pass: registration when needed, fetch, ACKs, receipts, recipient keys, stores. Rejected first batches can cause repeated work without progress. Foreground Android reconciliation uses long fallback intervals when no urgent work exists (15 minutes with event connectivity, two minutes without), while immediate continuation/retry and UI leases affect actual latency. Server FCM hint suppression/coalescing, 30-second wake budgets and bounded batches matter more than a generic Online label.

The new 500-message test measures local durability only. No real LAN delivery latency, background wake-to-notification latency, battery cost or Pi throughput was benchmarked. Collect monotonic stage times and queue age around existing production boundaries during the approved repair/physical pass before changing batching or storage commit policies. Never improve throughput by ACKing before durable persistence.

## H Validation and acceptance

### Tests executed in this audit

| Test | Result and boundary |
| --- | --- |
| `tests/test-messaging-durability-audit.ps1` | PASS expected observations: real Windows CNG identity, shipping HPKE, protected SQLite; pre-commit rollback, post-commit receipt failure/retry, lost ACK/reopen/dedupe, Stored-marker interruption, missing sibling after first ACK, stale-key reuse, expired pending envelope, 500 local queued messages/two conversations/reopen |
| `tests/test-messaging-receipt-pagination-audit.ps1` | PASS counterexample: production DB query with 65 synthetic per-device receipt rows; sibling receipt unreachable at page boundary. No signature validation claim |
| Android `Mailbox*` focused suite | PASS 21 host tests: audit faults 2, consent guard 2, isolation 12, re-enrollment 5. Two audit observations cover ACK then later service failure and first-four starvation. Robolectric/fake native+RPC, not JNI/phone proof |
| `tests/test-mailbox-protocol.ps1` | PASS native/managed canonical wire; signed bindings, HPKE, receipt verification, authorization/tamper rejection; real local pinned TLS offline store/fetch/ACK after sender logout; quotas, expiry, rotation, retired exact retry, revocation, device removal, DB reopen |
| `tests/test-reliable-endpoint.ps1` | PASS 102 real Schannel/shared-runtime host lifecycles, durable text/ACK/dedupe, winner/logout gates, bidirectional TransferManager payload nonregression |

The expected-observation tests deliberately assert current defects; they are not acceptance tests asserting those defects were fixed. Convert each to the desired invariant during its repair checkpoint. No production fix or new application package is claimed by this audit.

Evidence retained under `build/messaging-audit-durability.log`, `build/messaging-audit-mailbox.log`, `build/messaging-audit-endpoint.log`, Android JUnit XML, unique `build/tests/messaging-durability-audit-*` and `build/tests/mailbox-protocol-*` directories. Server pagination evidence is an isolated `%TEMP%/audit-server-receipt-pagination-*` database. Test timestamps can cross UTC midnight relative to the local audit date.

Android reproduction command: set `JAVA_HOME` to `C:\Program Files\Java\jdk-22` and `ANDROID_HOME` to `C:\Users\Livel\AppData\Local\Android\Sdk`, then run `gradlew.bat --offline --project-cache-dir build/audit-android-gradle-cache :app:testDebugUnitTest --tests 'com.peerchat.android.application.Mailbox*'` from `android`. The first broader rerun encountered the installed Studio JDK 25 incompatibility and then an unset SDK path; setting these process-local variables resolved both without modifying the repository or SDK configuration. Final Gradle result: BUILD SUCCESSFUL, all 21 selected tests passed.

### Requested fault matrix and remaining proof

| Cases | Existing proof or audit result | Remaining acceptance |
| --- | --- | --- |
| 1–4 Windows/Android live and offline directions | Shared endpoint/native mailbox and local TLS server exercised; Android host service verified | Real JNI/Keystore and packaged bidirectional phone paths |
| 5–6 same account devices, Windows online/phone background | Historical independent ciphertext evidence; B6 proves missing unprepared copy | Independent-target policy after approval, locked Motorola |
| 7 sender disconnect after Send | Durable queue and endpoint replay; local server fetch succeeds after sender logout | Physical disconnect before store, during ambiguous store and after store |
| 8–9 phone wakes before Windows timeout/simultaneous reconnect | Live-first suppression and device-scoped leases traced | Deterministic combined platform lifecycle fixture and physical repeated transitions |
| 10–13 duplicates, lost ACK, post-commit/pre-commit crashes | New real native fault tests plus existing signed mailbox replay and protected DB reopen | Abrupt process kill at instrumentation boundaries; not actual power failure tested |
| 14 Pi restart | Existing fixture reopens DB and reconciles receipts | Actual server process restart with undelivered messages, only with approval; no live action now |
| 15–17 background/process death/force-stop/delayed or repeated wake | FCM/JobScheduler ownership traced; post-ACK publication counterexample | Android OS/notification/Doze/reboot acceptance and deterministic job tests |
| 18–19 burst and concurrent conversations | 500 local committed rows over two conversations reopen intact | End-to-end draining with quotas; thousands require bounded replenishment, not exceeding quotas and claiming success |
| 20 identity/key changes/revocation | Signed server/native tamper/key/retired/revocation tests; rotation stall reproduced | In-flight revocation across real platforms |
| 21 simultaneous History Sync/new messages | Immutable attribution/dedupe mechanisms reviewed; feature unchanged | Real overlapping import/live mailbox, authorized provenance only |
| 22 restart recovery | Protected storage, exact receipt replay, saved envelope retry and DB reopen pass | Both packaged apps repeatedly restarted |
| 23 sender permanently offline after Store | Local real TLS fixture fetches/ACKs after sender logout | Physical sender shutdown after confirmed Store |
| 24 cleanup | Existing expiry/revocation and signed receipt cleanup tests pass | Per-target completion/retention specification before new policy implementation |

### Physical procedure after approved repairs

Use two Windows PCs, Motorola and existing Pi, with no profile reset or permission changes. Record build/account and redacted device identity suffixes before testing. Use unique synthetic test IDs; do not log message text, tokens, private keys or HPKE secrets.

1. Keep distinct devices of one receiving account authenticated. Send a short tagged message from the other account. Confirm durable import and per-device ACK on each receiver, not merely one Delivered badge.
2. Repeat Windows → Motorola and Motorola → Windows with the other receiver closed. Confirm ordinary inbox/history after restart and no duplicate rows.
3. Send while the phone is normally backgrounded/locked, then while its process is OS-killed. Check wake, commit and notification separately. Test explicit force-stop as its own OS-restricted case, then reopen and reconcile. Test reboot and token refresh separately.
4. Disconnect sender before store, during uncertain store response, and after confirmed Store. Reconnect/restart; verify identical retry/dedupe, accurate state and recipient delivery when sender stays offline.
5. Drop a delivery ACK after receiver commit; restart the receiver and retry. Confirm one durable message and matching signed receipt, not duplicate notification/history.
6. Test four permanently rejected outgoing candidates followed by a valid fifth. Test expired and rotated-key envelopes. Require fair progress and actionable old-item states without disabling authorization checks.
7. Exercise more than 64 per-device receipts, including a page boundary with two receipts for one message. Require every authorized receipt reachable. Exercise an unprocessable fetched row without ACKing/deleting it as a shortcut.
8. Run bounded bursts across two conversations while one device reconnects and History Sync performs authorized imports. Check counts, stable IDs, ordering and duplicate suppression. Do not exceed finite mailbox quotas without testing their explicit refusal path.
9. Only after approval for controlled service interruption, restart the Pi with pending ciphertext and verify recovery. Live Pi inspection/deployment is outside this audit.
10. Confirm accepted group attachments, emojis, clipboard, transfer pipeline, voice, screen sharing and current Group History Sync remain unchanged before accepting any later release.

## Checkpoint and next action

Audit changes are restricted to `MailboxAuditFaultTest.kt`, the native durability fixture and runner, the server receipt-pagination fixture and runner, and this Markdown report. Test checkpoint: `5704589`. Existing dev.162/dev.163 packages and production source remain the rollback/physical baseline. No new Windows/APK build is necessary for a test-only audit.

Dev.163 artifacts were rehashed unchanged: Windows ZIP SHA-256 `b00872a2576044f7bb4a392ec4d6af1965c2ab0e0d1f9bc078c476c135623ef4`; Android APK SHA-256 `f4b269ef3861dd1f371b532384afb23931eef349000479e4996582867989fd41`. Independent security review corrected the event-lease versus token distinction and retired-key exact-retry wording before checkpointing this report.

Approve the narrow repairs in E1–E6 first, with separate logical checkpoints and a packaged physical test candidate. Approve independent-target semantics and same-account sent mirrors separately before E7. Group History Sync and Android Voice must remain paused until that decision. No deployment, integration rewrite or feature continuation is authorized by completion of this audit.
