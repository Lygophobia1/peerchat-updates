# Windows Device Manager: authorization audit and approval boundary

Date: 2026-10-17 (America/Chicago; test logs cross into 2026-10-18 UTC).

## Outcome and repository state

**Audit complete; implementation stopped at the requested security approval boundary.** Full-device remote revocation is not an existing operation. Reusing remembered-login revocation, mailbox-key revocation, or deleting an ownership row would not safely implement the requested behavior.

- Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`.
- Branch: `codex/native-unicode-emoji`.
- Starting HEAD: `f45ab05`, clean. Accepted dev.178 implementation checkpoints `fc32687` and `9c6c2cb` remain unchanged.
- This checkpoint changes documentation only. No endpoint, schema, native API, UI, Android behavior, protocol, or live registration was changed.
- No Pi access/deployment, real-device revocation, account linking, identity regeneration, or package replacement occurred.
- Jonah account 3 and JonahPhone account 4 remain independent accounts.

Two read-only reviews covered server authority and Windows/Android integration. The primary review independently checked the decisive ownership, revocation, native signing and test paths.

## Existing authority inventory

Paths below are relative to the PeerChat worktree. Method names are preferable to line numbers because server source contains long compact lines.

| Domain | Authoritative record and enforcement | Local responsibilities / limitations |
| --- | --- | --- |
| Account/device ownership | `server/PeerChat.Server.cs`: `devices`, `AddDevice`, `OwnsDevice`. Unique public identity belongs to one account. | Historical identity mappings are not a current authorization decision. Ownership row has no full-device revoked state. |
| Identity/fingerprint | Device private key stays in existing local secure storage; authenticated proofs and peer TLS bind public identity. | Contact pins and explicit trust are local; a display name cannot replace them. |
| Enrollment | Account-authenticated `presence` calls `AddDevice`; remembered enrollment additionally verifies the existing device's signed challenge. | Existing password sessions can enroll/re-enroll; there is no persistent full-device revocation tombstone. |
| Remembered login | `RememberedRequest`, `RevokeRememberedDevice`, `CreateDeviceSession`: server credential and device-bound sessions. | Revocation disables that credential, associated bound sessions and push registration, not the account's device ownership or ordinary password sessions. |
| Sessions/leases | `Authenticate`, `SessionAllowsDevice`, event sessions and live presence. Bound sessions constrain identity; legacy unbound account sessions do not. | Native runtime generation and app session ownership must reject late replies. Presence is a current lease, not durable verified last-seen history. |
| Recipient keys | `server/EncryptedMailbox.cs`: signed recipient registrations and `RevokeMailboxKey`. | Private HPKE keys remain local. Key revocation is self-device/key scoped, not another device's administrative revocation. |
| Recipient eligibility | `EligibilityBindings`, `CurrentEligibility`, snapshot validation: exact account/device/key/generation and current server authority. | Frozen message targets and signed receipt checks must remain immutable and independent of UI names. Offline is not revoked. |
| Contact trust | `reliable_peer_protocol.hpp`: `AuthenticatedFriendSnapshot`, `initialPin`, `persistAuthenticatedContact`; local contact store. | Known historical identities support provenance and pin decisions. Do not erase them to simulate account-device revocation. Current authorization needs a distinct gate. |
| History Sync | `server/ArchiveAuthorization.cs`: signed grants, generations, capabilities and ownership; archive relay checks. | Local provenance, verified records, grants and durable cursors remain necessary. Device management must not fabricate grants or resolve old-server ownership. |
| Attachment cache | `AttachmentCacheAdmission.cs`, `AttachmentCacheDirect.cs`: current membership, exact device/key audience, signed capabilities and HPKE authorization. | Names cannot grant downloads. Preserve original audience, signed descriptors, expiry, encrypted objects and local verified copies. |
| Groups | Signed roster/epoch authority and `GroupMessageAuthorized`/`GroupRosterMember` plus account ownership. | A server administration action must not forge roster signatures or pretend already-distributed group keys were erased. |
| Names/capabilities | No own-account device inventory/name endpoint. `attachment_dm_capabilities` records exact signed cache support, not platform/version. | `desktop/HistorySettings.cs` `HistoryDeviceNames` is DPAPI-protected profile-local metadata, not synchronized or authoritative. |

### Truthful field coverage for a future page

- Fingerprint, ownership and enrollment time: existing server records; expose only through an authenticated own-account list.
- Current device: match the local immutable identity with authenticated account ownership, never a nickname.
- Online: fresh authenticated lease only. An expired/missing lease means no confirmed live presence; never infer revocation.
- Capability: exact active signed registration and key generation. Missing registration is not proof of a particular application version.
- Custom shared name, platform, application version, durable last verified activity and full-device revocation: not currently available as the requested authoritative inventory. Show unavailable until explicitly supported; do not manufacture values from events or filenames.
- Private inventory, names and administrative history must not leak through friend/contact APIs. Contact views retain only their separately authorized information.

## Confirmed security blockers

### 1. Remembered-login revocation is intentionally narrower

`RevokeRememberedDevice` removes matching device-bound sessions, remembered credentials and push registration. It does not remove `devices` or ordinary account sessions. `tests/remembered_login_test.cs` explicitly asserts the ordinary account token still works after revocation and then successfully re-enrolls the same identity.

`RememberedRequest` authenticates a revoke using the target device's own credential and key. It does not authorize one device to revoke another. Windows `windows_mailbox.hpp::rememberedDeviceProof` accepts only `enroll` and `resume`, checks current local account/device, exact canonical challenge bytes, freshness and replay. Extending it to administrative actor/target operations is a new scoped authorization purpose, not a UI adapter over an existing remote-revoke operation.

### 2. Mailbox-key revocation cannot be used as the shortcut

`RevokeMailboxKey` verifies a self-device/key signature and deletes pending encrypted envelopes for that key. The requested feature must preserve queued records and cannot sign as a different device. Do not call this operation to implement full-device management.

### 3. Ownership-row deletion is destructive and reversible by ordinary enrollment

`AddDevice` inserts a missing identity after account authentication. Deletion alone does not prevent re-enrollment. It also affects archive-grant triggers and DM relationship bindings, while historical group roster and mailbox-key records can outlive the ownership row. `PurgeMailbox` deletes envelopes lacking sender/recipient ownership rows. Therefore deleting an ownership row risks losing queued ciphertext and collateral authorization changes; re-adding it risks reviving retained authority.

Use a non-destructive authorization tombstone and separate current-active checks instead. Preserve provenance ownership records, history, existing signed grants and immutable target manifests. Audit all callers rather than changing the meaning of `OwnsDevice` globally without distinguishing provenance from active permission.

### 4. Older devices legitimately contribute to partial delivery/cache blocking

An authorized offline device can remain in recipient eligibility. Cache preparation requires every required device/key generation to have supported signed capabilities. Missing support can block account-wide cache admission; an unacknowledged target can keep aggregate delivery partial. These are not reasons to silently drop a device or manufacture delivery.

The audit proves those mechanisms exist; it does not determine which real device the user should revoke, and no real registration was altered. Existing dev.178 per-device status remains the truthful presentation until a verified authority change exists.

## Minimal proposed extension — NOT IMPLEMENTED

The following needs explicit approval before production implementation or a new management-proof fixture. It reuses existing identity keys and cryptography but introduces new administrative authorization semantics.

### Management authority and recovery policy

1. Own-account read/name/revoke operations require a currently valid, proof-bound actor device. No authorization from a supplied account number, display name or historical contact map.
2. For revocation, require explicit confirmation, fresh password step-up and the actor's existing identity-key proof over a single-use server challenge. Bind server identity, account, actor, target, operation, expected authority revision, expiry and nonce. The existing fixed-format signer must gain a separately typed, narrowly scoped management operation, never arbitrary-byte signing.
3. Add durable active/revoked state and monotonic account authority revision, preserving ownership/provenance rows. Commit target-state change and nonsecret audit metadata atomically. Recheck actor, target, revision and last-device rules inside the transaction. Duplicate operation IDs return the original outcome; stale/forged/mismatched proofs fail closed.
4. Initially reject self-revocation and revocation of the last active device. Do not invent account recovery in this milestone. Recovery after loss of all devices remains a separately approved policy, clearly reported as unavailable.
5. A revoked identity cannot automatically re-enroll through password login, `presence`, remembered enrollment or resume. Re-enrollment is unavailable in the first implementation; a later explicit process would require fresh target proof and independent current authority. Never implement a reversible UI checkbox.
6. Resolve legacy unbound account sessions explicitly: they cannot identify which physical device owns a token. Proposed conservative transition on revocation is to invalidate the affected account's unbound sessions; current clients resume with their own proof-bound credentials. This may require other devices on that account to reconnect. Do not revoke unrelated accounts or silently label unbound sessions as belonging to the target. New sensitive device operations must require device proof; a password session alone must not undo a tombstone.

Password compromise/new-identity enrollment is a separate account-security threat. Do not claim device revocation removes an attacker's knowledge of the account password or remotely wipes keys. The proposed policy needs approval precisely because current enrollment and session semantics do not provide these guarantees.

### Inventory and naming

- Minimal authenticated operations: own-device snapshot; compare-and-set rename; management challenge/commit for revoke. Exact wire names and canonical signature encoding require implementation review after approval.
- Snapshot binds server epoch, account and monotonic revision; response processing also binds current local profile/login generation. Omit credentials, push tokens, private keys and envelope contents.
- Shared names are optional metadata keyed by immutable identity. Validate bounded Unicode text and byte length, reject control characters, and treat names as plain text. Document one normalization/length contract for both clients before implementation.
- Concurrent rename uses expected revision and a server transaction: one succeeds, the other receives a conflict and refreshed state. Do not order writes by client clocks.
- Do not silently promote profile-local History Sync nicknames into server names or infer Windows/Android platform from fingerprints. Platform/version would be explicit, authenticated self-reported metadata, not hardware attestation.

### Enforcement and cross-device reconciliation

- After revocation, deny new eligibility, enrollment/resume, mailbox access, cache access, relay admission and group/archive participation for the target. Revalidate existing bound sessions and transport operations, retire affected leases and routes, and fence in-flight operations by authority revision.
- Keep historical ownership separate from active authorization. Preserve signed group rosters and epochs; apply current deny gates without rewriting signed records or broadening historical access.
- Existing queued ciphertext is retained under its original targets and retention rules, not redirected, deleted as a side effect, or marked acknowledged. A verified revocation may explain an unavailable target; it is not a delivery receipt. Preserve valid existing signed receipts.
- Cached objects must remain usable by other still-authorized original recipients. Do not use ownership deletion/relationship reset as a broad cache invalidation shortcut.
- Use authenticated event hints and bounded snapshot reconciliation on startup, reconnect and revision changes, independent of whether Settings is open. Android consumes state without new management UI. Ignore late replies from a replaced account/session; stale snapshots cannot reverse a newer decision.
- Client native authorization must consume current device state separately from historical mapping and contact pins. Healthy direct connections require invalidation when a verified revocation update arrives. Server denial alone cannot instantly stop an already-authenticated direct socket between disconnected or older clients; document this compatibility limit and require updated enforcement before claiming complete cross-device revocation.
- Neither server nor client can erase previously decrypted data or distributed keys. Existing authorized key rotation remains separate from revocation; no fake group-epoch change or new cryptography is proposed.

### UI and compatibility boundary

Reuse `desktop/App.cs::SettingsPage` and existing theme resources for a Device Manager page, not the History Sync grant-management screen. Reuse the account/generation/epoch fencing pattern in `desktop/HistorySettings.cs`. Show a confirmation with exact fingerprint and consequences; names are supplemental. No nonfunctional Revoke button is shipped in this audit.

Windows adapters would extend the scoped `desktop/DiscoveryClient.cs` surface and existing protected profile credential binding in `desktop/AttachmentDeviceSession.cs`. Native proof implementation belongs alongside `windows_mailbox.hpp` with cross-platform canonical tests. Android reconciliation belongs in the existing `ServerAccounts.kt`, `MessagingBackend.kt` and authenticated metadata projection, with explicit diagnostic allowlisting if needed.

An older server must yield an explicit unsupported state, not guessed inventory or simulated success. Older clients may need upgrade/re-authentication for complete enforcement. Server authority remains strict regardless of UI availability. No account linking, new History Sync engine, Android management page or attachment format change is proposed.

## Validation performed

Both runs use isolated test state, not live accounts. Logs are retained under ignored `build/`; conclusions are recorded here for the Git checkpoint.

| Existing test | Result and evidence boundary |
| --- | --- |
| `tests/test-remembered-login.ps1` | PASS. Production authentication dispatch exercised in-process, original-message CNG proof, wrong signer/device/purpose, replay/expiry rejection, 32 resumes, durable reopen, replacement/revocation and unchanged ordinary account sessions. Explicit successful re-enrollment after credential revocation. Not a physical network or new Device Manager test. Log: `build/device-manager-audit-auth.log`. |
| `tests/test-remembered-device-native-proof.ps1` | PASS. Existing constrained native enroll/resume proof, account/device/purpose/freshness/replay/ABI checks. Log: `build/device-manager-audit-native-proof.log`. Does not validate a future management signer. |

No new management implementation, UI, integration regression or package build was run or claimed. Existing dev.178 packages and prior validation remain unchanged. No physical acceptance is implied.

## Required tests after approval

1. Two accounts, multiple active/offline devices: inventory isolation; no contact inventory leakage; truthful unavailable fields and fresh presence.
2. Names persist through server/client restart; concurrent compare-and-set conflict; Windows-to-Android event loss/reconnect recovery; stale profile/epoch/revision replies rejected.
3. Actor/target/account/purpose mismatch, forged/replayed/expired proof, revoked actor, unbound session, password step-up failure, self/last-device rejection, and crash before/after commit.
4. Target sessions/leases/cache/mailbox/relay/archive/group gates invalidated; same-identity automatic re-enrollment denied; other accounts and authorized devices unaffected.
5. Frozen target manifests and queued ciphertext preserved; signed receipts not fabricated; old history/provenance still readable; no mailbox purge caused by the tombstone.
6. Cached DM/group objects retained for valid recipients; target cannot read new or existing server-held objects after revocation; stale authority and in-flight lease races fail closed.
7. Updated native direct transport responds to authenticated revocation; disconnected/legacy enforcement limitations explicit. Keys/pins/history never deleted to hide failure.
8. Existing messaging, attachments, History Sync, emoji and media regressions; Windows/Android builds, package audit, then physical acceptance and separate Pi deployment approval.

## Costs, remaining decisions and resume instructions

No measured latency improvement is claimed. Expected added steady-state cost is bounded indexed snapshot reads/reconciliation, not per-frame signing or continuous database writes. Rename/revoke writes occur only on management actions. Fresh step-up adds intentional administrative latency; revocation can interrupt the targeted connection and force legacy account sessions to reconnect. Keep network waits outside application locks and avoid delaying normal text/ACK work behind inventory refreshes.

Required approval: the new scoped cross-device management proof, non-destructive tombstone, conservative legacy-session handling, and first-release prohibition on self/last-device revocation and reactivation. A full recovery system is not implemented or implicitly authorized by this proposal.

After approval: implement isolated server/proof contract tests first; then transactional authority/enforcement and compatibility gates; then Windows UI/naming and Android consumption; finally broad regressions and versioned candidates. Use separate logical checkpoints. Do not deploy or revoke any real device until separately authorized. Until that approval, stop here and preserve dev.178.
