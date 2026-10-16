# Independent device delivery — dev.166

## Scope and checkpoint

Branch: `codex/native-unicode-emoji`, existing `native-unicode-emoji/Create 2D Game/peerchat` worktree. Starting checkpoint `36fef65` (accepted dev.165 retained). Production candidate source: `a91c34f`. No Android installation, account reset, historical migration, or group delivery change.

Checkpoints: `355af2f` authenticated eligibility snapshot; `4d8c3e4` relay authority revalidation; `27c8496` desktop snapshot prefetch; `38e6e9d` archive-first receipt regression; `8cf3a21` immutable target manifests and exact ACKs; `0c0774f` authenticated two-device application fixture; `a91c34f` Windows/Android integration and partial-delivery projection.

## Implementation and shared interface

The server issues short-lived account-bound eligibility snapshots over the existing authenticated mailbox TLS channel. A snapshot identifies current device candidates; it is NOT authority to trust a new identity. Native delivery freezes only locally verified, correctly mapped candidates (checking signed HPKE bindings where present) when committing the protected outbox row. No Send-time network call is added. Snapshot freshness is bounded by UTC and monotonic elapsed time; account/friend-generation changes invalidate the cache. Revalidation cannot expand an existing message's target set.

`DeliveryTargetManifest` contains stable logical message identity, sender/recipient scope, snapshot identity/sequence, canonical recipient, and immutable device/key-generation members. `DeliveryTargetState` and `DeliveryTargetStatus` project per-device Pending/Acknowledged/Unavailable state. Private Storage mutations are owned by DeliveryManager. `installEligibility`, `invalidateEligibility`, `queueMessageWithCachedSnapshot`, `eligibleForMailbox`, and `noteMailboxStoreFailure` are the shared boundary. Platform pumps schedule work; they do not own ACK authority or create a second delivery engine.

Future group reuse: the device-target state machine, exact encrypted-envelope receipt correlation, retry scheduling and read-only projections are reusable. Group membership/epoch authorization would require a separately typed scope adapter and approved eligibility source. A DM account snapshot MUST NOT authorize group delivery. This milestone does not modify group routing, ciphertext fanout, PCGE records, attachment envelopes or membership checks. Do not reuse DM recipient resolution as group authorization.

Live ACKs require the exact authenticated submitted connection/message. Mailbox ACKs require the retained immutable envelope, digest, signature and matching account/device. A single ACK never settles siblings. Partial ACKs produce Sent/Stored, not Delivered; both native event payload and WPF projection preserve that distinction. Retry deadlines are bounded and per device, so a retired recipient cannot starve another device or later queued messages.

## Security and compatibility boundaries

- New RPCs: `mailbox_eligibility_snapshot` and `mailbox_eligibility_validate`, additive server snapshot tables. Old clients remain compatible; new independent fanout requires the updated server.
- Native bridge mailbox operations 11/12 and additive Android JNI eligibility/store-failure entry points. No encrypted message, HPKE, TLS, identity, or legacy message-history format change.
- Protected supplemental target/retry records and an identifier-only outbox index preserve legacy records. These are authoritative delivery bookkeeping, not statistics.
- Without a fresh verified snapshot at queue commit, delivery remains canonical-only **unresolved**. Later snapshots never silently add siblings. Such a message cannot claim all-device Delivered. This is an explicit limitation: physical acceptance must not assume every offline/unprimed send reaches all devices.
- Device key-generation changes do not rewrite ciphertext. Unavailable targets do not silently reactivate. Public generic acknowledge cannot settle manifested messages.
- Server rechecks current friend relay session/ownership/relationship/block authority after pairing and before forwarding. A narrow authorization-check/write race remains; no claim of instantaneous atomic revocation.
- Remembered-session revocation and device unenrollment remain distinct existing operations. No automatic registration/trust or new sent-message mirroring protocol.

## History Sync compatibility

Archive-first records could retain the original recipient and Sent attribution, causing a later genuine mailbox copy's receipt to fail the old local-recipient/Delivered check. The new private authenticated receipt path accepts that alias only after real mailbox authentication/decryption plus exact content, sender, time, ordering, account and protected verified-archive provenance checks. Public receipt validation stays strict.

Archive imports alone never generate live ACKs or settle target state. Logical IDs and existing deduplication/cursors are retained. Historical unresolved ownership remains fail-closed; the prior 236 unresolved rows are neither reassigned, deleted nor silently skipped. Authorized older-history recovery is still History Sync, not routine live receipt fabrication.

## Modified shared and platform components

Server: `EncryptedMailbox.cs`, `EncryptedMailboxChannel.cs`, `PeerChat.Server.cs`. Native: `delivery_manager.hpp`, `storage.hpp/.cpp`, `reliable_peer_protocol.hpp`, `node.cpp`, `windows_mailbox.hpp`, `portable/reliable_endpoint.hpp`, `portable/peerchat_core.cpp`. Desktop: `App.cs`, `DiscoveryClient.cs`, `MailboxExecution.cs`, `ChatViewModel.cs`. Android: `MailboxBridge.kt`, `ServerAccounts.kt`, `EligibilitySnapshotExecution.kt`, `MailboxExecution.kt`, `MessagingBackend.kt`. Version: `shared/VersionInfo.cs`.

The Android groups/voice integration plan and existing worktree checkpoint were inspected before shared edits. Group/voice interface ownership is unchanged; no edits to that worktree. Existing native Android queueLocal is integrated; the deprecated ReliableBridge.send host-proof path deliberately retains legacy semantics and does not prove new fanout.

## Validation and measurement boundaries

Passed: isolated server snapshot signature/scope/revocation/expiry/restart/32-device bound tests; real pinned-TLS paired relay revocation cases; protected native durability/target/retry tests; WPF partial/full ACK projection and fair per-device scheduling; actual authenticated application-to-server two-recipient HPKE store/fetch/receipt with sender restart and final dedupe; archive-first real HPKE mailbox receipt recovery; archive provenance/cursor/revocation tests; shared portable storage; transfer integrity/restart; group History Sync (74 nonempty imports); emoji Unicode/encrypted round trips; clipboard attachment regression.

The two-recipient fixture seeds isolated authenticated account/session rows; it is not a login-issuance test. Separate JNI real-server login/resume tests cover those paths. Host JNI substitutes fail-closed attachment/voice adapters: it does not establish Android Keystore, POSIX I/O, OS lifecycle or physical acceptance.

Final Windows All/CacheCandidate build passed in `build/dev166-final`; Android assembleDebug, unit tests and lint passed in `build/dev166-android-release-check.log`. Linux x64/ARM64/ARM32 runtime/version/ELF package audits passed. Final JNI pinned-TLS tests passed 8 protected profile cycles, 3 authenticated server runtime reopens, 6 bidirectional deliveries and 2 offline IDs drained exactly once. The media/transfer fixture passed a 10 MiB durable transfer across real screen-stop/re-pairing; group screen tests passed two real RTP/MFT receivers. The final group attachment Application/TransferManager/HPKE/managed DiscoveryClient fixture passed against the dev.166 server, including reconnect and bounded reference backfill (isolated evidence `F:/PeerChat-isolated-cache-tests/application-5cc9625dc2be48b48c3015b22927487a`). These are automated host results, not physical acceptance.

The loaded host endpoint fixture measured median 8,369 µs / p95 35,311 µs / mean 13,561 µs while concurrent compilers ran. It is not a baseline comparison or physical LAN throughput result. No Android throughput, media smoothness or all-device latency improvement is claimed.

## Deployment and physical handoff

Read-only preflight found Pi ARMv7, running dev.158, existing 10 GiB/48-hour encrypted cache on the bound ext4 filesystem. Pinned TLS authentication/discovery and cache mount/free-space checks passed. Cache SQLite uses an exclusive live owner: integrity/consistent backup must run after the controlled stop, never by bypassing its lock. Configuration and certificate remain unchanged. The update helper preserves binary/config/private-file backups plus consistent application/cache database backups and rolls back only binaries/config while retaining the CURRENT database.

Physical acceptance remains pending. Install APK with `adb install -r`, never uninstall/clear data. Test: both authorized devices online; one offline then reopen; sender restart before second ACK; reconnect/repeated login; overlapping History Sync/live delivery without duplicates or false ACKs; ordinary DM/group/attachment/emoji nonregressions. Keep Windows PHONE-profile sessions closed when specifically isolating Android authority. No phone access is required or attempted by this milestone.

### Final artifacts

All paths below are relative to this report's `peerchat` repository root. Unzip the Windows package; do not run the incomplete Client-only directory.

- Windows: `build/development/4.0.2-dev.166/PeerChat-Windows-4.0.2-dev.166.zip`, SHA-256 `34a1abf358414aa47f4863052de58a93072f6fe3f1862030916478ae224f9a54`.
- Android: `build/development/4.0.2-dev.166/Android/PeerChat-Android-4.0.2-dev.166-debug.apk`, SHA-256 `702a6e2953882bbaeb31aa9be9811298bcb882b5915b2a16c7ebc0edf0bf8289`. ARM64; accepted signing certificate SHA-256 `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141` unchanged. Version/signature/dependencies/ZIP entry bytes/source checkpoint audits passed.
- Pi ARM32: `build/linux/4.0.2-dev.166/PeerChat.Server-4.0.2-dev.166-linux-arm.zip`, SHA-256 `d88b57dc1570d021c0cdb85e78580cc772dee8676f4e0b7e110e2f5a172d72b5`.
- Machine-readable client/package audit: `build/development/4.0.2-dev.166/messaging-repair-package-audit.json`. Its deployment flags describe package creation time; live deployment result is below.

### Authorized Pi deployment result

Deployed **dev.158 → dev.166** at 2026-10-16 04:07 UTC. Postflight confirmed executable/config command line and dev.166 PID 19692. Backups: `/opt/peerchat-test/rollback/pre-4.0.2-dev.166-20261016T040757Z` (previous runtime/config/private referenced files, online/stopped application DB and stopped cache index). All three SQLite backups passed integrity checks. Stopped cache index integrity passed; active exclusive ownership was not bypassed.

Pinned authentication and discovery passed before/after. Certificate, byte-for-byte configuration, original protected identity/grant table contents, 10 GiB/48-hour cache filesystem binding and free-space reserve checks passed. Existing cache remains enabled exactly as physically accepted. No TURN, firewall, OS, credential, key or grant changes. Automatic rollback was not needed. Rollback uses the old executable/config with the current database; NEVER restore an older database over subsequent writes automatically. See `scripts/pi-compatible-update.py` and `build/dev166-pi-deployment.log` / `build/dev166-pi-postflight.log`.

Mailbox store/fetch/ACK and relay behavior were exercised in authenticated isolated production-path fixtures, not by sending test messages through real user accounts on the live Pi. Live full two-device delivery remains physical acceptance work. An ARM32 relay fixture was built/staged but not executed because its ephemeral listener defaults to all interfaces; no additional Pi listening endpoint was opened.

### Final repository state and remaining boundaries

Production source is committed at `a91c34f`; subsequent checkpoint contains only this handoff, compatible deployment safeguards and package-audit support. Generated MSVC intermediates were moved without deletion into `build/msvc-intermediates-dev166-preserved`; preliminary dev.166 outputs remain in `build/dev166-preliminary-preserved`. Accepted dev.165 and older packages remain untouched. Working tree is intended clean after the handoff checkpoint; build/evidence directories remain ignored and preserved.

No performance improvement claim, automatic sent-message mirroring, new-device historical access, historical provenance migration or group delivery implementation is included. The canonical-only unresolved fallback remains a functional limitation, not a full independent-delivery guarantee. Stop for manual Windows/Android acceptance before expanding this work.
