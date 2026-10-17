# Online attachment compatibility repair

The dev.174 Windows and Android candidate restores verified online transfers
without weakening encrypted-cache admission. Physical acceptance is pending.
The Pi remains on dev.173; this repair does not deploy or change the server.

## Confirmed cause

Dev.173 selected encrypted cache for account conversations and required every
eligible recipient key to register the new cache capability. That strict rule is
correct for account-wide offline delivery, but the clients no longer offered the
already working direct-transfer path when a secondary device lacked capability.

Read-only server metadata confirmed the updated phone's capability was registered.
Another authorized Windows PHONE device was not registered. Other accounts also
had mixed-capability devices. The failure occurred before ciphertext upload; it
was not evidence that the phone update or its receive worker had failed.

## Delivery behavior

Encrypted cache remains preferred. A bounded request through the existing pinned,
authenticated server connection checks readiness before a new upload is queued.
The native cache controller still independently validates its fresh signed audience
and full authorization; a UI preflight is never an upload grant.

For an explicit compatibility rejection, the sender can choose **Send Directly to
Online Device** or **Cancel**. The dialog identifies the selected device and states
that other linked devices will not receive this transfer and no offline cache copy
is created. Direct delivery is unavailable without a verified online route. Cancel
leaves the attachment unsent and preserves the image draft/private imported source.

The current server reports a capability rejection, not individual affected-device
capabilities. The UI says those identities were not provided; it does not guess
which device is old or display invented capability measurements.

Authorization failures, stale scopes and network errors do not silently select
direct delivery. Existing paused cache attempts are not converted, canceled or
deleted. If the user reattaches the same file directly, the earlier cache attempt
remains independent; cancel it explicitly if a later cached copy is not wanted.

## Security and shared interfaces

- Windows uses an additive local `online:<canonical account>` selector on existing
  file operation 21. It checks current authenticated account mapping, exact verified
  peer/TLS key, and a live winning non-archive direct-message/file-capable connection.
  It then delegates to existing operation 1 with a fresh transfer ID.
- Android adds a local probe/commit action through the existing TransferBridge
  signature. It binds the pending choice to the current owner, account, generation
  and exact peer, and repeats native checks at commit. A narrowly scoped internal
  ReliableEndpoint predicate exposes current direct-file readiness, not new authority.
- Matching Desktop/Core and Kotlin/JNI artifacts must be packaged together.
  Other-project integration manifests were inspected read-only before these local
  interface additions; the other worktree was not edited or merged.
- No peer/server packet, encryption format, HPKE audience, grant, revocation,
  transfer-journal schema, file limit or network window changes are made.
- Existing TransferManager v2 owns negotiation, ACKs, bounded windows, durable
  progress, integrity verification and reconnect/resume. Completion still requires
  valid recipient confirmation, not merely a successful offer or upload.

## Validation evidence

- `build/dev174-cache-authorization-regression.log`: strict mixed-device cache
  admission, independent offsets/receipts, revocation and cross-account rejection.
- `build/tests/media-transfer-4113c32f57734d29aa833d64cff03bf1`: real Node peer TLS,
  explicit direct transfer with an unavailable older linked device and no cache
  session; wrong account, noncanonical selector, offline and unverified peers
  reject. Exact 10 MiB payload survived media stop and relay reconnect. Cache and
  old fallback gates remain closed when not authorized.
- `build/dev174-direct-interop.log`: actual Windows Schannel versus production
  Android JNI/Java TLS, mutual pinned Hello, bidirectional verified 2 MiB + 123 byte
  files, negotiated 64 KiB/window 16 and durable completion ACKs. Host execution,
  not physical Android storage or measured LAN throughput.
- `build/dev174-chat-attachment-tests.log`, `build/dev174-clipboard-tests.log`,
  `build/dev174-group-ui-tests.log`: explicit delivery scope/readiness/error policy,
  attachment/history projection, clipboard preservation and group UI nonregression.

- `build/dev174-chat-scope-final-tests.log`: final Windows assembly tests,
  including conversation changes while waiting for a route. The shipping assembly
  was rebuilt after the last scope guard; its native dependency audit passed.
- `build/dev174-pipeline-regression-final.log`: existing transfer pipeline,
  packaged scheduling and bounded negotiation regressions passed.
- `build/dev174-group-application-regression.log`: actual isolated application /
  server cache path passed two independent recipient downloads, sender-offline
  recovery, server restart, verified exports and signed receipts. Pi untouched.
- `build/tests/android-jni-dm-compat-174/direct-compatibility.log`: production
  Android JNI with real authenticated TLS/Hello sent a Unicode PeerFile without
  cache capability. Offline, unverified, wrong-account, stale generation and an
  old-generation live TLS route were rejected.
- `build/tests/android-jni-dm-compat-174/results-147afbfbbd204ef68b22430b24fbfdd2.log`:
  bidirectional cached JNI/Windows transfers, independent recipients, pin rejection,
  restart, lost append and ordinary mailbox ACK/deduplication passed.

- `build/tests/android-174-final-gradle-79f6f8091143447490ee69faafa7f45d.log`:
  final-source Android build, all 321 tests across 73 suites (zero failures/errors),
  lint (zero errors, 17 warnings) and APK assembly passed.
- The same JNI fixture directory's `legacy-transfer-resume.log` and
  `legacy-profile-ack.log` passed v2/window16, protected journal restart/resume,
  negative protocol modes and eight actual profile/TLS/message/ACK cycles,
  including lost ACK, exact-ID deduplication and history.

## Checkpoint structure

Existing branch/worktree retained: `codex/native-unicode-emoji` at
`C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game`.
The dev.173 baseline (`bdd28e5`, handoff `686dbdf`) and its accepted packages
remain available; no history was reset or rewritten.

- `51df50c`: exact verified Windows online selector and real Node tests.
- `5d0efd6`: cache-preferred Windows preflight, explicit compatibility dialog and
  shared clipboard/file handling.
- `cf89d7a`: dismiss obsolete Windows choices after conversation changes.
- `5ba7944`: generation-bound Android native admission and real TLS/JNI proof.
- `07bc033`: Android explicit compatibility UI, current-conversation guards,
  bounded route wait and focused regressions.
- `2f85963`: matching Windows/Android dev.174 package source checkpoint.

## Package handoff

Artifacts are under
`C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game/peerchat/build/development/4.0.2-dev.174/`:

- Windows: `PeerChat-Windows-4.0.2-dev.174.zip` (runnable copy:
  `Package/PeerChat.Desktop.exe`).
- Android: `Android/PeerChat-Android-4.0.2-dev.174-debug.apk`.
- Source/dependency/version/signature/ZIP payload audit and artifact hashes:
  `messaging-repair-package-audit.json`; ZIP includes `candidate-manifest.json`.
- Android version `4.0.2-dev.174`, version code 2, arm64-v8a. Its existing signing
  identity is unchanged; update with `adb install -r`, never uninstall/clear data.
- Final APK SHA-256:
  `f2a2378f74bf466f1530a81ecca6d3b7efbbdbf7b5dfbb91f73e0fd0e20c3391`.
- Windows ZIP SHA-256:
  `b6dabb47f36d5b13e56fe4b8a2f4a697815630bc621dbc487037220b971379c6`.
  Final package audit passed version, unchanged Android certificate, native
  dependencies, source checkpoint and every ZIP payload byte/hash.

These are host-validated test packages, not physical acceptance. No APK was
installed and no live server was changed. Android lint warnings remain; they are
not reported as zero-warning validation.

## Latency and performance

Cache readiness adds one bounded authenticated preflight roundtrip. Windows and
Android can wait approximately 25 seconds for the existing route owner to establish
a direct connection; neither creates a second reconnect owner. Direct payload chunking,
concurrency and bandwidth settings are unchanged. No physical speed improvement
is claimed from these controlled fixtures.

## Physical acceptance

1. Update both clients without clearing profiles or application data.
2. With an older secondary device still registered, attach a file from Windows to
   the phone and from Windows to another Windows device. Confirm the compatibility
   explanation, explicit device-only choice and correct verified saved file.
3. Cancel the choice and confirm nothing is sent and the image draft remains.
4. Disconnect the selected recipient; confirm no direct send is offered without
   a verified route. Reconnect, send, interrupt and resume, then verify contents.
5. On an account whose required devices support cache, verify cache remains the
   default and the recipient can download after the sender exits.
6. Recheck clipboard images, group attachments, messaging and chosen filenames.

No APK installation or Pi deployment is part of this handoff. Stop for the user's
physical results after validated packages are produced.
