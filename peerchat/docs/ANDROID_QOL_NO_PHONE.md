# Unified Android QOL candidate — 2026-10-03

## Checkpoints and preservation

Accepted physical Android messaging: `5481933d4678f0a33480e9522d3e25c697af4fbf`.
Unified pre-QOL merge: `a8bab7b`, branch `codex/windows-android-unified`.
This branch starts from Windows `2a5b721`, not from the Android lineage.
Neither rollback worktree nor authoritative main has been advanced. The Windows
dev.64 reference and its excluded four modified/six untracked entries remain.
See `UNIFIED_PRE_QOL_BASELINE.md` for the broad pre-QOL regression evidence.

## Ownership and implemented behavior

Remember Me uses a dedicated device-bound, signed-challenge credential, not a
stored password or stored Online boolean. See `ANDROID_REMEMBER_ME.md`.

UI and bounded JobService work lease the same application MessagingBackend and
native Profile. SessionTransportManager still owns sessions/retry/winners;
DeliveryManager still owns history/outbox/ACK/dedupe. Last active application lease
quiesces native execution and closes Profile. No background foreground service,
permanent socket, Kotlin message database or second transport owner is added.

Push registration is authenticated and device-bound, persistent, length-bounded,
replaceable and expires after 30 days. Logout revokes it. Provider rejection can
remove an invalid token only if it has not been replaced. Wake work is coalesced
and bounded; shutdown excludes late database access. Push contains only the fixed
opaque `peerchat_pending`/version hint, never message content or trust material.
Existing Android/Windows senders emit an optional hint only after native durable
queue acceptance; failures never alter delivery state.

The notification JobService uses normal authenticated presence, relay and native
delivery. Only real native received messages can produce notifications. It uses a
30-second coroutine budget plus existing bounded connection operations, releases
its application lease afterward, and guards replaced/cancelled job callbacks.
Notification permission/channel, private lockscreen visibility, conversation tap,
per-conversation grouping and sender-only default privacy are implemented.

Protected PCR1 preference records store privacy settings, account/server-scoped
drafts and sequence cursors only. They do not copy message history. Drafts save
after a 250-ms debounce. Abrupt termination before that save can lose the final
keystrokes. Unread/notification counts cover the current bounded native history
window (eight messages), not an exact all-history unread total. Scroll positions
remain session-local. Already accepted queued sends retain native retry ownership;
unaccepted sends preserve the draft and allow retry using the existing Send action.

## Explicit deployment gaps

### Post-push QOL backlog (not part of FCM debugging)

Android optimistic/local-echo sending: after **native durable outbox acceptance**,
project the message immediately with Sending/Pending state. Session establishment
and delivery remain asynchronous under SessionTransportManager/DeliveryManager.
Only a real ACK changes it to Delivered. Failed local enqueue preserves the draft
and reports failure. Do not fabricate delivery or create a Kotlin outbox.

The initial candidate had no concrete/configured Firebase provider. The current
follow-up adds Google Services 4.5.0, Firebase BoM 34.19.0, Messaging 25.1.3 only
(no Analytics), a FirebaseMessagingService and concrete HTTP-v1/OAuth provider.
Android uses an ignored local `google-services.json`; server service-account
credentials remain external and are read in place, never copied or packaged.
FCM stays disabled by default; the actual development server must explicitly
configure `FcmEnabled`, `FcmProjectId` and `FcmServiceAccountPath`.

A real opaque HTTP-v1 send was accepted and received on the authorized Motorola
(Android 16/API 36), producing `fcm_received` and `wake_scheduled` breadcrumbs.
This proves provider/device delivery, **not full background notification acceptance**.
The active development server still needs the new provider configuration and the
Windows-message → native receive → notification/tap/ACK/dedupe physical test.
The historical unclassified IOException cannot be diagnosed retroactively; new
bounded request/stage correlation must be collected from that updated server.

Firebase introduces AndroidX DataStore 1.1.7's ARM64 shared counter library. Its
packaged bytes are checked against the exact upstream hash, with unchanged peer
TLS/native ABI allowlists. The external credential file is excluded from all
client/server packages.

There is no new server offline DM mailbox. The existing sender-owned encrypted
native outbox is authoritative; wake/reconnect can drain it while a sender is
available. Both-offline delivery still waits for normal sender reconnect. Group
offline push/mailbox behavior is not added. Android force-stop may suppress all
background work until manual launch; ordinary dismissal is the intended future
push case. QOL physical results have not been claimed.

## No-phone validation

- 56 Android host/UI tests: encrypted remembered state, 100 record reopens,
  tamper/context failures, pin prerequisite, presence-gated auth, drafts/cursors,
  account-scoped notification taps, privacy/dedupe and 1,000 application-lease
  operations (not a new claim of physical native lifecycle testing).
- Offline debug, instrumentation-test and unsigned release APK builds; lint pass.
  Existing ARM64-only/intentional certificate trust-manager lint warnings remain.
- ARM64 dynamic dependency/JNI/TLS module/license/test-hook/secret-fixture audit pass.
- Production remembered-login server test: wrong device/signer/replay/scope,
  expiring access session, 32 resumes, DB reopen, credential replacement, explicit
  revocation, deleted-device failure, push persistence/replacement/coalescing and
  shutdown. Normal Windows account-token behavior remains tested.
- Full Windows direct build and restricted package/import audit pass at unified
  `4.0.2-dev.1`; Client/Server/Admin/Updater/Package and Android versions match.
  Windows `4.0.1-dev.64` remains the final pre-Android baseline. Earlier local
  `4.0.1-dev.1`–`.4` merge-proof outputs are stale metadata, not unified version
  history, and are not publication candidates.
- Pre-QOL and follow-up discovery/heartbeat/reconnect, real shared JNI/TLS queued
  delivery and group fanout/room regressions retain the accepted media architecture.
- No ADB commands, phone installation or physical QOL validation occurred.

## Candidate artifacts

Worktree: `C:\Users\Livel\.codex\worktrees\windows-android-unified\Create 2D Game\peerchat`.

- `android/app/build/outputs/apk/debug/app-debug.apk`
  SHA256 `B5F9E181B485C6C391F765AA647BD1F144DD0C50C0DCB330644D89D5D68D9202`
- `android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk`
- `android/app/build/outputs/apk/release/app-release-unsigned.apk`
  SHA256 `AE2DD8AA05151E5BB065D7CB833ABB6DC7447C99D87B3E13556E6240CC8375C2`
- Windows: `build/development/4.0.2-dev.1/{Client,Server,ServerAdmin,Package}`.
  No development updater publication or authoritative-main merge is performed.

Local prepared updater ZIP:
`build/development/4.0.2-dev.1/DevelopmentUpdater/PeerChat-4.0.2-dev.1.zip`.
SHA256 `49E850A33146BCC8711751CB78760EB642D3D715FB83D652E4D0F23386D7AA1F`.
Sibling `manifest.json` declares Development, `4.0.2-dev.1`, and upgrade floor
`4.0.1-dev.64`. It is unsigned under the existing Development-only policy;
no release signature is claimed and prospective URLs have not been published.
Version consistency, exact extracted runtime hashes and production updater safe
extraction pass via `tests/test-development-version.ps1`. Updater hash/signature,
downgrade, interrupted-transaction and first-launch rollback tests pass (42 assertions).

## Morning physical checklist

1. Reconnect/unlock the same Motorola and authorize debugging. Replacement-install
   debug APK only; do not clear data, uninstall or replace the normal identity.
2. Use an isolated unified Server build with the existing trusted fingerprint.
   Verify the same Android identity, profile/history and saved host/port/pin.
3. Explicit login with Remember Me off; cold launch requires login. Enable Remember
   Me with verified pin; cold launch/process kill/reboot securely resumes and only
   reports Online after real presence. Wrong pin must fail closed.
4. Exchange Android/Windows messages, observe ACK, dismiss/reopen and reconnect.
   Exercise queued offline delivery with the sender available and verify dedupe.
5. Verify protected drafts after waiting for the debounce, failed queue preserves
   drafts, conversation scroll, bounded unread indicators and account separation.
6. Verify notification permission and three privacy modes using controlled wake
   injection/tests. A hint alone must produce no notification; a real new native
   message may. Tap opens only the correct account/conversation. Verify UI/wake
   overlap does not create a second Profile/session owner.
7. Test online logout and offline logout/relaunch retry: remembered credential must
   not auto-authenticate after revocation-pending state. Reenrollment/deleted device
   must invalidate old credentials. No private-key export or sensitive logs.
8. Inspect logcat for JNI/native/provider/TLS/storage errors and content/token leaks.
   Do not claim real background FCM delivery until its configuration and concrete
   adapters are supplied and validated separately.

Physical QOL and FCM deployment gates remain. Do not merge this candidate into
authoritative main solely from these host results.
