# Network Phase 1 admission / verification correction (dev.22)

Parent: `ee33279863d44cf42514b85badfc5b8ddb9d5cb5` (dev.21), preserved.

## Proven boundaries and fixes

1. **Offline composer:** the model already ignored presence/route readiness, but
   required the currently selected device to be verified. `BindFriend` can select
   another device and reset that projection while keeping historical device IDs.
   A valid authenticated friend-account conversation can now compose and durably
   queue independently of the selected device's trust. Unknown direct peers,
   signed-out/removed accounts and unavailable local storage remain blocked.
2. **Awaiting verification:** native queue admission required a verified contact;
   the removed eager routes had incidentally performed the TLS handshake first.
   Explicit Send or Verify device now requests only that peer's existing manager
   route for 45 seconds. Unverified pending outbox entries do not renew that window
   forever. Real authenticated TLS/Hello and the existing trust/pin policy remain
   required before delivery; local queueing never creates/promotes a pin.
3. **Windows to quiescent Android:** the earliest demonstrated barriers were the
   same composer and native admission gates, before mailbox preparation. If the
   current server device selection is unverified, an existing verified device
   mapped to the same authenticated friend account may remain the queue target.
   Cross-account substitution is rejected. No presence/live-route gate was found
   in mailbox eligibility, and no durable-key deletion on presence expiry was
   demonstrated. Actual per-profile device selection in the reported failure was
   not captured; this is the proven code-level failure, not a claim that every
   physical run used that transition.

Shared admission/recipient resolution is in `reliable_peer_protocol.hpp`, used by
Windows Node and Android JNI. Protected local history/outbox storage and message
IDs are unchanged. A pending unverified destination is only a local reference;
it grants no recipient encryption or transport authorization. Mailbox encryption
still requires the existing verified signing identity and current signed recipient
key binding. Stored requires server acceptance; Delivered requires real ACK.

SessionTransportManager and DeliveryManager remain authoritative. No all-contact
startup routes, selection routes, five-second polling, global reconnect scan,
new retry owner, protocol/schema change, server authorization change, cryptographic
change or media change was introduced. Ordinary mailbox delivery still requires
no incoming relay-demand hint. Existing pending live-delivery demand is retained.

## Automated evidence

- Offline/account composer model: pass, including missing trust, revocation,
  authorization, busy/unavailable profile and unknown-direct-peer rejection.
- Shared reliable protocol: 1,000 cycles plus local admission without pin promotion,
  cleared authorization rejection and same-account/cross-account resolution: pass.
- Relay manager: 1,000 lifecycles plus idle, explicit incoming demand, expiry,
  unrelated-presence/backoff and retirement: pass.
- Mailbox: local offline queue -> Stored -> Delivered; unverified encryption
  rejected; crypto/tamper, multi-device, duplicate store/fetch/ACK, TTL/quota,
  revocation, restart/receipt durability and absent sender session: pass.
- Actual committed/current Node/Schannel lifecycle characterization: pass.
  Reconnect fixture queues before verification, then real relay TLS/trust drains
  that exact ID; asymmetric EOF, P2P handoff and relay media-control regressions pass.
- Production JNI/Profile host TLS: eight reopen/message/ACK cycles and lost-ACK
  restart with unchanged ID/dedupe/history: pass. The fixture now explicitly asks
  for verification demand rather than relying on removed idle eager routing.
- Android: 84 JVM tests, zero failures/errors; debug, test APK, release and lint pass.
  JNI/ARM64, TLS and HPKE artifact/dependency audits pass.
- Full Windows direct build and package/import audit pass. Go HPKE is active;
  wolfCrypt is absent. Shared/generated Windows/Android versions match dev.22.
- Development updater dev.21 -> dev.22 ordering, manifest/hash, safe extraction
  and exact runtime hashes pass. Unsigned Development policy; not published.
- `git diff --check`: pass. Three pre-existing generated compiler objects excluded.

## Artifacts / basic device sanity

- Windows package: `build/development/4.0.2-dev.22/Package`
- Development ZIP: `build/development/4.0.2-dev.22/DevelopmentUpdater/PeerChat-4.0.2-dev.22.zip`
  SHA-256: `3b62d2a1ce485f53b6e4dce3f1014e39d91857327714902a7a7a255f225525a2`
- Android: `build/android/4.0.2-dev.22/PeerChat-4.0.2-dev.22-debug.apk`
  SHA-256: `83946f941b76fb8841feab63401cc7ec294787eea4520a7b8962e15ba6d69a2a`

Replacement install on the same authorized Motorola succeeded. Signing identity,
protected Remember Me and recipient-key registry hashes were identical immediately
before/after install. Local profile opened and eight contacts' local history
projected before network authentication. Windows launched responsive. Initial
Android remembered authentication timed out because no development server was
running; this is not evidence of an event-stream regression. Matching dev.22
ServerAdmin must run using existing server config/data before physical testing.
After the matching dev.22 server was started, safe Android wake logs reported
`authenticated=true`, `presence_live=true`, eight contacts, 22 history rows and
one ready session; a delivery ACK was logged. The activity remains launchable.
This bounded wake evidence is not a foreground event-stream or idle cadence
physical proof; those remain part of the user's validation. Automated demand
and scheduling invariants passed as listed above.

User physical acceptance remains pending: offline send with storage enabled and
disabled; explicit verification; foreground/background/locked mailbox delivery;
sender closed after Stored; exact-once/ordering and receipt reconciliation. No
physical success or locked-device acceptance is claimed by these host tests.
Network Phase 2 remains out of scope.
