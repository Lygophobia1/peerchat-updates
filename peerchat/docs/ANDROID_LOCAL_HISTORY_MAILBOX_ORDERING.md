# Android local-history / mailbox startup correction

Evidence date: 2026-10-04. Branch: `codex/windows-android-unified`.
Committed base: `db08a7ff3f31bcca9fa0267845171442fe415028`.
The dev.17 correction and required prior mailbox/FCM dependencies are preserved
in the checkpoint described by `DEV17_CHECKPOINT_SCOPE.md`. The base hash above
is not the dev.17 source checkpoint; use the reported checkpoint / Git history.

## Proven source boundaries

`ReliableBridge.history` reads native Storage without consulting a peer route.
The previous application flow populated its conversation list only after the
server bootstrap. The startup UI then covered it while authentication/presence
was pending. Android also admitted peer attempts before mailbox service.

`MailboxBridge.receive` requires current authenticated account/device mapping,
a persisted verified sender signing key, the correct recipient key, and normal
envelope/HPKE/signature validation. It does not require a winning peer session.
Server `FetchMailbox` does not require sender presence or a sender session.

The Windows contact snapshot loop updates each contact's native route plan and
global status text. Actual relay executions have independent worker threads;
no serial network timeout chain across contacts was demonstrated. Windows
mailbox service already runs as its own application task and can be triggered
by the existing server mailbox events. This pass did not redesign Windows
presence/retry ownership or introduce another event/transport channel.

## Implemented ordering and ownership

1. Restore protected local display/account/device metadata for the configured
   server, verify its local device binding, and open the existing native Profile.
2. Publish native history before network authentication. The existing protected
   friend index plus remembered account binding supplies display-only recovery
   for installations predating the new context record.
3. Authenticate and refresh authoritative server contacts/device mappings.
4. Service mailbox fetch/decrypt/signed ACK before NEW peer attempt admission.
5. Publish committed history; the existing notification worker observes it.
6. Ordinary foreground live peer execution continues afterward. A finite
   background wake lease does not admit new peer executions at all.

Quiescence closes/revokes network authorization but keeps the single process
backend's local Profile/history projection. Cold process start uses protected
local metadata. Cached names/device bindings never populate authenticated
friendship or create transport authority. Server refresh still supersedes them.
Offline composer submission invokes native `queueLocal`: it requires an existing
verified matching storage contact and commits through the existing queue/history
logic. It does not open/pump a route or create a Kotlin outbox. Delivery states
and message IDs remain native; local enqueue is not Delivered.

An invalid/unverified envelope is left server-pending, not ACKed. One such item
does not prevent another independently valid item in the bounded fetched batch
from being handled. Successful ACK processing remains bounded to four items per
service pass. Diagnostics expose counts/stage/class only, never key/plaintext.

## Security boundary still pending for unknown/changed devices

No initial/replacement sender trust is invented from cached UI data. The current
server device directory exposes Peer IDs, not a retrievable verified signing-key
record; mailbox key proofs do not supply a trusted sender-signing-key lookup.
An unknown/changed sender therefore remains pending until normal verification
exists. This pass does not implement a new server-published signing-key proof or
silently replace the existing first-pin/trust semantics. Known persisted verified
senders do not need a live TLS session to consume stored content.

This is NOT proof of the exact physical sender-closed failure. In particular,
FCM timing and the existing server hint throttle remain distinct boundaries to
correlate if the next device test fails. No physical success is claimed.

## Executed automated gates

- Android JVM: **82 tests, zero failures/errors**. Includes encrypted display
  context reopen/tamper/server-binding checks, slow/failed authentication with
  visible local history, offline composer dispatch and quiescent snapshot
  retention without fabricated presence/delivery.
- Android debug, instrumentation APK, unsigned release and lint: PASS.
- ARM64/JNI/dependency audits: PASS, including `queueLocal`; destructive native
  test hook absent from release.
- Shared native protocol: 1,000 Hello/identity/wire/durable history cycles: PASS.
- Shared endpoint: 102 real Schannel lifecycle/retry/ACK/dedupe cycles: PASS.
- Portable Profile/Storage/provider/identity/DPAPI/history/outbox/ACK/dedupe/
  protected registry and group-envelope host regressions: PASS.
- Mailbox protocol/server: PASS. The real pinned TLS RPC test stops the sender
  event channel, logs the sender out and clears sender presence AFTER Stored,
  then fetches/ACKs/clears before reopening a sender session to retrieve its
  receipt. No peer transport is created by that harness. Native decrypt/durable
  receive/receipt verification is covered by the separate native protocol part;
  this is not an on-device end-to-end test.
- Full Windows evaluation `build.ps1 -All -HpkeEvaluation`: PASS for
  **4.0.2-dev.17**, including restricted-load/import/group media ABI audits.
- Evaluation package containment/no-publication audit: PASS.
- `git diff --check`: PASS (existing CRLF warnings only).

Added native Android history-before-auth/no-route/unknown-local-enqueue-rejection
instrumentation assertions **compile but are not executed this pass**: phone
unavailable. Do not count them as a physical pass.

## Preserved candidate

Windows: `build/evaluation/4.0.2-dev.17/Package/`.
Android: `build/evaluation/4.0.2-dev.17/Android/`.
The APK manifest reports `com.peerchat.android`, `4.0.2-dev.17`, `arm64-v8a`.

SHA-256:

- `PeerChat-4.0.2-dev.17-debug.apk`:
  `C589CCB0EFC1288B5C9E44CCFD04DD6419B7F19E887F2B73132796B7232EE450`
- `PeerChat-4.0.2-dev.17-androidTest.apk`:
  `B1E4A2A2FEC7C7315EB9FF4AA1886B5AB2476985283303F5E2392D3015C00A5E`
- `PeerChat-4.0.2-dev.17-release-unsigned.apk`:
  `87246C382201F6DCB15E5A13E4C6552066AB74018DA561FAD819938C0E9F8E37`
- Windows packaged `PeerChat.Desktop.exe`:
  `380B49A479A2C62B2A6929775AB06F0242011C3C1C961F5D425A1481435D7827`

Evaluation-only/non-publishable: wolfCrypt shipping license is unresolved.
Older packages, identities, normal server data and unrelated dirty files were
not reset/deleted/replaced. The existing local development server remains dev.14;
it was not restarted by this pass. Host server tests used isolated databases.

## Physical gates when the Motorola returns

Replacement install only (`adb install -r`); no uninstall, clear, identity/key
replacement or Remember Me removal. Keep normal server-storage consent explicit.

1. With the sender offline and network initially unavailable, cold launch and
   view existing local history. Test the offline draft/durable queued send.
2. Background Android; send a numbered message, observe Stored, fully exit the
   Windows sender and verify its process is gone. Recipient must fetch/notify/
   ACK without either UI being reopened. Correlate FCM, mailbox batch and signed
   receipt/cleared ciphertext, not just a prior notification.
3. Repeat while Android is locked/screen-off, at least three numbered messages;
   verify once-only history, ordering and fresh notifications.
4. Reopen Windows only AFTER recipient ACK and verify Stored -> Delivered from
   the durable server receipt. Repeat the sender-closed test twice if practical.
5. Run compiled native-history/profile instrumentation on test-only profiles;
   preserve/verify the normal signing and recipient identities.
6. Verify no-server-storage mode does not upload new mailbox envelopes.

If any stage fails, retain exact wake/server/message-ID correlation. Do not infer
a peer connection dependency merely from contact labels or late UI refresh.
