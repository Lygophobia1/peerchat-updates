# PeerChat Android development handoff — STOP checkpoint

> Finalization audit: see `ANDROID_CHECKPOINT_AND_REMAINING_WORK.md` for the
> current authoritative status. The interrupted handoff below is preserved as
> the STOP-time snapshot. Source remains227ce31; the focused authorized lint
> recheck now passes (0 errors,25 warnings), including final Kotlin compilation.
> It did not rebuild APKs or rerun the190-test suite. Main Work was re-read at
> de3b64314f0ff9b29db19bfff60fcb3acc09258d:51 changed paths, zero overlap with
> Android. Its Windows attachment application remains WIP and cache gated.


Development stopped on 2026-10-15 at the user's explicit request. This document
preserves the current milestone for later resumption or branch integration.
**The integrated milestone is WIP, not production-ready or physically accepted.**
No new feature, optimization, investigation, test, benchmark, signing, deployment,
phone installation or branch integration was started after STOP.

## Repository state

- Worktree root: `C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`.
- Project/source directory: `C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice/peerchat`.
- Branch: `codex/android-groups-voice`; no branch/worktree switch or creation.
- Shared starting baseline: `e0940de`; continuation started from `d67b0e5`.
- HEAD when STOP arrived: `516aed14b19ea2d63acc6223810f5faddbb16957`.
- Captured final **code HEAD**: `227ce313d5b9c7d40af599c5eaa8e2451849c34f`.
- The final repository HEAD adds only this handoff and the updated integration
  manifest to that code checkpoint. Its exact hash and final status are recorded
  in the accompanying `handoff-state.json` after the documentation commit.
- All tracked modifications and untracked source/test files were reviewed and
  committed. Expected final tracked/untracked status: clean. Ignored build outputs
  and synthetic test profiles are intentionally retained on disk.
- No history rewrite, force push, merge, reset, worktree deletion or cleanup ran.

### Checkpoints

| Commit | Meaning / validation boundary |
| --- | --- |
| `e0940de` | Common baseline with Main Work. |
| `d6c3a64` | Android mailbox recipient-provisioning failure isolation. |
| `158d317` | Standalone pinned Android ARM64 Opus build proof. |
| `d67b0e5` | Initial cross-project integration manifest. |
| `ededec3` | Scoped pending group key query, preserving old overload/schema. |
| `2961f94` | Native group adapter, scoped admission and interoperability/security fixtures. |
| `516aed1` | Pinned media/platform audio adapter and permission gate; final permission cleanup unvalidated. |
| `03211ab` | Gated existing CallManager/VoiceMedia application adapter; host component tests pass. |
| `b92a575` | **WIP** group/voice JNI and Kotlin command bindings + instrumentation fixtures. |
| `0f6a801` | **WIP** application execution, scoped relay and reconnect submission. |
| `794eaca` | **WIP** protected group preferences and native-history notifications. |
| `227ce31` | **WIP** group UI, drafts, unread/paging and voice diagnostic presentation. |

WIP labels preserve incomplete acceptance honestly; they do not mean discarded
work. No unresolved merge or partially staged code remains.

## Completed implementation and current limitations

### Messaging fault isolation

`application/MailboxExecution.kt` and its isolation fixture separate null local
recipient registration from outbound preparation and signed receipt reconciliation.
The existing native key registry, verified recipient selection, HPKE, durable
outbox and receipt authority remain unchanged. Incoming receive/ACK remains gated
on successful native local registration. No authentication bypass was introduced.

Earlier checkpoint evidence:157 host Android tests, debug/release/instrumentation
builds and existing real HPKE/isolated pinned-TLS mailbox regressions passed. Those
results belong to the earlier source state, not proof of the final WIP package.

**The physical Motorola's Online/Queued problem is unresolved.** No phone diagnostics
were collected in this continuation. Missing mailbox registration and rejected
peer authentication still require preserved-profile investigation. Same-account
Windows/Android devices retain distinct identities and existing verification rules.
No reset, key regeneration, contact/history modification or claimed phone repair.

### Native group text and session integration

`portable/group_application.hpp` composes the existing signed GroupLedger,
GroupKeyManager, Replica, MessageLayer, Storage and DeliveryManager. The existing
SessionTransportManager remains the route/attempt owner. There is no replacement
group protocol, cryptography, trust database or transport scheduler.

Implemented: authenticated rosters; create proof; signed metadata/key/ACK exchange;
current epoch and exact key admission; encrypted Unicode text send/receive;
protected native history/deduplication; exact ciphertext retry; revocation and
explicit additional-device guards. Rejected metadata cannot grow the ledger.
`Storage::pendingGroupKeyHandoffs(peer,group,generation,limit)` filters before LIMIT
so64 old handoffs cannot starve a current key. Existing overload/schema remains.

Scoped TLS never creates friend trust. New optional group/voice endpoint callbacks
preserve existing friend/archive defaults. Completion requires this transport's
key exchange, durable native state, socket output drain and a final native check.

Passed host checks: real Schannel group key exchange; actual unchanged Windows
Node with two Android native adapters (three participants), both text directions;
Unicode, signed proof, rotation/removal, tamper, restart/history and dedupe;65 scope
plans and stale retirement;64 stale offers; unauthorized signed metadata growth;
102 existing DM/archive reliable endpoint lifecycles. This is real native crypto
and TLS on Windows, **not Android ART/device execution**.

### Android group application and presentation — WIP

`GroupBridge`, `GroupExecution`, `MessagingBackend`, `ReliableRecords`,
`AndroidRelayExecution`, `ServerAccounts` and `ForegroundEvents` bind local commands
to the existing server RPC/event contracts. Commands carry current account/auth
snapshot generation. Native receive binds expected group/message IDs before ACK.
No failed native admission can create a positive ACK.

Compose supports group listing/create/invite/accept/decline, roster devices,
send/receive attribution, delivery detail/retry, bounded history/paging, protected
drafts/read positions and unread state. Known authenticated friend accounts supply
names; other senders retain account/device attribution. No fake Delivered status.
Group attachments/calling controls remain unavailable.

At most8 native rows/page,200 projected rows/group,4 new previews/pass,128 groups,
256 roster devices/group,4096 UTF-8 text bytes. Group preferences reuse the protected
codec in a separate file/domain, bounded128 entries; old DM preference records remain
compatible. `baselinePending` prevents late historical pages creating false unread.

`GroupPendingProjection` scans existing protected history at most8 rows/4 envelopes
and239000 bytes/pass, verifies native signature/AEAD/plaintext consistency, and
returns exact stored ciphertext/IDs without receive-side effects or encryption.
Deferred rescan preserves older pending work under continuous new sends. Existing
foreground event transport submits it; backpressure/exception causes safe rescan.
Passed BCrypt and portable-MbedTLS bounds, corruption, auth, restart and starvation
fixtures. Kotlin fake-port recovery tests passed in the latest190-test report.

Notifications consume native-admitted history and protected cursors with existing
privacy/permission settings. Old history is silent; matching account/server scope
is required for links. Existing group server delivery is **online sessions only**.
Server acceptance is not offline-recipient storage or end-to-end delivery. Group
wake/push and historical recipient catch-up remain incomplete. Notifications,
process death, background behavior and the final APK require physical acceptance.

### DM voice — components integrated, production disabled

`portable/voice_application.hpp` reuses CallManager and existing call frames;
`android_voice_application.hpp` binds verified friend pins, authenticated session
winner and generation/expiry-bound ICE to actual VoiceMedia. Kotlin VoiceExecution
and native VoiceBridge expose strict capability/state snapshots. UI displays the
actual unavailable reason instead of simulating a call.

Android platform audio implements the existing Audio device API using
AudioRecord/AudioTrack:16-kHz mono320-sample blocks, bounded queues, permission and
audio-focus preparation, mute/deafen, routing, stale callback tokens and cleanup.
Actual pinned Opus/libdatachannel/libjuice/MbedTLS DTLS-SRTP compile for ARM64/API26.
Session AEC/NS/AGC are separate from unavailable WebRTC APM; no fake presets or
double-processing claim. Microphone/audio-settings permissions are declared;
there is no runtime permission UI grant or active microphone service.

Passed: actual Audio native host JNI lifecycle/concurrency + PCM bounds;
Windows receive50 lifetimes; native CallManager adapter tests for unavailable gate,
replay/glare,64candidate bound, recovery, timeout, mute/deafen and teardown. Media
unit factory is explicitly fake, so these are not voice transport/audio E2E tests.
Kotlin parser/gate tests passed; actual JNI instrumentation compiled only.

**Architecture blocker:** genuine pinned Android WebRTC NetEq is unavailable. The
actual GN probe says "Android builds are only supported on Linux." No runnable
Linux/WSL environment is installed. No substitute jitter/receive stack was created.
Capability remains false; voice is not advertised; Start/Answer cannot capture.
Authenticated TURN E2E, actual Opus encode/decode execution on ARM64, permission UI,
active-call foreground-service/lifecycle integration, headset/Bluetooth, interruptions
and physical audio quality/resource acceptance remain unfinished.

### Group voice — reused native code prepared, orchestration unfinished

Existing GroupVoiceSession, LocalVoiceSource, RelayMeshVoiceTopology, room service
and server group_room/group_media contracts were inspected. Existing session and
mesh sources cross-compile for ARM64 using the same device/audio/receive architecture.
Android route/SSRC randomness uses the existing platform primitive; room/protocol
fields stay unchanged. NetEq absence also gates GroupVoiceSession before capture.
Unused existing room events no longer close text subscriptions.

Existing Windows group session30 lifetimes and topology15 lifetimes passed. No
live TURN success was inferred. Android room participation orchestration,
participant UI/state, multi-party signaling and actual audio remain unfinished.
Do not present master volume as independent participant gain. DM acceptance gates
group enablement; video/screensharing remains unsupported.

## Validation snapshot at STOP

| Check | Recorded result |
| --- | --- |
| Latest JVM/Robolectric/Compose reports |190 tests across41 suites;0 failures,0 errors,0 skipped. |
| Integrated ARM64 debug | Existing dev.157 APK built; predates final source edits. |
| Instrumentation | APK compiles; not installed/executed. |
| Combined final validation command | **FAILED** after3m46s on lint; not a successful final build. |
| Lint |1 MissingPermission error,25 warnings in retained report. |
| Final source permission fix | Direct capture permission recheck/cleanup and manifest declaration saved afterward; no rerun. |
| Latest presentation cleanup | Known-contact group sender names saved afterward; no final compile/test. |
| dev.157 release | Not packaged; release CMake task was reached, not a final release acceptance. |
| ART/real ARM64 execution | None. Cross-compile is not runtime proof. |
| Emulator | No AVD/device; hypervisor missing (acceleration exit1/error6); installed image x86_64 vs ARM64 app. |

Initial compile errors and the JSONObject test-runner failure are preserved in
logs. Corrected group tests use Robolectric and passed in the latest report.
The lint fix is **not claimed passing** because STOP prohibits a new validation run.
No new tests/benchmarks started after STOP. The already-running Gradle invocation
was found completed with failure; it was not restarted.

## Outputs and evidence preserved

Session output folder:
`C:/Users/Livel/Documents/Codex/2026-10-14/peerchat-android-groups-voice-session/outputs/android-handoff-2026-10-15`.

| Preserved file | Exact status / SHA256 |
| --- | --- |
| `PeerChat-4.0.2-dev.157-WIP-debug-preserved.apk` | Existing WIP debug,29,971,971 bytes;1259f571640e61534832e76ae33b81a7947a668fa63286bcbf7d32abfc954011. |
| `PeerChat-androidTest-WIP-preserved.apk` | Compile-only fixture,1,145,634 bytes;bc644f059862c8a3e9d08ed4d12386bcc0f2d55189b4a3b8371edbc443e69268. |
| `PeerChat-4.0.2-dev.156-release-unsigned-preserved.apk` | Older release, unsigned,12,872,911 bytes;3e7a21aa4f1decb0402f0f5c7c0bffa7d180f9478fa4be32532d92abf1ccb57a. |
| `handoff-state.json` | Final code/documentation HEAD, status, artifact hashes and test/evidence inventory. |
| `evidence/` | Existing native logs, failed/successful Android runs, lint,41 suite XMLs, GN probe and standalone media build evidence. |

Read-only verification of the preserved debug APK certificate matches dev.155:
`db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`.
Package remains `com.peerchat.android`, versionCode2, ARM64, minSdk26/targetSdk35.
Matching certificate permits update compatibility; retained-data upgrade remains
unexecuted. This development certificate is not a distribution signing identity.
No APK was built, signed or installed after STOP. No dev.157 release is implied.

Earlier dev.156 deliverables remain in session `outputs/android-dev156` without
replacement. The original dev.155 diagnostics APK remains untouched in
`C:/Users/Livel/.codex/worktrees/native-unicode-emoji/Create 2D Game/peerchat/build/development/4.0.2-dev.155/Android/PeerChat-4.0.2-dev.155-diagnostics-debug.apk`.
Existing ignored `build/`, Android `.cxx/`, APKs and synthetic profile fixtures remain
in this worktree. They were not cleaned, reset, imported into real profiles or
committed as source. Copied evidence deliberately excludes synthetic private keys.

Important retained logs under this project's `build/`:

- `tests/android-session/dev157-final-validation.log` and earlier `dev157-validation*.log`.
- `tests/android-windows-interop-latest-final.log`.
- `tests/group-pending-native-final.log`, `tests/group-pending-portable-final.log`.
- `tests/android-group-extra-regressions.log`.
- `tests/android-session/dev157-reliable.log`, `dev157-group-voice.log`, `dev157-topology.log`.
- `android-media/host-tests/`, `android-media/arm64-v8a/`, `android-media/webrtc-gn-probe.log`.
- `tests/group-relay-admission-7de6bfc9fec146ef8187974f01dc3a0b/`.
- `tests/android-voice-application-6ee038a44dc64544a8260c4b8e0da40e/`.

## Cross-project integration boundary

Main Work branch `codex/native-unicode-emoji` was read only. Initial manifest
inspection preceded shared changes; latest committed inspection was
`724116da7db84fbbaea15509a45a62c5e6f6d832`. Its15 changed paths and this branch's
paths have zero overlap against `e0940de` at inspection. No Main Work index,
checkout or live execution was modified; no cross-chat approval is claimed.

Detailed exact shared-file, JNI, protocol, dependency and compatibility inventory
is in `ANDROID_GROUPS_VOICE_INTEGRATION.md`. Existing PCGE, signed ledger,
GroupText.v1, call frames, group room/media, HPKE/mailbox formats, device identity,
trust, Keystore aliases and protected message schema are preserved. No server
change or migration. Local JNI records are new local interfaces, not peer wire.

Main Work owns attachments, per-recipient HPKE envelopes, descriptors, cache
admission/cleanup and journals. No immediate Android text/voice build dependency.
Future attachment UI/JNI needs canonical signed(group,message,object) identity,
historical+current roster/incarnation entitlement, retained recipient binding and
operation lease; per-recipient envelopes cannot replace common PCGE history.
Envelope receipt is distinct from verified file completion. Do not create a second
TransferManager or attachment crypto/cache stack. Reinspect both manifests before
integration; the branches were not merged.

## Agent and operational shutdown

`android_media`, `group_security_review` and `interop_emulator` completed their
assigned work. Their changes/evidence were collected; all were explicitly stopped
and none remains executing. No further agent work was dispatched after STOP.
The existing Gradle operation completed; no new build/test process was launched.
No live Pi/phone operation, physical install, identity reset, destructive migration,
service/driver installation, branch merge or deployment occurred.

## Resume notes — documentation only, not started

1. Inspect this code checkpoint and the two manifests; keep current branch/scope.
2. Verify final permission cleanup and sender-name edit with targeted compile/lint,
   then the final integrated debug/release/instrumentation checks. Current lint
   status remains failed until a new authorized run proves otherwise.
3. Rebuild candidate APKs from an explicitly recorded final commit; preserve the
   existing certificate/data paths and older rollback artifacts. Do not relabel
   the existing dev.156 release as dev.157.
4. Retain physical mailbox/auth blocker and online-only group fanout limitations.
   Validate retained-profile DM/group messaging, epoch/revoke/restart, notification
   and process/background behavior without bypassing trust.
5. Obtain/build genuine pinned Android NetEq on a compatible Linux host, then
   finish permission/focus/foreground-call lifecycle and authenticated TURN media
   acceptance before enabling DM/group voice. No fallback receive architecture.
6. Reinspect Main Work's latest additive attachment contracts before touching
   shared attachment/session/authentication boundaries. No automatic merge.

## Complete changed-file inventory

Relative paths below are from the Git worktree root and cover `e0940de` through
captured code HEAD `227ce31`. The only later tracked additions/changes are this
handoff and the updated integration manifest. No remaining uncommitted source
file is intentionally omitted.

- `peerchat/android/app/src/androidTest/java/com/peerchat/android/NativeGroupApplicationTest.kt`
- `peerchat/android/app/src/androidTest/java/com/peerchat/android/NativeVoiceApplicationTest.kt`
- `peerchat/android/app/src/main/AndroidManifest.xml`
- `peerchat/android/app/src/main/java/com/peerchat/android/GroupBridge.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/VoiceBridge.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/application/AndroidRelayExecution.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/application/GroupExecution.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/application/MailboxExecution.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/application/MessageNotificationPlan.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/application/MessagingApplication.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/application/MessagingBackend.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/application/MessagingSnapshots.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/application/ReliableRecords.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/application/VoiceExecution.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/platform/AndroidQolStore.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/platform/ForegroundEvents.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/platform/MessageNotifications.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/platform/PeerChatWakeJob.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/platform/ServerAccounts.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/platform/audio/AndroidAudioDevice.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/platform/audio/AndroidCallAudio.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/ui/ChatsHome.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/ui/ConversationScreen.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/ui/DetailsScreen.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/ui/NativePresenter.kt`
- `peerchat/android/app/src/main/java/com/peerchat/android/ui/UiState.kt`
- `peerchat/android/app/src/test/java/com/peerchat/android/application/GroupExecutionTest.kt`
- `peerchat/android/app/src/test/java/com/peerchat/android/application/GroupNotificationTest.kt`
- `peerchat/android/app/src/test/java/com/peerchat/android/application/MailboxIsolationTest.kt`
- `peerchat/android/app/src/test/java/com/peerchat/android/application/VoiceExecutionTest.kt`
- `peerchat/android/app/src/test/java/com/peerchat/android/platform/audio/AndroidCallAudioTest.kt`
- `peerchat/android/app/src/test/java/com/peerchat/android/ui/AndroidQolTest.kt`
- `peerchat/android/app/src/test/java/com/peerchat/android/ui/AttachmentParityTest.kt`
- `peerchat/android/app/src/test/java/com/peerchat/android/ui/GroupPresenterPreferencesTest.kt`
- `peerchat/android/app/src/test/java/com/peerchat/android/ui/NativeProjectionTest.kt`
- `peerchat/android/voice-foundation/CMakeLists.txt`
- `peerchat/android/voice-media/CMakeLists.txt`
- `peerchat/android/voice-media/media_capabilities.cpp`
- `peerchat/android/voice-media/media_probe.cpp`
- `peerchat/audio_receive/peerchat_audio_receive.h`
- `peerchat/delivery_manager.hpp`
- `peerchat/docs/ANDROID_GROUPS_VOICE_INTEGRATION.md`
- `peerchat/group_text.hpp`
- `peerchat/group_voice_session.cpp`
- `peerchat/portable/CMakeLists.txt`
- `peerchat/portable/android_media_capabilities.hpp`
- `peerchat/portable/android_voice_application.hpp`
- `peerchat/portable/audio_android.cpp`
- `peerchat/portable/audio_android_buffer.hpp`
- `peerchat/portable/group_application.hpp`
- `peerchat/portable/group_pending_projection.hpp`
- `peerchat/portable/peerchat_core.cpp`
- `peerchat/portable/relay_requests.hpp`
- `peerchat/portable/reliable_endpoint.hpp`
- `peerchat/portable/voice_apm_android.hpp`
- `peerchat/portable/voice_application.hpp`
- `peerchat/scripts/build-android-media.ps1`
- `peerchat/scripts/build-android-voice-foundation.ps1`
- `peerchat/session_transport_manager.hpp`
- `peerchat/storage.cpp`
- `peerchat/storage.hpp`
- `peerchat/tests/android_audio_buffer_test.cpp`
- `peerchat/tests/android_audio_lifecycle_test.cpp`
- `peerchat/tests/android_group_application_test.cpp`
- `peerchat/tests/android_group_windows_interop_test.cpp`
- `peerchat/tests/android_voice_application_test.cpp`
- `peerchat/tests/group_pending_projection_test.cpp`
- `peerchat/tests/group_relay_admission_test.cpp`
- `peerchat/tests/test-android-audio.ps1`
- `peerchat/tests/test-android-group-application.ps1`
- `peerchat/tests/test-android-group-windows-interop.ps1`
- `peerchat/tests/test-android-voice-application.ps1`
- `peerchat/tests/test-group-pending-projection.ps1`
- `peerchat/tests/test-group-relay-admission.ps1`
- `peerchat/voice_apm_capture.hpp`
- `peerchat/voice_media.cpp`
- `peerchat/voice_media_topology.cpp`
- `peerchat/voice_receive_engine.cpp`
- `peerchat/voice_receive_engine.hpp`
