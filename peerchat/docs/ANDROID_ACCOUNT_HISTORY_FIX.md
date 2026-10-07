# Android account history / notification projection — dev.19

Based on preserved Go integration checkpoint `42cb799` / dev.18. No merge,
identity/key rotation, storage migration, protocol, server schema, media or
Windows HPKE provider change.

## Fix

The contact's protected existing known Peer IDs define the eligible local-history
read set. Current device remains authoritative for current routing/presence, not
for choosing which already-accepted historical messages exist in the conversation.
Initial projection and Load older both query native Storage for up to eight rows
per known device (maximum 32 devices), dedupe logical message IDs and return the
eight newest native rows for the account. Rendering is deterministic by timestamp,
logical order, message ID and device attribution. No messages are copied/migrated,
renumbered or given invented identity/delivery state.

Pagination/notification watermarks retain native sequence values: `Storage::history`
selects `messages.rowid` from ONE profile database. These are global local insertion
sequences, not unrelated per-device counters. Selecting the globally newest eight
before advancing the shared cursor avoids skipping messages between device pages.
Presenter older/current caches use the same chronological/deduplicated ordering.

Notifications consume the corrected account conversation and additionally dedupe
logical IDs. Existing protected read/notified watermarks remain unchanged in format.
Wake completion no longer requires a displayed notification: the existing native
durable receive/ACK/output completion readout is sufficient even when notifications
are disabled, denied, suppressed or already posted. Completion is sampled before
the notification-planning snapshot so a concurrent ACK cannot terminate the worker
before planning against the corresponding completed owner projection. A stale
historical message alone still cannot complete a fresh wake.

## Exact changed files

| File | Purpose |
| --- | --- |
| android/app/src/main/java/com/peerchat/android/application/MessagingSnapshots.kt | Bounded account history page, stable chronological/deduplicated projection |
| android/app/src/main/java/com/peerchat/android/application/MessagingBackend.kt | Load older uses the existing account's known-device set |
| android/app/src/main/java/com/peerchat/android/application/MessageNotificationPlan.kt | Logical-ID notification dedupe |
| android/app/src/main/java/com/peerchat/android/ui/NativePresenter.kt | Consistent merged old/current message ordering |
| android/app/src/main/java/com/peerchat/android/platform/PeerChatWakeJob.kt | Native completion independent of notification display; snapshot ordering |
| shared/VersionInfo.cs | Coherent source/APK version 4.0.2-dev.19 |
| android/app/src/test/java/com/peerchat/android/application/NativeProjectionTest.kt | Two focused projection/notification/pagination regressions |
| android/app/src/androidTest/java/com/peerchat/android/MailboxHistoryProbeTest.kt | Opt-in read-only existing-profile characterization, including corrected combined view |
| tests/encrypted_mailbox_server_test.cs | Prior investigation's bounded sender-disconnect/held-wake regression evidence |
| docs/MAILBOX_SENDER_SESSION_COUPLING_TRACE.md | Prior investigation evidence; no server-lifetime defect was demonstrated |
| docs/ANDROID_ACCOUNT_HISTORY_FIX.md | This implementation/build/install record |

Generated object files remain excluded. The read-only physical probe requires
explicit arguments and is not a second history database or production owner.

## Lightweight validation

- Android offline `testDebugUnitTest`: 84 tests, zero failures/errors, including
  the two new regressions (device selection change, notification selection,
  combined pagination without skipped rows, stable tie/dedupe ordering).
- `assembleDebug`, `assembleDebugAndroidTest`: PASS; relevant native build tasks PASS.
- Lint: PASS, 0 errors / 14 existing warnings.
- Sender-disconnect server/native mailbox regression: PASS during the preceding
  investigation, including ciphertext/binding/push survival and signed receipt
  reconciliation. No new server production change in this fix.
- `git diff --check`: PASS; scoped diff inspected. Production Windows still calls
  `createShippingHpke`; no active wolfCrypt source/dependency returned.
- Replacement installation of debug/test APKs: PASS; installed dev.19 confirmed.
  No uninstall, data clear, profile wipe, identity/recipient rotation or Remember
  Me change. Before/after public identity, protected registry database and
  remembered credential checksums matched exactly.
- Motorola existing normal profile probe: PASS. Selected device `68b644245788`
  still has zero native rows; known sender `7927a46cbe1c` has 90. The corrected
  combined eight-message projection includes the exact previously accepted
  `test6` ID `3865bf474e0740939f3db94031f75ed6` without a peer connection/login/fetch.
- MainActivity launch: PASS. No new messages or physical delivery matrix executed.

## APK / next gate

Immutable copied candidate: `build/android/4.0.2-dev.19/app-debug.apk`.
SHA-256: `3BC6D79C21BA7D5A100918B6820F2091DF843924334272FB7613B980C036A206`.
The matching test APK is archived alongside it. Normal Gradle output also exists
at `android/app/build/outputs/apk/debug/app-debug.apk`.

Windows dev.18 and the existing server are preserved; this Android-only correction
does not require their replacement. The shared source version is dev.19. No new
Windows package/full media regression matrix was run for this bounded Android fix.
Foreground/background/locked delivery, sender-closed notification behavior,
ordering/dedupe and older-history usability are left to user physical acceptance.
