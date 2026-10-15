# Android dev.159 — inbound mailbox investigation and physical checkpoint

Date: 2026-10-15. Branch: `codex/android-groups-voice`.
Worktree: `C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`.
Source root: its `peerchat` directory. Starting HEAD: `dcdb3282d58346ca11dd7c95562211bfbff41872`.
Compiled source: `b73d035ab54beb1815028036ade03486862a2deb`.

**This is an improved diagnostic candidate, not a demonstrated mailbox-key or
Motorola receive repair.** Stop for physical evidence; do not reset data to make
registration succeed. No installation, Pi access, Main Work modification, merge,
identity replacement, key rotation, trust change, queue retargeting or migration
was performed. Existing group/voice/attachment work remains preserved.

## Physical findings supplied by the user

Dev.158 displayed the fingerprint prompt. The user accepted Sammy's new fingerprint,
and Android can now send to Sammy. Preserve this working verification/outgoing path.
Android still cannot receive. Windows reports the new outgoing message as **Stored
for delivery**. This supports server acceptance for mailbox delivery; it does not
prove that its recipient fingerprint equals the current Motorola fingerprint.
No new fingerprints or physical logs were supplied during this investigation.

The diagnostic includes authenticated account4, storage/delivery ready, local
`native_registration_stage=registry_read_provision_or_binding`, platform last stage
`registry_execute result=ready`, and `mailbox_keys` IllegalStateException with
unrecognized state. Presence was reconnecting, History Sync reported
`relay_pair_timeout`, and group transport reported `TlsNoCloseNotifyException`.

## Proven boundaries and actual corrections

| Boundary | Finding and dev.159 change |
| --- | --- |
| Local recipient registration | `MailboxBridge.registration` catches local native/provider exceptions and returns null. This occurs before `mailbox_register`; the supplied record is not evidence of server rejecting the phone key. Add fixed native phases: registry_snapshot, recipient_provision, recipient_rotation, binding_read_validate, binding_sign, binding_validate, binding_persist and successful binding_prepared_local. |
| Registry/provider evidence | SQL ROLLBACK succeeds during unwinding and overwrites the earlier platform failure with registry_execute ready. This diagnostic defect is corrected: retain the first allowlisted provider failure until the next local registration attempt. Record protection/unprotection and blob-column reads now expose their fixed boundary. STORAGE_RECOVERY_REQUIRED is retained rather than generalized. No raw exceptions, aliases, protected bytes or keys enter diagnostics. |
| `mailbox_keys` exception | This is outgoing lookup of a remote account's recipient bindings, not provisioning of Android's local key. Existing server responses rejected_or_storage_unavailable, malformed_request and invalid_key_or_signature were incorrectly displayed as unrecognized_or_missing. Their fixed codes are now recognized. We do not know which actual response the phone received. Server rejection and native registration remain independent. |
| Offline receiving | Local registration null means registeredBinding remains null, so fetch is intentionally skipped. This explains outgoing operation alongside blocked mailbox receiving. Add a separate inbound status, retained even if later outgoing lookup throws. It distinguishes registration block, fetched count, native rejection, durable import/receipt and acknowledged count. |
| Native null receive | A null result may mean sender authorization, local key/binding, decryption or durable storage failure. Previously every null was counted as awaiting verification. Rename to pending_native_validation and expose fixed native receive stages. Never ACK a null result or bypass verification. |
| Online incoming | Existing authenticated reliable endpoint and native message storage remain unchanged. Isolated real TLS tests pass, but the supplied phone evidence does not identify the physical online failure. A message Stored for delivery can be on the mailbox path. Verify exact Windows recipient device before drawing a routing conclusion. |
| Application projection | Approved contacts are read from the same persisted native trust store by direct and mailbox receive. Conversation projection includes all authenticated known devices; no demonstrated UI suppression defect was found. Native import counts remain visible independently of later outbound failure. |

Files: `mailbox_key_coordinator.hpp`, `portable/peerchat_core.cpp`, Android
`AndroidStoragePlatform.kt`, `RecipientFailureCode.kt`, `MailboxExecution.kt`,
`MessagingBackend.kt`, their focused tests, and the new native key-preparation
fixture/runner. The verification dialog and approval operations were not edited.

Native receive stages distinguish envelope_parse, providers, sender_authorization,
recipient_account, registry_snapshot, recipient_binding, decrypt_durable_import_receipt
and durable_receipt_ready. The combined decrypt/import/receipt stage does not yet
distinguish every inner operation. It describes the last attempted envelope; an
aggregate pending count can include an earlier failure followed by success.

## Key condition remains unknown — do not silently recover

Real Windows SQLite/DPAPI/CNG tests prove fresh registry provisioning succeeds,
an existing accepted binding is reused without key creation, account mismatch
fails binding validation, and corrupt protected registry fails snapshot loading.
These are native provider fixtures, not Android Keystore runtime execution.

An injected failure writing Active metadata reproduces a separate fail-closed
condition: provisioning intent and Active metadata are in one SQL transaction;
the key provider can create its opaque key before that transaction fails. Rollback
removes the intent while preserving the key. A later attempt sees an empty registry
and an existing alias and refuses to overwrite/adopt it. **This is a demonstrated
failure mode, not a proven explanation of this phone.** No custody fix was applied.
Changing shared transaction/custody behavior requires separate design/integration
review, not an assumption that the phone owns an orphan key.

The next report should distinguish:

- registry_snapshot plus SQL failure: access/locking/schema/provider boundary;
- registry_record_unprotect plus STORAGE_RECOVERY_REQUIRED: protected-record key or
  marker unavailable; preserve both database and Keystore state;
- registry_record_unprotect plus CRYPTO_PROVIDER_FAILURE: authentication/provider
  failure, not proof that data can be discarded;
- registry_snapshot after successful unprotect: native registry decode/metadata
  validation remains a candidate;
- recipient_provision plus recipient_exists state=present and no create: possible
  missing intent/orphan alias; do not adopt or replace it automatically;
- recipient_create/public with fixed reason: unsupported API, inaccessible key,
  policy mismatch or provisioning failure;
- binding_read_validate: existing metadata/account/device/signature mismatch;
- binding_sign/validate/persist: exact signing/verification/protected write boundary.

If protected metadata or an old key is genuinely unrecoverable, stop that action.
A future explicit recovery flow must first explain affected mailbox generations,
retain original registry/envelopes/history, and seek user approval. Reconstructing
metadata could be considered only from authenticated original signed bindings plus
matching existing opaque public key, account and device identity, with current
server authorization and explicit approval. Missing evidence must fail closed.
Creating a new generation must never claim to recover old encrypted messages.
This is a design constraint, not implemented recovery or permission to clear keys.

## Validation and package correspondence

- 202 Android host tests passed; zero failures/errors/skips.
- Lint: zero errors/fatal issues,25 existing warnings.
- ARM64 debug application and instrumentation APK compilation passed. No release
  package produced. Instrumentation was compiled, not executed on Android/ART.
- Real SQLite/DPAPI/CNG key-preparation regression passed, including failed-write
  preservation, corrupted registry and wrong-account rejection.
- Existing native real HPKE and isolated pinned-TLS server regressions passed:
  registration, store/fetch/decrypt/import/signed ACK, independent devices,
  restart, sender offline/logged out, exact retry, tamper, expiry, quota,
  rotation, revocation and receipt persistence.
- Existing reliable endpoint suite passed102 real Schannel/shared-runtime relay
  lifecycles including durable text/ACK/dedupe, authorization and logout gates.
- Independent read-only review found no changed custody/trust/ACK decisions.
- No physical online/offline incoming acceptance performed. No emulator or host
  toolchain reconfiguration attempted; the prior hypervisor limitation remains.

Candidate: `PeerChat-4.0.2-dev.159-inbound-diagnostics-debug.apk`.
Application ID `com.peerchat.android`, versionCode2, versionName4.0.2-dev.159,
ARM64, minSdk26, targetSdk35. Compiled source is exactly the code at b73d035;
subsequent documentation-only checkpoint does not change the APK.
SHA256: `a74a0625175218cd72aed17cec20fba8319bd37187f72c00b30b7118a4fc50fe`.
Verified signing certificate SHA256:
`db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`.
It matches dev.155/dev.158. Signing and package metadata permit a replacement
installation; actual on-device preservation still requires user execution.
Prior accepted/diagnostic packages remain preserved in session outputs.

## Checkpoints and integration manifest

- `c2c7162`: precise native/provider registration and receive diagnostics plus real
  key-preparation regression. Diagnostic correction only; no key recovery claim.
- `b73d035`: independent inbound progress and accurate server/native-state wording,
  with host regressions for lost ACK/reopen, duplicate suppression and later
  outgoing rejection. This is the compiled code checkpoint.
- Final documentation checkpoint is recorded in delivered package-evidence.json.
  Source tree was clean after code checkpoints; documentation is committed too.

Main Work's latest committed manifest was inspected at
`96de4692962f0a8e8ec38e6fa384028f0f200ac5` before shared edits.
Shared modified production files are `mailbox_key_coordinator.hpp` and
`portable/peerchat_core.cpp`. KeyCoordinator gains an optional final diagnostic
callback with fixed enum stages, preserving existing source callers. Existing
C ABI and exported JNI method signatures are unchanged. Android's platform facade
adds beginMailboxRegistration()V; ship Kotlin/native library together. The existing
registrationDiagnostic text gains native_receive_stage; readers must tolerate
additional fixed fields. No wire, crypto format, signing representation, key alias,
registry schema, trust policy, attachment API, server operation or ACK format changes.

Potential merge points remain portable core and the coordinator header; retain Main
Work's attachment/cache additions when manually integrating later. No dependency on
Main TransferManager, attachment encryption, Pi cache or group key handoff changes
was introduced. Apply the diagnostics/code checkpoints together, then focused native
and Android checks, before any later separately authorized integration. No merge
was performed. Main Work Windows dev.158 is distinct from this Android dev.159.

## Physical test instructions — no reset or automatic retargeting

1. Replacement-install dev.159 over dev.158 using the ordinary APK installer.
   Do not uninstall, clear storage, reset identities, revoke accepted Sammy trust,
   or toggle mailbox consent as a repair. Confirm retained chats and version159.
2. Keep Motorola foreground and online. Record its current device fingerprint,
   mailbox consent, native registration stage, first provider reason, inbound line
   and server presence. Confirm the accepted Sammy fingerprint remains trusted.
3. From Windows send a new uniquely named test text to the **current Motorola
   fingerprint**. Record selected recipient fingerprint and Queued/Stored for
   delivery/Delivered state. Do not retarget or re-encrypt existing historical rows.
4. If no receipt, collect the complete messaging diagnostics promptly. The key
   lines are native_registration_stage, provider stage/reason, receive=..., and
   native_receive_stage. A blocked_native_registration line means no fetch was
   attempted, not a successful receive hidden by UI. fetched>0 with native pending
   means a different boundary; imported>0 with absent display warrants projection
   investigation. Report presence/archive/group errors separately.
5. Test offline only with server-accepted storage: disconnect phone networking,
   send a second uniquely named text, confirm Stored for delivery, close Windows,
   then reconnect phone. Record fetch/import/ACK and displayed message; reopen the
   phone app to verify one durable copy and preserved history/trust. Reopen Windows
   to verify delivery receipt reconciliation. If registration still fails, record
   the failure; do not claim offline delivery passed.
6. Send a phone→Sammy control message to confirm outgoing still works, without
   resetting the now-verified contact. Stop and return the diagnostics/results.

Remaining priorities: identify the actual registration phase on the retained phone;
make only its demonstrated non-destructive correction; confirm exact recipient
routing; accept online incoming; accept offline sender-absent import/ACK/restart.
Presence reconnect, archive relay timeout and group TLS closure remain separate,
unresolved transport observations. No broader repair is claimed or scheduled.
