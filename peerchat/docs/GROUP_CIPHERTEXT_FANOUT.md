# Stage 1C: online group ciphertext fanout

## Ownership and confidentiality

The existing authenticated discovery event connection carries group text in both
directions. `GroupCiphertextFanout` is a bounded server application handler, not
a transport/session owner. It owns no sockets, listener, reconnect worker,
SessionTransportManager, database, keys or offline outbox. Server roster ownership
remains `AccountDatabase`; the existing event-channel lifecycle owns sessions.
GroupControl peer TLS remains the key/membership-control path, never a plaintext
chat channel. Neither friends nor a client-to-client mesh are required for text.

The server reads only fixed routing fields of the **unchanged Stage 1B PCGE v1**
envelope. It binds sender account/device to the authenticated event subscription,
checks current roster/key generation, and selects current authorized roster-device
sessions itself. It neither decrypts nor validates the private cryptographic body.
Recipients verify the device signature, membership, generation and AES-GCM tag
using Stage 1B before committing history or displaying text. The server does not
receive group keys, plaintext, unsigned body timestamps or UI sender names.

## Existing channel messages

The initial `events` subscription is unchanged and still bounded to 4 KiB.
Established event-channel frames are bounded to **100,000 UTF-8 JSON bytes**;
ordinary one-shot requests remain 4 KiB. The envelope maximum is 65,790 bytes
(87,720 Base64 characters). All group messages use this already-authenticated
TLS channel, with its existing heartbeat/lease and reconnect handling.

- `group_send`: `groupId`, `rosterGeneration` (decimal string), `messageId`,
  `envelope` (Base64 opaque PCGE bytes). The ID must match the envelope. Supplied
  `recipients` is explicitly rejected. Sender identity comes from the session.
- `group_message`: `groupId`, `messageId`, identical Base64 `envelope`.
- `group_ack`: `groupId`, `messageId`, `state` = `accepted`,
  `duplicate_message`, or `rejected`. Sent only after native receive validation
  and protected history commit (or durable replay detection).
- `group_delivery`: identifiers, bounded state and counts: `sessionCount`,
  `acceptedCount`, `duplicateCount`, `rejectedCount`. No plaintext or key fields.

`server_accepted_online_only` means server admission and successful writes to
eligible online event sessions, **not end-to-end delivery**. The sender receives
recipient-reported validation counts separately (`recipient_acknowledged`). ACKs
are authenticated server-session reports, not independently signed read receipts.
There are no read receipts. Missing ACK means unknown recipient acceptance.
Repeated terminal ACKs are idempotent and cannot downgrade an earlier receipt.
Protected client summaries also retain their explicit progress stage: a delayed
server-admission/disconnect notification cannot overwrite a recipient receipt.

## Limits, reconnect and dedupe

- Fanout has at most 64 current member accounts/roster devices per group.
  Larger rosters fail visibly instead of partial unauthorized selection.
- The server holds at most 128 recent submissions for 120 seconds, in memory,
  containing opaque wire bytes and safe session/receipt metadata only. Capacity
  exhaustion fails closed. This is a bounded dedupe/receipt window, **not** an
  offline queue. Server restart loses this window, not client durable dedupe.
- The client has at most 32 pending/queued sends, and drains at most eight per
  existing event-loop iteration. Disconnect clears unsent/uncertain requests and
  exposes `delivery_unknown_after_disconnect`; no hidden offline retry is added.
- New/reconnected sessions receive subsequently sent messages, not a replay of
  messages they missed. Replaying an identical request within the server window
  returns its existing summary without retargeting new sessions. After expiry or
  server restart, recipient Stage 1B durable dedupe still prevents double display.
- Sender renders its authenticated protected local copy once, and is excluded
  from server fanout to its same device. An account's current roster authorizes
  the specific member device, as established in Stage 1A; other account devices
  do not implicitly inherit membership. One current event subscription per
  authorized account/device is selected; stale/replaced bindings cannot ACK.

No server offline/durable queue, automatic client retry or historical delivery to
new members is claimed. Sender history can contain a message that never reached
any recipient; its label remains server/recipient state, not `Delivered`.

## Membership changes

Admission checks the current roster. Each recipient write rechecks both sender
and recipient authorization under the roster mutation lock and checks its live
authenticated session/lease. A mutation between writes prevents further writes
of that old-generation envelope. A previously admitted write may already have
completed: membership cannot revoke bytes already delivered. Recipients still
reject delayed old-generation ciphertext after applying the new roster.
Removal rotates keys through Stage 1A; removed members receive neither current
keys nor future-generation fanout. No new member obtains historical keys.

## Private text body and history/UI

Inside AEAD only, `PeerChat.GroupText.v1` uses the existing bounded protocol
writer/reader: Text discriminator, domain string, positive millisecond timestamp,
positive logical order, and strict UTF-8 text (at most 4,096 UTF-16 code units).
Text framing validation occurs after crypto authentication **before** atomic
history/dedupe commit. Invalid bodies cannot poison a valid message ID.

Protected history retains the existing DPAPI-wrapped envelope/body records.
An identifier-only index supplies a monotonic local pagination cursor; no message
content/key is added to plaintext SQLite columns. Delivery summaries are also
DPAPI-protected. Reads after restart are previously authenticated local history,
not a bypass of live current-roster checks. The UI renders owner-provided events,
sender identity, timestamp, unread state and honest delivery/error labels. It
does not perform crypto, choose recipients or own reconnect.

## Automated and physical validation

`test-group-ciphertext-fanout.ps1` hosts an isolated real TLS server and three
non-friend authenticated event channels, plus deterministic routing/epoch/ACK
cases. Its routing fixtures deliberately contain opaque synthetic bytes; it
does not claim they decrypt. `test-group-message-envelope.ps1 -IncludeStage1A`
separately runs real native crypto and three-device key handoff + authenticated
text/decrypt/history/dedupe tests through the production native dispatch boundary.
`test-group-text-ui.ps1` checks the compiled presentation model, sender labels,
group isolation, replay suppression, local cursor and delivery wording.

Physical candidate procedure (update server **and** all clients): create a new
authoritative group, invite/accept three non-friend accounts, wait for current key
handoff ACK, send from each, confirm each peer displays once, restart and inspect
history, remove a member, wait for rotation/ACK, and send again. Removed member
must receive no future messages. Disconnect/reconnect one member: missed messages
are explicitly unavailable; new messages must arrive once. Group voice/screen
sharing remain out of scope. Physical acceptance is still required.
