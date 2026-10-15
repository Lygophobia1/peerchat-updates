# Android dev.158: device verification and mailbox diagnostics

Focused repair baseline: ade45a4. Branch: codex/android-groups-voice.
Compiled implementation:837c357664050084b5d44e1668681f2cb5c9a4d4.
Final documentation commit adds no compiled changes. Stop for physical acceptance.
No real profiles, keys, pins, queued recipients, Pi accounts or server data were
reset, migrated or modified. Main Work and its attachment/cache work are untouched.

## Proven findings versus unresolved phone causes

| Physical symptom | Evidence-supported conclusion | dev.158 behavior |
| --- | --- | --- |
| native_recipient_registration / IllegalStateException, recipient_not_requested | dev.155 converts null native registration to IllegalStateException. JNI catches native/provider failures and clears Java exceptions. Failure is local and before mailbox_register; supplied evidence cannot distinguish the underlying native precondition/registry failure. | Existing d6c3a64 isolation remains. Native phase, native authorization readiness and fixed registry/provider reason are now retained. This is diagnostic, not a proven provisioning repair. |
| Sammy initial_pin=existing_verified_account_device_requires_explicit_verification | Constrained initial pinning intentionally refused an account with a verified historical device. Android lacked the existing Windows-style explicit prompt. | Equivalent Compose prompt exposes existing native approval/persistence, with exact identity/session revalidation. No silent approval or replacement. |
| Queued recipient differs from current Sammy device | Existing account display index retains historical device bindings. Presence selecting another device does not make old messages invalid or authorize retargeting. The supplied report lacks full IDs to prove a stale mapping. | Diagnostics label retained rows current_resolved_device, historical_account_device or not_in_account_display_index. Original message ID/recipient/ciphertext is unchanged. |
| platform relay closed / retry | Generic transport retirement is not a unique root cause. Verification refusal can prevent messaging but a socket close alone proves neither offline status nor an authentication defect. | Separate fixed purpose/stage/reason diagnostics distinguish friend, archive_history and group_control. Existing native bounded retry owns reconnection. |
| History Sync platform relay failed | Independent archive scope: exact approved source/reader/device lease and TLS bindings, not ordinary DM trust. The report does not identify rendezvous, authorization or peer TLS origin. | Retained stage/reason will distinguish relay_rendezvous, native_peer_tls_open, peer_tls_receive or relay_output; no archive permission bypass or broad repair. |

**Motorola mailbox provisioning and bidirectional delivery remain unproven.** A new
verification prompt cannot create the missing recipient registration or prove fetch.
No reproducible recipient algorithm/registry schema/JNI descriptor defect was found;
no key-provider replacement or speculative cryptographic fix was made.

## Existing Windows verification reused

Reference inspected: desktop/App.cs TrustRequired queue/Confirm, CoreAdapter.Approve,
ui_approve and Node Action::Approve. Both the existing Windows operation and this
Android endpoint use reliable::persistAuthenticatedContact and existing native
approval/session activation. Existing Windows source/behavior is unchanged.

The prompt displays the current contact/account, prior trusted device fingerprints,
the newly presented device fingerprint and the additional/replacement/impersonation
warning. It instructs independent-channel comparison. Actions are Verify and Accept
and Keep Untrusted; dismissal is rejection. Fingerprint text scrolls independently
so actions remain visible with32 prior devices.

Approval invokes the native operation only after matching the complete local proof:
local account, authentication generation, attempt, remote account, peer fingerprint
and exact sorted prior-pin list. Native revalidates current TLS Hello/key, expected
identity, authenticated friendship, attachable transport, creation generation and
existing20-second presentation deadline. Stale/replaced/revoked/wrong-contact proof
cannot grant trust. Existing historical pins are retained; acceptance adds the exact
new device pin through the existing protected Storage contact operation.

Expired prompts remain visible with acceptance disabled until a fresh matching
presentation updates them. One pending item per fingerprint,32-item bound, no
duplicate dialog per retry, and a single active user decision prevent prompt floods
and double acceptance. Rejection suppresses that fingerprint for this authorization
lifetime and closes the attempt without modifying contacts or messages. Reconnect
or app restart can request review again; rejection is not a new persistent trust DB.
Account/session change invalidates approval. User acceptance is never retained as a
permission to accept a different future fingerprint.

The shared HistoricalPin decision is a DM contact-trust decision and is handled by
one application event path regardless of which conversation is selected. Existing
group/archive scoped TLS does not use ordinary friend pinning and cannot create a
friend pin from membership. Their signed device-enrollment/history-consent guards
remain mandatory; this change does not add a new group approval system or silently
approve group-only devices. Existing Android History Sync device consent UI remains.

Security regression also demonstrated a buffered-output gap: an ordinary DM endpoint
could emit previously buffered output after its friendship was revoked. takeOutput
now applies existing authorized() to every scope before emitting. It discards the
revoked transport buffer while retaining original durable queued messages for the
existing authorized retry path. Windows Node is unchanged.

## Mailbox trace and provider state

Call chain:
MailboxExecution -> MailboxBridge.registration -> native mailboxProviders ->
Android identity provider -> keyRegistryProvider facade -> distinct registry SQLite
connection/schema -> ProtectedKeyRegistry -> DeviceKeyManager snapshot/provision ->
KeyCoordinator signed binding -> mailbox_register -> native acceptance/reconcile ->
fetch -> verify/decrypt -> durable import -> signed ACK. The server call occurs only
after a complete native binding. Presence/login and storage consent are not proof
of any of these operations succeeding.

Native registration diagnostics now preserve one of:
not_requested, provider_preconditions, identity_provider, registry_facade,
registry_schema, registry_protection, registry_read_provision_or_binding,
binding_prepared_local. They also show native_storage/native_delivery readiness,
native_authenticated_account and native_authentication_generation. Subsequent
outgoing/provider work cannot overwrite the captured registration stage.

Platform diagnostics preserve fixed registry_execute/prepare/row or
key_registry_provider failure stages. SQLite lock/SQL failures and missing-identity
recovery requirements have allowlisted codes. Arbitrary exception messages, key
aliases, SQL, protected records, secrets and tokens are never printed. Existing
Keystore keys, alias names, protected registry format and separate DB path are
unchanged. Registry I/O/corruption is never treated as an empty/new registry.

Recipient provisioning still requires API31+ AndroidKeyStore ECDH and an agreement-
only key. Existing keys are opened, not regenerated. Unsupported/recovery failures
remain fail-closed. Given recipient_not_requested, unsupported ECDH is not established
as the phone cause. A missing native authenticated account is another possible
precondition despite authenticated UI, but it too needs the new physical stage.
Compiled JVM descriptors for all registry/provider JNI calls were independently
checked and matched; the regression validates them without opening keys.

## Validation and checkpoints

- c907942: explicit native approval + creation-generation fencing, buffered output
  revocation guard, isolated actual Schannel verification/retry tests.
- bb2c0d3: safe registry stages and fixed failure classifications/JNI contract test.
- 837c357: Android JNI/Compose/backend integration and independent relay diagnostics.
- 198 JVM/Robolectric/Compose tests:0 failures/errors/skips.
- ARM64 debug APK, debug instrumentation APK compilation and lint pass.
- Lint:0 errors,25 retained warnings.
- Real Schannel acceptance/rejection, stale auth/fingerprint/deadline/replaced attempt,
  revoked friendship/buffered output, protected pin/profile reopen, immutable old
  queued recipient, new verified recipient, dropped ACK/exact retry/dedupe pass.
- Existing102 real reliable endpoint lifecycles pass.
- Native/managed mailbox suite passes actual identity-bound registration, real HPKE,
  pinned TLS sender-offline fetch/signed ACK, per-device receipts/restart, tamper,
  quota/expiry/rotation/revocation and wrong-device rejection.
- Instrumentation compiles but was not run. These are host/isolated-service results;
  no ART/Keystore execution on the Motorola or actual physical Windows/Android
  bidirectional acceptance is claimed.

Evidence in build/tests/android-session/dev158-package-final.log,
dev158-mailbox.log and dev158-reliable.log. The native verification fixture executable
and isolated profiles remain under build/tests/device-verification-8c256923dcd743a998e599ac7c31fc7b.
Initial compile/UI test failures are retained in earlier dev158 logs; the final run
passes. Two selective agents completed/stopped; no overlapping production edits.

## Integration manifest delta

Read Main Work committed TRANSFER_CACHE_INTEGRATION_MANIFEST at cfc3fa2 before shared
changes. No Main Work worktree edit/merge, Pi deployment, server RPC/crypto format,
storage schema, C ABI header, signed ledger, PCGE or mailbox wire change.

Shared modified native files: portable/reliable_endpoint.hpp (optional public explicit
verification methods and authorization check), portable/peerchat_core.cpp (local
JNI proof/decision and registration diagnostics). MailboxBridge adds an Android-only
registrationDiagnostic export. ReliableBridge.exchange retains its existing signature;
local operations5/6 carry bounded type3 PeerChat/DeviceVerification/v1 records. These
are local command records, not peer wire. Existing helpers remain the trust authority.

Android modifications are restricted to mailbox/registry diagnostics, relay diagnostic
retention, immutable verification presentation, backend commands and Compose prompt.
Existing group/voice/attachment implementations and feature gates are preserved.
Future merge review should reconcile portable endpoint/core additions, not replace
Main Work's Node/TransferManager/cache/encryption implementation.

## APK and data-preserving physical procedure

Candidate: PeerChat-4.0.2-dev.158-verification-mailbox-diagnostics-debug.apk.
APK SHA256:4e627cdb9a132187f9602d8789be608ad3eceea3cba2990812beed8604ea1509.
Compiled source837c357; final documentation commit changes no compiled inputs.
Package com.peerchat.android, versionCode2, ARM64/API26+, target35. Signature matches
dev.155:db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141.
Existing dev.155/dev.156/dev.157 artifacts are retained. No data migration, uninstall,
clear-data, identity regeneration or automatic trust import is included. Matching
signing permits an update; on-device data preservation still needs physical acceptance.

1. Replacement-install only (adb install -r if using adb). Do not uninstall or clear
   app data. Record the existing native phone identity before/after; it must match.
2. Close only the competing Windows PHONE profile. Keep Sammy available. When the
   prompt appears, compare the full presented fingerprint against Sammy's actual
   current device over a separate trusted channel. Prior pins remain listed/trusted.
   Verify and Accept only after that comparison. If unsure, Keep Untrusted.
3. If the presentation expires, approval is disabled until a fresh authenticated
   presentation arrives. Stale approval fails. Sammy's Windows client may also need
   its existing explicit Android device verification; compare fingerprints both ways.
4. Send one new uniquely labeled message in each direction. Record native logical
   IDs, intended device, durable receive and ACK/receipt, then restart/reconnect and
   verify no duplicate or identity change. Old queued messages retain old recipients;
   do not assume they should appear on the newly verified device.
5. Copy the full Diagnostics report, especially native_registration_stage, native
   account/generation readiness, provider/registry reason, mailbox receive state,
   recipient_relation rows and purpose/stage/reason for any archive failure. Do not
   share secrets or raw database/key material. Verification alone does not prove mailbox.
6. If registration still fails, stop after collecting the new stage. Do not reset
   identities or grant extra History Sync permissions to mask it. Report the result
   so the actual provisioning/relay cause can be corrected without speculative changes.

This candidate stops for physical testing. The Motorola is not declared repaired.
