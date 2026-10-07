# Sender-disconnect mailbox trace — dev.18

Investigation on 2026-10-07, isolated `codex/go-hpke-production`, starting
checkpoint `42cb799`. No production source, identity, key, protocol, package or
server configuration was changed. The existing development server remained
running from dev.17; the focused server regression compiles current dev.18 source.

## Physical message correlation

The user identified `test6` as the sender-closed failure and `test5` as the last
visibly accepted message. A read-only normal-profile instrumentation probe found
the exact test-label match in Android native encrypted history:

- Message ID: `3865bf474e0740939f3db94031f75ed6`.
- Sender account: 8; recipient account: 19.
- Sender device prefix: `7927a46cbe1c`; recipient: `61f70fb01a60`.
- Server creation: 2026-10-07 05:00:14.860 UTC.
- Server receipt expiry minus the fixed seven-day receipt retention gives ACK
  update time 2026-10-07 05:00:31.669 UTC. There is no separate ACK timestamp
  column; this timestamp is derived from the actual AckMailbox update logic.
- Row state: receipt-only, 318-byte signed receipt; ciphertext NULL.
- Recipient binding remains active, generation 2, storage allowed. Recipient push
  registration and remembered authorization remain present; sender device offline.

The server accepts that receipt only after device/signature/digest validation.
Android creates it through native durable acceptance. The independent native
history read confirms this message exists, not merely that a receipt row exists.
This disproves missing durable acceptance for this specific message. It does not
reconstruct the exact sender process-exit timestamp or prove when a notification
was posted; a contemporaneous physical before/after timeline remains unavailable.

## Proven history projection boundary

The protected local conversation binding currently selects device `68b644245788`
for account 8 and retains two known device IDs. Native history has:

- Selected device `68b644245788`: 0 messages.
- Historical/sending device `7927a46cbe1c`: 90 messages, including the exact ID above.

`MessagingBackend.publish -> nativeConversationProjection` calls native history
only for `friend.peerId`, not the account's other known device bindings. The server
`Profiles` projection selects a presence-associated device when available, otherwise
the first registered device. Sender departure can therefore change the selected
device without removing any stored history. This is a presentation dependency on
current device selection, not a need for live peer TLS to read native history.

Wake notification planning also consumes `ConversationSnapshot.messages` and
computes incoming sequence watermarks from it. An empty selected-device projection
can hide an accepted mailbox message from notification planning. Historical logcat
for this exact wake was not available, so the notification-specific causal chain
is not claimed as fully observed.

## Server lifecycle proof

The strengthened `encrypted_mailbox_server_test.cs` takes assertions/safe stage
snapshots before sender disconnect, after event-channel close plus authenticated
logout, and after explicit sender presence cleanup plus normal mailbox purge.
All three preserve ciphertext, recipient binding, fetch eligibility, recipient
push registration and pending receipt state. A disposable held push provider
starts before disconnect and completes after disconnect. Recipient TLS fetch and
signed ACK succeed before sender reauthentication; sender later reconciles its
durable receipt. The test does not use real FCM or claim physical push timing.

Source trace:

- Event close removes that event subscription and its presence; room state is
  disconnected separately. No mailbox/push deletion is invoked.
- Relay close retires relay endpoint/paired socket ownership, not mailbox rows.
- Session logout deletes the session, not the device/key or recipient queue.
- Mailbox purge removes TTL-expired or explicitly removed-device rows, not rows
  whose sender is merely offline.
- Fetch uses recipient device ownership, key state, friendship/block state; it
  does not join sender presence/session/event/relay tables.
- Push dispatch captures recipient account/device/token and checks only recipient
  token continuity/global server stop, not sender lifetime.
- Receipt reconciliation is durable and independent of sender event delivery.

## Validation and disposition

- `tests/test-mailbox-protocol.ps1`: PASS, including the new lifecycle snapshots,
  held wake, server restart, signed ACK, quota/TTL/revocation and native protocol tests.
- Android `assembleDebugAndroidTest` offline build: PASS using the installed SDK.
- Replacement install of instrumentation APK only: PASS; production dev.18 APK,
  normal profile, signing/recipient identities and history were not replaced.
- Opt-in `MailboxHistoryProbeTest`, account 8/test-label match: PASS. It opens the
  existing normal native profile and reads at most 32 eight-message pages per
  known device. Output contains counts, public ID prefixes and matching message ID,
  never message contents, tokens or key bytes. It performs no login/receive/ACK.
- `git diff --check`: PASS.

No sender-lifetime workaround or production fix was applied. The next bounded
correction is account-scoped history/notification projection over existing known
device bindings, preserving native storage/trust/ACK/dedupe ownership. Sequence
watermarks and pagination need explicit regression coverage; do not blindly merge
device histories while assuming sequence numbers are interchangeable. Physical
notification/locked/offline-sender acceptance remains incomplete until corrected
and retested. There is no evidence requiring a mailbox crypto or transport redesign.
