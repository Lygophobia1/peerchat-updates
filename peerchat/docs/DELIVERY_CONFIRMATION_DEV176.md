# Delivery confirmation and recipient selection in dev 176

Dev.176 is a client reliability candidate, not physical acceptance. The investigation distinguishes missing delivery from delayed confirmation. Existing encrypted messages, frozen recipient manifests, keys, histories and transfer journals remain unchanged. The live Pi remains on dev.175; server changes in this branch are not deployed.

## Physical evidence

The Windows report captured on 2026-10-17 at 20:33:22 and Android report at 20:34:20 confirm different outcomes in the two directions. Android has resolved outgoing messages with verified recipient ACKs. Its older unresolved messages can have one valid ACK while still correctly showing partial confirmation.

Windows message `616e47f975f64efe8fb894d2ddf3f3a1` has an unresolved frozen selection containing only device `35049f…` (Windows PHONE), with no ACK. The Motorola's identity is `61f70f…`. Read-only Pi inspection found only the Windows-device encrypted envelope for this message. The phone cannot decrypt or acknowledge an envelope intended for the other device. This is not evidence of routing to the wrong account: both devices belong to account 4.

Windows knows both identities, but an installed, fresh eligibility snapshot is required to select verified siblings at local message commit. Its current report shows 574 snapshot RPCs versus 57 native installs and authentication generation 59. Earlier retained reports contain repeated `snapshot_quota_exceeded`. The precise generation and snapshot state at this particular send were not retained, so quota exhaustion versus an invalidation gap cannot be uniquely assigned to it.

The diagnostics navigation is a list of current and retained connection scopes, not a device inventory. Repeated shortened phone labels do not establish four identities. The inspected server account contains two device identities.

The latest Android snapshot was captured about three seconds after remembered authentication bootstrap succeeded, while its event channel was not yet connected. That snapshot alone does not demonstrate a persistent event-session failure.

## Confirmed defects and corrections

### Eligibility refresh and quota

Windows republishes its authenticated friend generation on every successful friend refresh, even when the security-relevant mapping is identical. Native correctly invalidates old-generation eligibility. A send in the resulting gap commits a canonical-only unresolved manifest, which does not subsequently expand. The prospective correction preserves the generation only for an identical account/device mapping within the same profile and authenticated session. Changed mappings and session boundaries still invalidate it; native freshness and trust checks remain authoritative.

The server previously allocated another immutable eligibility proof on every query. Retained proofs live for ten minutes and each sender device may retain 64, so repeated refreshes can exhaust that limit. The server correction recomputes authorization and the current audience, then reuses an identical existing proof only when at least 90 seconds of its original two-minute freshness remain. It never extends freshness, changes the proof or increases quota. The 90-second minimum preserves headroom for older clients with a fixed 60-second refresh cadence. Changed audiences still require a new proof. This server correction requires separate deployment approval.

Both clients schedule refresh against the proof's actual freshness deadline rather than assuming every response starts a new two-minute lifetime. Android also backs off quota refusals and retains fair recipient iteration.

### Android confirmation publication

The dev.175 physical trace records owner projection up to 13,812 ms and owner gate waits up to 27,201 ms. These are local measured maxima, not one correlated end-to-end delivery duration. Repeated complete projection and unrelated history paging contend with local work.

The correction bounds periodic projection to approximately one second during active peer work and ten seconds while idle, with immediate dirty hints for native events and durable imports. Empty mailbox passes no longer reread all history. Verified receipt and store changes prioritize the affected conversation; duplicate retained receipts are still validated natively but do not repeatedly trigger unchanged display work. A later outgoing failure cannot suppress publication of an earlier verified receipt. Remaining dirty pages drain without waiting a full idle interval per conversation.

### Windows history paging

The older-page cursor was taken from the combined account conversation and reused for each device. A newer first page from one device could skip older rows belonging to another device. Paging now retains independent per-device anchors, without allowing a late live message to move the history cursor. Display ordering continues to use authenticated timestamps with stable sender/message tie-breaks, not arrival time or rewritten timestamps.

### Attachment compatibility and diagnostics

Before a cache job exists, a classified cache-preflight socket failure or timeout now offers the existing explicit verified-direct choice. The sender must confirm delivery to the named online device rather than all devices on the account. TLS/authentication failures, malformed replies and native authorization failures do not trigger this choice. Group transfers and TransferManager v2 remain unchanged.

Native cache operations now retain the actual failure phase instead of leaving all failures labeled `jni_input`. No ABI or wire format changes are introduced.

Automatic fallback after a cache job or ambiguous upload is **not implemented** in this checkpoint. The current direct API creates a new transfer identity; blindly switching could duplicate an accepted cache attachment and narrow its audience. Existing cache status/retry and recovery remain in control. The supplied physical reports do not retain the structured failed cache reply needed to prove the specific attachment failure. RPC `result=ok` means a response arrived, not that admission succeeded.

## Validation boundaries

Completed host regressions include authenticated Windows eligibility installation and independent encrypted receipts, native durability and frozen manifests, History Sync overlap without false live ACKs, ordering storage/projection, server authorization/revocation/quota/restart, and production JNI/cache interoperability in both directions. The JNI fixture covers offline recipients, interrupted upload/download, restart, direct compatibility checks and durable mailbox delivery during upload. Native local text commit during upload measured 32 ms in that run; this is not phone or Pi end-to-end latency.

The final complete Android suite passed 349 tests across 75 suites with zero failures or errors, including the near-expiry refresh correction. Lint reported zero errors and 25 warnings, and the debug APK built successfully. The final Windows build passed with compiler warnings; runtime dependency, native-load, group voice/screen ABI and restricted DLL-search checks passed. Final rebuilt DM and group attachment UI regressions passed.

Focused Windows tests cover identical mapping, reordered inputs, display-name changes, failed native publication, zero account, changed account/device mappings, profile/session changes and sign-out. The authenticated TLS/native two-device eligibility application regression and Windows mailbox reliability suite passed. The group attachment UI regression passed, including verified actions, expiry, private drafts, bounds and stale response rejection.

Local evidence includes `build/dev176-android-final.log`, `build/dev176-windows-final.log`, `build/dev176-server.log`, `build/dev176-durability.log`, `build/dev176-archive-mailbox.log`, `build/dev176-eligibility-app.log`, `build/dev176-jni-cache.log`, `build/dev176-chat-attachments-final.log`, `build/dev176-group-ui-final.log`, and `build/dev176-package-audit.log`. Isolated fixtures do not use real user profiles or modify the live server.

## Integration manifest

| Components | Changes and compatibility |
| --- | --- |
| `desktop/App.cs`, `FriendConversations.cs` | Same-mapping publication guard, scoped to existing profile/session generations. No new native API. |
| `desktop/ChatViewModel.cs` | Per-device paging anchors. Stored message ordering and IDs unchanged. |
| `desktop/MailboxExecution.cs` | Refresh before actual eligibility expiry. Existing authenticated RPC and native operation 11 unchanged. |
| Android `MessagingBackend.kt`, `MailboxExecution.kt`, `LocalHistoryProjection.kt`, `EligibilitySnapshotExecution.kt` | Bounded projection and verified-change hints; expiry-aware refresh. Kotlin-local callback changes only. |
| `desktop/ChatAttachments.cs`, Android `AttachmentCompatibility.kt`, `PeerChatApp.kt` | Explicit verified-device choice for classified preflight transport failures. No automatic scope narrowing or changed transfer packets. |
| `portable/peerchat_core.cpp` | Fixed diagnostic stage names for existing cache operations 11–14. No signature, JNI ABI, authentication or packet changes. |
| `server/EncryptedMailbox.cs` | Same-audience immutable proof reuse with original expiry and quota enforcement. Backward-compatible reply fields; not deployed. |
| `shared/VersionInfo.cs` | Candidate version 4.0.2-dev.176. |

The Android worktree's integration notes and checkpoint `ac7ab34` were inspected read-only. This milestone introduces no conflicting shared API, group encryption format, database schema or message protocol. The server reuse optimization is independent of the client corrections; clients still support the existing dev.175 server. Its quota remains unchanged until an authorized deployment.

No physical post-fix ACK latency, transfer throughput or connection time is claimed. The exact observed 5–10-second delivery delay cannot be reconstructed by subtracting timestamps from different devices. Local monotonic timing identifies client contention, while message IDs correlate authorization and durable receipt state.

## Recovery limits and further work

An existing unresolved manifest is not silently broadened, and its ciphertext is not redirected. The old Windows-only messages require delivery to their original authorized device, followed by separately authorized existing history recovery where applicable. A general durable account-wide send that waits for eligibility before freezing its audience would require an explicit design for pending selection and restart semantics; it is not disguised as an ACK fix.

An unreachable authorized secondary device can legitimately keep aggregate delivery partial. A history import never acknowledges an independent live/mailbox target. No account reset, key replacement, automatic trust or device exclusion is used to make the UI show Delivered.

The most useful next improvements are explicit diagnostics for recipient selection at commit, bounded authenticated eligibility acquisition without redundant context invalidation, and a separately reviewed identity-preserving cache/direct handoff for ambiguous upload recovery. A networking rewrite is not supported by the current evidence.

## Physical acceptance checklist

1. Keep the Windows PHONE session closed initially. Send new, uniquely identifiable messages in both directions. Check device-specific ACKs, not only aggregate labels.
2. Repeat after reconnect and app restart; send during a contact refresh. Confirm both verified account devices appear in a resolved audience when eligible.
3. Bring the secondary device online and confirm it receives its independent copy without duplicating the conversation row. Distinguish old unresolved messages from new candidate messages.
4. Receive offline queued messages and overlap authorized History Sync. Check stable ordering and no false delivery ACK from history alone.
5. Test cache admission, explicit direct compatibility choice, interrupted transfer and restart. Confirm security refusal never becomes direct fallback and narrowed delivery requires confirmation.
6. Export both complete diagnostics immediately after any failure. Do not change trust, grants or identities to work around it.

## Checkpoints and packages

Baseline: `86ceece`, on `codex/native-unicode-emoji` in `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game`. Checkpoints:

- `31da8b8`: per-device history paging.
- `764966a`: eligibility proof reuse and expiry-aware Windows refresh.
- `edfd5e9`: mixed-client 90-second reuse floor, superseding the initial 30-second floor.
- `0ee932c`: unchanged authenticated friend mappings preserve generation.
- `89ed130`: Android receipt publication, refresh and explicit preflight recovery.
- `5f38443`: Windows preflight recovery and native cache-stage diagnostics.
- `df2b931`: dev.176 candidate source version.

Package source checkpoint: `df2b931`. The final build was produced in the isolated `build/dev176-final-validation` directory and promoted to the unshipped versioned candidate location. The earlier unshipped build remains in `build/dev176-initial-validation`; generated compiler objects are retained in the corresponding build evidence directories. Accepted dev.175 artifacts were not replaced.

Windows ZIP:

`C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat\build\development\4.0.2-dev.176\PeerChat-Windows-4.0.2-dev.176.zip`

SHA-256: `58f9f9ba175820076eee07735c624572388ac0f7bcd4452b59353c168168ca88`.

Android APK:

`C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat\build\development\4.0.2-dev.176\Android\PeerChat-Android-4.0.2-dev.176-debug.apk`

SHA-256: `a365d76ae557e8c0a18da4e9f193a7bece2d14bd18bff91396668f10d0545c58`.

The package audit verified version, source checkpoint, ZIP payload hashes, Windows dependencies, Android arm64 ABI and the existing APK signing identity. Update Android in place with `adb install -r`; never uninstall or clear data. Extract Windows to a new folder and use the existing profile. No APK installation or Pi deployment was performed. Physical acceptance remains pending, and automatic post-upload direct fallback remains incomplete. The documentation checkpoint contains this report; production source has no uncommitted changes.
