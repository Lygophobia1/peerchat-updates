# PeerChat dev183 Android responsiveness and compact messages

## Scope and checkpoints

This candidate separates slow Android refresh work from call control, text and
durable acknowledgments, and makes Windows and Android message details expandable.
It preserves the existing authentication, encrypted storage, transfer and voice
engines. Phone performance and visual acceptance remain a physical test gate.

- Worktree: `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`
- Branch: `codex/native-unicode-emoji`
- Starting checkpoint: `279989d`, preserving dev182 and earlier accepted packages.
- Native hot-path checkpoint: `d6648e5`.
- Android scheduling and readiness checkpoint: `1b52b5c`.
- Native Android fixture checkpoint: `c79ff47`.
- Compact message UI checkpoint: `14dfd31`.
- Visibility-only attachment expansion correction and final candidate source:
  `005d171`. Expanding details reuses the existing panel rather than reconstructing
  controls or repeating filesystem/preview work.
- No Pi deployment, phone installation, account reset, key replacement or server
  protocol change is included.

## Physical evidence and confirmed bottlenecks

The supplied dev182 Android report during group-call preparation recorded a
57,736 ms owner tick, 45,916 ms Contacts stage, 31,396 ms projection stage,
7,573 ms local-page stage and native output lock wait up to 14,157,153 microseconds.
These are overlapping diagnostic maxima, not additive durations of one request.
Recent group-state and invitation RPC durations were approximately 119 and 87 ms;
bootstrap samples were 149–364 ms and mailbox samples 75–118 ms. The evidence does
not support attributing the entire worker stall to a slow Pi RPC.

Code inspection and controlled fixtures confirmed these avoidable costs:

1. The application owner performed refresh and network-dependent work serially,
   delaying unrelated control and publication. A single shared RPC owner also put
   mailbox ACK requests behind metadata/cache requests.
2. A full reliable-message ACK window still decrypted pending messages on every
   pump even though it could not send another message.
3. Completed cache jobs repeatedly revisited delivery evidence despite all required
   signed receipts, or a verified published receiver output, already being present.
4. Unchanged contact snapshots caused redundant protected serialization/writes;
   retained delivery diagnostics and broad projections ran on latency-sensitive
   paths.

The Windows report for the same group session showed only its own room participant,
with capture but no incoming Android media. The 35-second Android error is a UI
watchdog. Worker starvation is consistent with those observations, but the exports
do not establish an exact causal timeline for every failed call. Similarly, the
80,608-byte cached attachment had one 65,536-byte chunk offered and zero durable
bytes; the exports do not demonstrate a cache codec or server disk failure.

## Scheduling and readiness changes

`MessagingBackend` retains its single application owner and existing bounded IO
executor. Single-flight work lanes perform contact/group refresh, eligibility,
mailbox, cache, archive and push work without holding the application gate across
their network waits. The owner processes current control/events and cheap local
publication first. A lane remains occupied until its old operation exits, so
reconnects cannot build an unbounded backlog of stale refreshes.

Each borrowed operation is fenced by the exact native profile, account owner,
session object, account ID, authentication generation and runtime lifetime
revision. Foreground/background transitions invalidate old callbacks even when
the account and session objects are otherwise unchanged. Resume marks readiness
work due again; discarding an old callback cannot suppress refresh for 15 minutes.

`ServerAccounts` uses two existing bounded RPC owners: one for delivery registration,
fetch/store/ACK/receipt work and another for metadata, eligibility and cache work.
This adds at most one persistent authenticated TLS connection per active profile,
not a new authentication protocol. Both lanes retain session binding, reply limits,
timeouts, revocation and pre-write/post-reply checks. TURN remains on its existing
separate path. A latch-based test proves that an ACK can run while the metadata
lane is deliberately blocked.

Protected local conversations are usable before authentication but confer no live
friend, group, device or media authority. Contact snapshots publish cheap metadata
before broad history diagnostics. Exact unchanged protected snapshots avoid writes;
the bounded decoded cache compares encrypted bytes and returns defensive copies.
Presence-only changes do not rewrite unchanged local conversation metadata.

Group metadata is fetched and parsed outside the owner gate, then installed only
against the current scoped native authority. Group/friend change hints fence older
results. Group roster changes invalidate authority before refresh, retain history,
and retire the old voice-room state. Room occupancy is meaningful only with current
native roster/key readiness; stale counts are unavailable, never fabricated zeroes.
Reconciliation is bounded to one due group per pass, 15 seconds for active rooms and
60 seconds for idle rooms, with explicit freshness limits. A newer room roster
cannot restore an older authorization snapshot.

Mailbox publication now projects the durably imported message before ACK submission
without first decrypting all transfer diagnostics. Retained mailbox diagnostic
sampling runs only while idle, at most once per minute, and reports sample age.
Event-driven message invalidation remains immediate; fallback idle history polling
is reduced from ten seconds to sixty seconds.

## Native changes and measured boundaries

| Controlled operation | Before | After | Boundary |
| --- | --- | --- | --- |
| Eight pumps with a full 16-message ACK window | 136 protected message decryptions | 0 redundant decryptions | Native host fixture; the seventeenth message still resumes after ACK |
| Eight completed-sender cache ticks across ten synthetic seconds | 8 evidence revisits | 0 | All required signed receipts already present |
| Completed verified and published receiver cache job | 1 evidence revisit | 0 | Retained output and release handling preserved |
| Blocked ordinary RPC lane with delivery ACK queued | Shared-owner contention | ACK runs independently | Deterministic JVM scheduling fixture, not Internet latency |

`ReliableBridge_requests` also loads one contact snapshot lazily per request batch
instead of repeating that protected read for each peer. Security-sensitive checks
still use the same snapshot and native authority gates.

## Compact message presentation

Both clients retain content, timestamp and concise evidence-based status in the
collapsed bubble. Device names, fingerprints, individual delivery outcomes, routing,
ACK information and transfer measurements remain in the expanded details and
existing exports. Server storage acceptance is not promoted to Delivered. Partial
or unresolved device selection remains distinguishable from complete confirmation.

Windows supports bubble activation and a keyboard-accessible detail toggle; Android
supports tapping, an accessible expanded/collapsed state and a compact chevron.
Actual image/text content and essential attachment actions remain accessible without
opening diagnostics. Cancelled/failed transfers cannot appear actively uploading.
Expansion uses the already-projected row and does not request network information.
No message, transfer-journal or diagnostic persistence format changes are involved.

## Validation record

Validation evidence is retained under `build/dev183-*.log`, existing isolated native
fixture directories and the versioned package audit. The final package handoff lists
the completed test counts and checkpoints.

- Scheduler checkpoint: 458 Android unit tests passed before the compact UI additions.
- Final Android unit and Compose interaction suite: 463 tests across 100 suites,
  zero failures/errors/skips. Covers stale/lifetime fences, roster replacement,
  independent RPC lanes, snapshot write suppression and message expansion states.
- Native reliable-endpoint regression: 102 checks passed, including full-window
  backpressure and subsequent durable progress.
- Real Android JNI and Windows encrypted-cache fixture passed both payload directions,
  two Windows recipients, signed receipts and restart checks. Payload was 256 KiB plus
  17 bytes. Its server RPC adapter is hosted by the Windows fixture; it does not
  measure production Android owner-loop or Pi network latency.
- Genuine host voice/media regression passed authenticated TLS, Opus/ICE/DTLS-SRTP/
  NetEq, answer consent, mute/deafen, replay/generation recovery, repeated reverse
  calls, concurrent 2,097,275-byte v2 transfer with integrity verification, and durable
  text ACK. The audio device was synthetic; this is not physical Android audio or
  TURN acceptance.
- Old native instrumentation expectations were corrected to parse existing group
  history trailers and reject cross-account identity rebinding. No production
  provenance rule was relaxed to satisfy a fixture.
- Emulator JNI checks passed profile identity/storage/reopen, signed group history
  retry/deduplication/tamper/revocation, unavailable-media authorization, Keystore
  cache registration and original-account key preservation. The full run had one
  obsolete same-account scoped-enrollment assertion; its corrected targeted rerun
  passed. Ten other tests passed and the separate-process fixture was initially
  skipped until its explicit driver ran. All five driver phases then passed:
  seed, pending, ACK, verify and isolated cleanup, each in a new instrumentation
  process.
- Offline startup fixture used fresh isolated profiles with protected local contacts
  and real JNI history. For 128 contacts, first backend restore was 152 ms, reopen
  85 ms and selected-history open 104 ms. For 96 contacts: 70 ms, 82 ms and 98 ms.
  The profile had just been seeded in the same process: these are not process-cold
  start, network authentication or physical-device measurements.
- Windows tests passed compact bubble click/collapse and action exclusion, delivery
  projection/legacy history, DM/group attachment UI, 608-row protected attachment
  history paging, transfer controls, automatic verified saving, durable selected
  filenames, restart, collisions, overwrite consent, unavailable destination/retry,
  and unified History/Logs diagnostics read-only ownership and export boundaries.

### Reproduction

Use the existing pinned Android NetEq directories and NDK; no dependency was replaced.
The final ARM64 command is `gradlew.bat :app:testDebugUnitTest :app:assembleDebug
:app:lintDebug -PpeerchatVoiceEnabled=true
-PpeerchatVoiceReceiveDirectory=../../build/android-neteq/arm64-v8a
-PpeerchatVersion=4.0.2-dev.183 --offline --max-workers=2`.
The isolated emulator build adds `-PpeerchatVoiceIntegrationTest=true
-PpeerchatTestAbi=x86_64` and uses `../../build/tests/neteq-x64-ndk27`.
Its application ID is `com.peerchat.android.voiceintegration`; no production phone
application is installed or cleared by these tests.

Windows uses `build.ps1 -All`, followed by the same managed compiler/resources and
generated dev183 version for the reviewed bubble event-routing correction. Logs are
`build/dev183-windows-build.log` and `build/dev183-managed-final-build.log`.
Native screen build objects are retained in `build/dev183-native-objects`.
`scripts/package-messaging-repair.ps1` audits source checkpoint, version, unchanged
Android signing identity, ARM64 ABI, Windows dependencies and each ZIP entry hash.

The legacy Windows-host Android-profile runner could not compile because its runner
did not provide `opus/opus.h`; no assertions ran in that runner. Production NDK
ARM64/x86 builds and real emulator profile/JNI tests cover the native code instead.
Existing FCM deprecation and Windows unawaited/unused-field compiler warnings remain.

### Validated packages

Final Android ARM64 build, all 463 tests and lint passed. Windows build and final
managed UI regression pass are complete. The clipboard exact-content test needed
a bounded retry for Windows `CLIPBRD_E_CANT_OPEN` contention; its content assertions
remain unchanged and passed. The package audit passed version, source checkpoint,
signer, ABI, restricted native dependency loading and every ZIP entry's byte hash.

Both packages are under
`C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat\build\development\4.0.2-dev.183`:

- Windows: `PeerChat-Windows-4.0.2-dev.183.zip`, 12,523,929 bytes.
  SHA-256: `3decfd6be4c105b8d3247d63d75f89bc0078f4b3f886cbacba8f21274840705e`.
- Android: `Android\PeerChat-Android-4.0.2-dev.183-debug.apk`, 28,385,932 bytes.
  SHA-256: `f9ade9c70e7240b00033a9b38fa3261a8b71ed7b47669cdd8dcbc598faf8fd00`.
- Audit: `messaging-repair-package-audit.json`; source checkpoint `005d171`.
- Android signing certificate SHA-256 remains
  `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`.

The Windows runtime is also available in `Package\PeerChat.Desktop.exe`. Only
the isolated emulator test application was installed during validation; the physical
phone and live Pi were not modified. Earlier versioned packages remain intact.

## Remaining limitations and tradeoffs

Initial security-critical authentication/bootstrap and explicit account-management
operations remain serialized. This milestone does not remove every application-gate
or native-storage wait. Native route-demand inspection can still read a large pending
message set; idle retained diagnostic sampling can still be expensive. These should
be measured separately before further optimization.

The additional delivery TLS lane costs a socket and bounded connection memory.
Decoded protected snapshot caching adds one bounded entry per store instance.
Less frequent idle reconciliation reduces repeated work but can delay a missed-event
fallback. Conservative group-roster invalidation can interrupt an active group call
until current authority is verified again; it must not preserve stale authorization.

Authentication-to-interactive time, group-key/room readiness, call setup, main-thread
stalls, lock waits, ACK latency, CPU/memory and battery improvements on the physical
phone still require measurement. Host operation-count improvements must not be
reported as a measured 57.7-second-to-fast phone startup improvement.

## Physical acceptance checklist

1. Update Windows and Android in place without clearing data. Confirm dev183 and the
   existing identities, history, linked-device names and queued messages.
2. Compare cold launch, warm launch and offline reopen; open a conversation while
   contact/group refresh is active. Export full diagnostics after any noticeable stall.
3. Send in both directions during History Sync and a file transfer. Confirm local-first
   composer behavior and truthful per-device receipts in expanded details.
4. Join a group call from both devices, inspect fresh room occupancy, repeat the call,
   and test background/file-picker return. Confirm audio, not just room membership.
5. Test cached and direct files, cancellation and retry; confirm integrity and normal
   Save/Open behavior. Cancelled bubbles must remain inactive.
6. Click/tap short, long, incoming, outgoing and attachment bubbles. Check collapsed
   density, expansion/collapse, text selection, links, image previews and keyboard
   navigation without blocking new messages.
7. Check offline/partial/unavailable device outcomes, failed save/transfer and completed
   downloads. No server-only acceptance should say all devices received.

Stop at this acceptance gate. No live Pi changes or phone installation are authorized
by this handoff.
