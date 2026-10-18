# Device Manager implementation — dev.179

## Scope and baseline

Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`; branch `codex/native-unicode-emoji`. Starting checkpoint `c90e4bf`, preserving the dev.178 delivery projection and attachment-choice fixes (`fc32687`, `f45ab05`) and their immutable packages. The user approved the scoped authorization/revocation policy after the [authority audit](2026-10-17-device-manager-authorization.md), then explicitly authorized Pi deployment. No real device is revoked as a test. No Android installation is authorized by this milestone.

This implements own-account management, not account linking. Jonah account 3 and JonahPhone account 4 remain separate. Contact identities, local trust pins, History Sync grants and device ownership are not interchangeable.

## Authority and implementation

### Server

`server/PeerChat.Server.cs` adds device authority, account revision, authenticated self-reported platform/version metadata and accepted management-operation outcomes. Existing `devices` ownership remains historical provenance. A separate durable tombstone supplies current authorization; no ownership row, ciphertext queue, key, message or signed historical roster is deleted by full-device revocation.

`ActiveDevice` is used by current authentication, device-bound sessions, relay admission, mailbox registration/store/fetch/ACK, recipient eligibility, group authorization and archive authorization. Friend snapshots retain historical known identities and carry a separate revoked-identity list. An offline active device is not revoked. Offline/default routing must not select a revoked historical identity.

New bounded, authenticated operations are `device_inventory`, `device_metadata`, `device_rename`, `device_revoke_challenge` and `device_revoke`. Inventory is restricted to an active, proof-bound device session of the same account and capped at 64 entries; exceeding the cap is an explicit error, not silent truncation. Platform/version are authenticated self-reports, not hardware attestation. Last verified activity is not retained and is shown as unavailable. Fresh online status uses a current authenticated live lease, not an old presence row.

Names use trimmed NFC Unicode, 1–64 UTF-16 code units and at most 256 UTF-8 bytes, rejecting controls, invisible formatting and invalid surrogates. Names are stored as encoded UTF-8 in the new metadata table to avoid the legacy Windows SQLite ANSI text boundary; they are plain Unicode on the authenticated wire. Rename is compare-and-set against the account authority revision. Identical self-reported platform/version publication does not write repeatedly. Names never establish trust.

### Scoped revocation proof

Revocation requires the manager's active device-bound session, fresh account-password step-up, exact account/actor/target/revision binding, pinned server fingerprint, process epoch, short expiry, nonce and hash of the current session token. The existing identity key signs only this separately typed purpose:

`PeerChat device management v1|revoke|account|actor|target|revision|serverFingerprint|serverEpoch|issuedUtc|expiresUtc|nonce|sessionBinding`

`windows_mailbox.hpp`/`node.cpp` add scoped operation 13, bounded to 2 KiB, with exact canonical reconstruction, local/current account binding, actor-key match, at most 60-second freshness and bounded replay rejection. It does not expose a general-purpose signer, export a private key or change enrollment/resume operation 9. The managed adapter verifies authenticated origin and all expected fields before native signing. The server independently verifies the original signature and rechecks authority inside its transaction.

The server durably records an accepted operation digest/revision. An exact accepted retry can reconcile its result; altered payloads, wrong sessions/accounts, stale revisions and replayed challenges cannot authorize another mutation. Step-up/challenge creation is bounded and throttled before expensive password work.

Self-revocation, last-device revocation and reactivation are deliberately unavailable. Automatic password login/enrollment cannot erase a full-device tombstone. Account recovery/reactivation requires a separate reviewed design.

Revocation invalidates the target's bound sessions/remembered credentials and affected live routes. It also invalidates all legacy unbound sessions of the affected account because their physical owner cannot be determined safely. Other active devices may therefore need to reconnect/sign in again; their keys and data remain intact. No cross-account session invalidation is intended.

### Historical data and encrypted attachments

`server/EncryptedMailbox.cs` excludes revoked devices from new eligibility and rejects their current operations, while frozen existing targets and encrypted queued copies remain under their original identities and normal retention policy. Revocation is not an ACK and does not make an incomplete account-wide message Delivered. Existing signed receipts remain valid evidence.

`server/ArchiveAuthorization.cs` separates retained descriptors/grants from current eligibility. `AttachmentCacheKeys.cs` and group admission enforce active requesters/recipient selection without rewriting signed metadata or HPKE envelopes.

An isolated production-cache test reproduced a subtle over-denial: applying active-device checks to an old uploader proof blocked another active original recipient from an already-completed object. `AttachmentCacheDirect.cs` now distinguishes historical uploader provenance from current request authority, only for a stored complete object requested by its active original recipient. The exact retained signed source binding, account/device/key/generation, original audience, relationship, friendship, integrity, expiry and current requester checks remain. A revoked key still denies access. There is no exception for new admission, uploading or the revoked uploader requesting its own object. Ciphertext is not rewritten or redirected.

### Windows and Android

`desktop/DeviceManager.cs` adds Settings > Device Manager, account-scoped cards, full selectable fingerprints, naming, explicit device verification and password-confirmed revocation. It reuses `AttachmentDeviceSession` encrypted profile/device-bound credentials and existing private identity keys. There is no automatic enrollment solely from opening Settings. Network waits run off the UI thread on a separate management client, with profile/session/generation fencing. Refresh runs on authenticated events/reconnect and at a bounded 60-second interval independently of Settings visibility. Unknown fields remain unavailable.

`desktop/CoreAdapter.cs`, `FriendConversations.cs`, `node.hpp`, `ui_bridge.cpp` and `reliable_peer_protocol.hpp` keep revoked IDs separate from historical identity mapping. Updated native peers close an existing authenticated route when a verified revocation snapshot arrives and reject subsequent send/demand. Stale lower-generation snapshots and legacy-shaped refreshes cannot erase the in-memory deny set in that account lifetime.

Android `OwnDeviceInventory.kt`, `MessagingBackend.kt` and `ServerAccounts.kt` consume bounded account metadata independently of shared network locks. Names appear only in existing History Settings rows, not a new Android Device Manager. Epoch/account/session/revision fences reject late replies; revocation invalidates runtime authorization without clearing keys/history/queued messages. The optional local JNI friend-snapshot extension carries revoked IDs; malformed input is rejected before mutating the native snapshot. `portable/peerchat_core.cpp` closes revoked-peer connections using existing endpoint ownership.

## Compatibility and limitations

- New server endpoints and additive non-destructive tables require dev.179 server for management. Existing message, transfer v2, group ciphertext, HPKE and attachment descriptor wire formats are unchanged.
- The scoped Windows native operation and Android local JNI extension ship with their matching clients; mixed DLL/APK components are not supported.
- Older clients retain ordinary compatible operations but cannot provide complete immediate local revocation enforcement or device-name UI. Updated server admission remains fail-closed. Update all devices before relying on prompt cross-device enforcement.
- Revocation cannot erase already decrypted data, distributed keys or packets already in flight. Previously issued TURN credentials can remain usable until their existing expiry. This milestone does not rotate group epochs, rewrite media sessions or claim remote wipe.
- No new device recovery/reactivation flow, cross-account linking or Android management controls are implemented.
- The legacy `session_info.publicIdentity` retains its historical-primary semantics; actual friend routing selects an active device. The old direct-probe consent operation can still produce a harmless hint naming a revoked historical device, but current session/native/relay authorization denies establishing its transport. This hint cleanup is not a bypass or a new route and remains a small follow-up. Inventory does not yet expose a separate mailbox-capability field; Windows explicitly displays that measurement as unavailable.
- Metadata refresh is one bounded request at a time, off the shared transfer/message lane. No high-frequency statistics writes or new polling thread are introduced. No physical latency/battery improvement is claimed; password verification intentionally adds management latency, and legacy-session invalidation may interrupt the affected account briefly.
- A rollback to dev.177 after any real full-device revocation would ignore tombstones. `scripts/pi-compatible-update.py` now refuses automatic legacy rollback when the current database contains revocation authority. Preserve the database and use a reviewed revocation-aware binary; never restore an old database over newer messages to work around this safety gate.

## Validation record

All development fixtures use isolated identities, databases and ciphertext. They do not establish physical acceptance.

- Native scoped proof and reliable peer regressions passed: canonical signatures, account/device/purpose/freshness/replay rejection, deny-set monotonicity and historical ownership retention.
- Real managed Windows adapter to production local TLS server passed account isolation, enrollment/resume with protected credentials, rename/CAS, bad password, revocation, legacy/bound session invalidation, re-enrollment rejection and server restart.
- Android focused suites and production JNI/TLS/storage fixture passed malformed snapshot rejection, existing-route closure, denied future operations, preserved history, repeated reopen and lost-ACK recovery.
- Production-cache regression reproduced and then passed the retained-uploader proof correction; storage reserve was enforced unchanged. C: lacked the required reserve, so a fresh isolated E: fixture was used rather than weakening policy.
- Pi updater unit tests passed, including refusal to roll back across a durable revocation without modifying the database.
- Final full builds, checkpoint and deployment are complete as recorded below. Physical acceptance remains pending.

### Final validation and checkpoints

| Validation | Result / evidence |
| --- | --- |
| Scoped native proofs | PASS, `build/device-manager-native-proof.log`; purpose/account/device/freshness/replay and native ABI rejection |
| Shared reliable protocol | PASS, `build/device-manager-reliable-protocol.log`; 1,000 identity/Hello/Accepted lifecycles, Unicode/history/deduplication/scoped ACK |
| Full Android | PASS, 389 tests in 86 suites, zero failures/errors/skips; lint and ARM64 debug APK; `build/device-manager-android-validation.log` |
| Android production JNI host | PASS, `build/tests/reliable-profile-jni-66497744461d4fc09a9877cfe0644325`; actual TLS/storage, malformed snapshot, revoke/live-route closure, eight reopens and lost ACK recovery; not a physical phone test |
| WPF actual packaged assembly | PASS, 44 assertions; `build/dev179-device-manager-ui.log`; sample-only renders at 100/150/200% in `build/tests/device-manager-ui-0851784bafb24ee9afb56a14f997b6aa` |
| Windows management adapter + actual local TLS server | PASS, 53 assertions; `build/device-manager-adapter-current.log`; profile-protected credentials, CAS names, self metadata no-op, password/proof, revoke and restart |
| Existing attachment device adapter with dev.179 native DLL | PASS, `build/dev179-attachment-session.log`; native enrollment/cache operation, unchanged main session, protected credential restart, revocation denial |
| Delivery status nonregression | PASS, `build/dev179-delivery-projection.log`; per-device projection, legacy history and XAML |
| Group history | PASS, `build/device-manager-group-history.log`; 74 nonempty imports, pages/restart/replay/deduplication, historical authorization, tampering and revoked grants |
| Eligibility snapshot | PASS, `build/device-manager-eligibility.log`; immutable original targets, revocation, scope, restart/expiry and overflow rejection |
| Server portability | PASS, `build/device-manager-server-portability.log`; Framework/modern canonical JSON and signatures, authentication/presence, groups/media rooms, encrypted mailbox durability and new management authority |
| Device management/cache isolation | PASS, `build/tests/device-management-cache-93b84d8d278b4d5886ff56a3fc0907f3`; exact historical source binding, active original recipient, forged/revoked key, revoked recipient, group admission and retained ciphertext |
| Full encrypted-cache runtime | PASS, `build/device-manager-cache-runtime.log`; quota/reserve/expiry, 16 concurrent admissions, original recipients, restart, actual process-kill upload/cleanup recovery |
| Group voice and screen sessions | PASS, `build/device-manager-group-voice.log` and `build/device-manager-group-screen.log`; authorization, retired callbacks/generations, bounded routes and topology; no physical media claim |
| Windows full build | PASS, `build/device-manager-windows-build.log`; native DLL export/load and package-only dependency checks; existing compiler warnings remain |
| Linux packages | PASS, `build/device-manager-linux-build.log`; x64/ARM64/ARM32, pinned .NET SDK 10.0.401/runtime 10.0.12 and manifest allowlists |
| ARM32 isolated management test on Pi | PASS, `build/dev179-pi-isolated-security-final.log`; same production dispatcher/database/crypto with disposable accounts, queued ciphertext retained; live server untouched during this test |
| Deployment helper | PASS, six isolated unit tests including revocation-aware rollback refusal |
| Final package audit | PASS, `build/dev179-package-audit.log`; exact source/version, original Android signing identity, ABI, dependencies and all ZIP payload bytes |

Validation setup failures were corrected without changing production security: the first full Android invocation lacked `ANDROID_HOME`, and the first ARM32 fixture rejected the disposable uploaded PFX's permissions. The SDK path was supplied and that test certificate alone was restricted to mode 0600, followed by fresh isolated ARM32 state. The original failure logs are retained. No real key/certificate permission or cache reserve was weakened.

Logical checkpoints:

- `5c53170` — server authority/authorization, cache preservation tests and rollback protection.
- `878ae4a007dea572b4cc1a58cbdc6283d295b546` — Windows/Android/native integration and dev.179 source used for all packages.
- The subsequent documentation checkpoint contains this report/index/backlog; it does not change packaged production source. Working tree is clean at handoff. Generated compiler objects were retained under the dev.179 build directory rather than discarded.

## Package handoff

All paths below are relative to the worktree listed above. Existing dev.178 and older artifacts were not overwritten.

| Package | Path | SHA-256 |
| --- | --- | --- |
| Windows | `build/development/4.0.2-dev.179/PeerChat-Windows-4.0.2-dev.179.zip` | `c094e6590b55c82b68fbf00d38f651caed7b2875aac989b680508abffc10f9cf` |
| Android | `build/development/4.0.2-dev.179/Android/PeerChat-Android-4.0.2-dev.179-debug.apk` | `30d9675d267f83fe655db2099b45566e574c571886de6d77fb2ed64d0b177a2b` |
| Pi ARM32 | `build/linux/4.0.2-dev.179/PeerChat.Server-4.0.2-dev.179-linux-arm.zip` | `3730e300cc7a9e222f4bfb45a41185de0ad3e7b12fabaff9e0e46d2175f843f9` |

The existing Android signing certificate is unchanged. Update with `adb install -r` only when the user chooses to install; never uninstall or clear data. No phone installation was performed here. The immutable client manifest records packaging-time deployment/installation state; subsequent Pi deployment is documented separately, not by rewriting the archive.

## Approved Pi deployment, 2026-10-18 UTC

Readiness: ARMv7, existing 117 GiB ext4 filesystem, approximately 103 GiB free, approximately 3.3 GiB available RAM. No assumed SD capacity or storage policy change. ARM32 isolated management/security tests passed before switching the live executable.

Deployed from dev.177 to dev.179 using `scripts/pi-compatible-update.py`. Executable: `/opt/peerchat-test/server/4.0.2-dev.179/PeerChat.Server`; verified running PID 28595 at handoff. Staged package/helper: `/opt/peerchat-test/incoming/dev179-deployment/`.

Backup: `/opt/peerchat-test/rollback/pre-4.0.2-dev.179-20261018T011113Z`. It contains the prior binary/configuration, protected configuration references, coherent online/stopped databases and encrypted cache backup. The cache backup held 106,528 bytes in three files, with matching hashes and SQLite integrity `ok`. Original account/device/grant/remembered-credential/group tables remained byte-content equivalent under the helper's protected-column snapshot. Certificate/configuration remained unchanged. Postflight database quick check was `ok`; full-device revocation count was **zero**. No real devices were renamed/revoked by testing or deployment.

Pinned TLS/authenticated discovery startup health passed with zero retries in 11,005 ms. Three later complete login/discovery/logout diagnostic sequences took 1,349, 1,280 and 1,332 ms. These measure composite health calls, not message delivery, pure TLS latency or a before/after performance improvement. Cache remains enabled with verified filesystem binding, unchanged settings and satisfied reserve. Evidence: `build/dev179-pi-deployment.log` and `build/dev179-pi-postflight.log`.

Rollback, if subsequently authorized: preserve the **current** database/cache and inspect durable revocation state first. Before any revocation, the prior dev.177 binary can be restored with the existing checked runtime helper and pinned health gate. Once even one full-device tombstone exists, do **not** start dev.177 or restore a pre-revocation database: use a validated revocation-aware binary. The deployment helper now blocks unsafe automatic legacy rollback. Never overwrite newer messages with backup data merely to roll back binaries.

Development stops here for physical acceptance. No additional roadmap milestone, account migration, key rotation, phone installation or real-device revocation is included.

## Physical acceptance checklist

1. Update Windows and Android using the same existing profiles/signing identity; do not clear app data. Open Windows Settings > Device Manager and explicitly verify the current device if requested.
2. Check account isolation, current-device label, fingerprints, platform/version availability and active/offline/revoked distinctions. Unreported legacy metadata must not be guessed.
3. Rename a device; confirm another same-account client/Android History Settings receives the label after reconnect/restart. A different account must not gain access to this inventory.
4. Only if intentionally desired, revoke a disposable/obsolete device after checking its full fingerprint. Confirm password step-up and warning; never use a valuable device merely to test. Expect legacy same-account sessions to reconnect. Self/last-device and reactivation remain unavailable.
5. Send messages in both directions and inspect truthful per-device receipts. Test direct and cached attachments, existing completed downloads, restart/resume and History Sync. No prior history or queues should disappear and no incomplete target should become falsely Delivered.
6. Verify accepted emojis, clipboard attachments, voice/screen sharing and normal settings behavior. Report physical issues before further rollout.
