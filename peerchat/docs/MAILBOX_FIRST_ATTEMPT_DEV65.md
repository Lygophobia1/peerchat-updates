# Windows mailbox first-attempt fallback — 4.0.2-dev.65

The user accepted dev.63 Android login/live messaging, but observed Windows
remaining Queued while Android was backgrounded. Physical retest of this
correction is pending; FCM delivery architecture is unchanged.

## Code gap and correction

The send action requested one mailbox pass. That pass could see a live winner
and skip upload. Later session failure did not request another pass; an otherwise
successful pass could leave the next repair 15 minutes away. Authoritative
disconnected session snapshots now hint the existing serialized mailbox pump.
Hints coalesce in its existing boolean/task gate; no polling or new owner.

The native delivery policy uses only SessionTransportManager snapshots:

- mailbox forbidden: never prepare/upload new envelopes;
- authenticated winner: prefer live delivery;
- first pending/scheduled attempt, without a failure: wait for its existing
  bounded lifecycle;
- first failure, suspended/unavailable route, or no candidate: permit mailbox
  immediately, even if another retry is pending. Online presence is not proof
  of route usability and is not a policy input.

DeliveryManager retains durable outbox, encryption, stable IDs and dedupe.
Server acceptance alone permits Stored; recipient ACK alone permits Delivered.
Live retries remain on the existing session owner if mailbox is unavailable.
Multi-device binding/pin validation and live/mailbox race checks are unchanged.

Safe application diagnostics record policy, message ID, fallback/submission
boundary and Stored acceptance. Existing connection diagnostics record attempt
start/result. A server-owned opaque wake is not falsely claimed as FCM receipt.
No plaintext, credentials or key bytes are logged.

## Settings

Mailbox and audio-processing master controls reuse the existing dark ON/OFF
slider template. History synchronization uses that template too, retaining an
unknown/disabled state until authenticated settings load and retaining explicit
archive authorization. Friendly strength selectors remain discrete. Android
mailbox wording/ON-OFF labeling is aligned. Existing protected saved choices and
defaults are unchanged, including explicit OFF.

## Acceptance

Run mailbox protocol/server, network presence/mailbox lifecycle/offline composer,
shared reliable endpoint/protocol and packaged WPF settings regressions. The
new native test uses the real session owner to prove first-attempt waiting and
immediate eligibility after failure/backoff, including mailbox OFF. Existing
tests cover ciphertext tamper, durable server storage/restart, sender-independent
fetch/ACK, multi-device uploads, races and dedupe. Android JVM tests/builds/lint,
ARM64/JNI/dependency audits and version/package validation accompany the build.

The work-list command explicitly forwards the protected mailbox policy into
native eligibility; a regression verifies both boolean values. Dev.64 was an
intermediate, superseded candidate before this forwarding correction. It is
not overwritten or recommended for this physical test.

Physical retest: use Windows dev.65, mailbox ON on both endpoints, background
Android, send one numbered message and verify Queued -> Stored -> Delivered
and notification without reopening Android. Repeat locked; then send while
Android is unavailable, wait for Stored, close Windows and verify recipient
delivery/ACK independently. With mailbox OFF, unavailable live delivery must
remain Queued without server storage. Do not clear app data or change identities.

## Recorded automated results

- Native policy and actual WindowsMailbox adapter enumeration: passed first
  pending attempt, first failure/backoff and explicit OFF; eligibility does not
  change Queued to Stored. Managed forwarding tests cover both policy values.
- Mailbox crypto/server suite: passed identity/AAD/tamper/device rejection,
  durable store/fetch/signed ACK, sender disconnect/logout/cleanup independence,
  duplicates, restart receipts, multi-device uploads, TTL/quota and revocation.
- Presence/offline composer/lifetime: passed 1,000 presence cycles and 100
  active mailbox cancellation/restart cycles.
- Shared reliable endpoint: passed 102 real Schannel/runtime lifecycles;
  reliable protocol: passed 1,000 Hello/identity/Accepted lifecycles.
- Packaged WPF smoke: passed protected explicit-OFF persistence, corruption
  fail-closed and rendered ON/OFF states; archive consent remains explicit.
- Android: 112 JVM tests, zero failures/errors; debug/test/release builds pass;
  lint zero errors and 14 existing warnings. ARM64/JNI/runtime dependency and
  release fixture/credential containment audits pass.
- Full Windows dev.65 build, package/import/runtime-load audit, shared/generated
  Windows/APK versions, development manifest/hash and dev.63 -> dev.65 safe
  updater extraction passed. No publication or release signature claimed.
- Development ZIP SHA-256:
  `835e26632437e5fa89e0ddbc4d7798b1b297369894aecaed01c5077fd5445a71`.
- Debug APK SHA-256:
  `4ED88BF15B60087A36E215F0AA372E3DCECF6D9378FF4C8EEC30E20EB4B943C8`.

ADB detected no device during this pass. No phone install, identity/storage
mutation or physical notification success is claimed. The physical dev.63
login/live-messaging acceptance remains preserved separately.
