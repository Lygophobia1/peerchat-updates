# Android direct/group voice — dev.181 candidate

## Status and boundaries

This is a physical-test candidate, **not physical voice acceptance**. Work continues on `codex/native-unicode-emoji` in `C:\Users\Livel\.codex\worktrees\native-unicode-emoji\Create 2D Game\peerchat`. Accepted dev.180 packages and checkpoints are retained. No phone installation, Pi deployment, server protocol change, message migration or key replacement was performed. Package manifests record the exact source checkpoint and artifact hashes.

Earlier architecture/readiness documents describe the pre-implementation state. This report supersedes their open build/application gates where evidence below explicitly closes them; it does not supersede the physical gates.

## Implementation and integration manifest

| Area | Implementation / shared boundary |
| --- | --- |
| Genuine receive dependency | Pinned WebRTC NetEq/Opus/mixer archive, API 26/NDK 27, existing `pc_rx_*` ABI. No substitute audio engine. Dependency integration checkpoint `616a259`. |
| Direct voice | Existing `VoiceApplication`, verified reliable peer TLS, `VoiceMedia`, Opus and DTLS-SRTP. Existing Start/Answer/Decline/End plus native exact call lifetime/authentication checks. TURN acquisition/refresh is bounded and outside the application lock. |
| Android ownership | `VoiceConsentRegistry`, `MessagingBackend`, `AndroidMessagingRuntime`, `PeerChatVoiceService` and `AndroidCallAudio`. A short-lived single-use explicit UI gesture binds the profile/session/call. Permission and a visible activity precede microphone service startup. Service owns the runtime lease rather than Activity focus. |
| Group voice | `GroupVoiceRooms`, `GroupVoiceExecution`, `GroupVoiceBridge`, `portable/android_group_voice_application.hpp` and the JNI adapter in `portable/peerchat_core.cpp`. Existing authenticated room events and signed current group roster gate the existing `GroupVoiceSession`/relay-mesh engine. No new server messages or group encryption format. |
| Diagnostics | `portable/voice_application.hpp` exposes an allowlisted bounded projection of existing counters. `VoiceExecution`, snapshots and `NativePresenter` export it. A private, versioned JNI diagnostics tail and a separate private group JNI request domain require matching APK/native components; neither changes the peer wire protocol. No raw audio, SDP, TURN credentials or database sampling. |
| Android UI | `PeerChatApp`, `ConversationScreen`, `CallsAndYou`, `DetailsScreen`, `NativePresenter`, `UiState`, `VoiceNotifications`: permission/consent, incoming call notification, call controls and group room/participant controls. Existing composer and attachment routes remain. |
| Packaging | Voice is opt-in for build tooling and requires the exact exercised archive and notice hashes. Production ARM64 app ID and signing identity remain unchanged. An explicitly isolated x86_64 emulator package cannot overwrite the real application. |

Group signaling validates account, device, room, epoch, participation, roster and media generation; duplicate device identities under alternate participation IDs are rejected. Authentication/event-session loss, roster mismatch or essential audio failure retires media. Late callbacks and old notification actions cannot control a successor call. Rejoining is explicit; microphone consent is not persisted or recreated on process restart.

## Build-error corrections and reproducibility

The supported dependency build uses the separate **PeerChat-Build WSL2** Ubuntu 24.04.5 distribution. User-approved Windows restarts were performed by the user, not automatically. Full installation history and prior failed probes remain in `WSL_SETUP_PROGRESS.md`.

- WebRTC: `6cf6bcb585b31414aac02377df2bc8fec770d0e8`; depot_tools: `d38909eb68d85b91259f4bf20fe5ebcd38461bc2`.
- Chromium Clang 24 generates ordinary, non-LTO ELF objects against the NDK `27.0.12077973` target sysroot; the APK uses NDK 27 Clang 18. Target custom libc++ is off; host custom libc++ remains on. Hidden static C++ runtime boundaries are retained.
- `audio_receive/webrtc-ndk-iterators.patch` repairs two isolated incompatible iterator constructions, not codec algorithms. The complete archive carries upstream Opus `55513e81d8f606bd75d0ff773d2144e5f2a732f5`; a second independently pinned Opus is not linked into that Android media owner.
- ARM64 archive: 999 AArch64 ELF objects; SHA-256 `fcef3218613460ca02eae2bd83f997a5a7e6a3097440a7c9e2b19f6472bd47e4`.
- Native Android x86_64 test archive: 1,006 objects; SHA-256 `671d90a6a95d4bfc4a8561dbd28196138662449a98ccefd1a9334d9da190a14e`.
- `scripts/build-android-neteq.sh`, the checked-in GN target/patch, and `scripts/export-android-voice-notices.sh` reproduce dependency preparation. The exporter derives included third-party notices from the actual archive build rule, not the entire source checkout. Bundle hash: `d02f083a5ce0e2375cee4f6786a3035f70ef5a10d000d46c37a97859b422b131`.
- Linux dependency paths: `/home/peerchatbuild/deps/webrtc-6cf6bcb-android/src`, `/home/peerchatbuild/deps/android-ndk27-target`. Windows output: `build/android-neteq/arm64-v8a` and `build/android-neteq/notices`.

Production APK command from `android` (JDK 22 and existing SDK):

```powershell
./gradlew.bat :app:assembleDebug :app:lintDebug '-PpeerchatVoiceEnabled=true' '-PpeerchatVoiceReceiveDirectory=../../build/android-neteq/arm64-v8a' '-PpeerchatVersion=4.0.2-dev.181' --offline --console=plain
```

Quote the complete version property in PowerShell: an unquoted dotted value was split, correctly failing the version-format gate before compilation. This was a build invocation error, not a runtime defect.

The x86_64 test build additionally uses `-PpeerchatVoiceIntegrationTest=true`, `-PpeerchatTestAbi=x86_64` and `../../build/tests/neteq-x64-ndk27`. Never hand that APK to a user as the production update.

## Validation evidence and measurement boundaries

| Validation | Result / boundary |
| --- | --- |
| Android JVM/Compose regression suite | **436 tests, 95 suites, zero failures/errors/skips.** Consent, permission denial, lifecycle, stale generation, room projection, UI controls, existing messaging/storage/attachment paths included. Robolectric tests are not phone audio tests. |
| Android emulator instrumentation | **2 tests passed, 4.908 s** in isolated `com.peerchat.android.voiceintegration` on `emulator-5580`. Real JNI authorization/lifetime checks and actual AudioTrack/AudioRecord start, mute, stop and successor-token isolation. Temporary microphone permission restored afterwards. Log: `build/tests/android-voice-final-emulator.txt`. |
| Genuine ARM64 receive/mixer | Full unchanged ARM64/Bionic suite passed under QEMU: 1–8 sources, silence, loss/reordering, NetEq concealment and lifecycle. `build/tests/android-receive-qemu-8f13301bb33f433eb892a6b0dd5c3c4b/runtime.log`. Functional emulation, not phone performance. |
| Genuine native x86_64 Android receive | Full JNI receive suite passed: `build/tests/android-receive-proof-ad1d90fadfa8422da36b35219cb129e3/runtime.log`. |
| Genuine DM media + concurrent transfer | `test-android-voice-media-integration.ps1`: production native application, real verified peer TLS, Opus/ICE/DTLS-SRTP/NetEq; synthetic audio devices only. 2,097,275-byte v2 transfer confirmed at both endpoints with SHA-256 equality; 35 authenticated transfer frames, maximum plaintext transfer frame 65,620 bytes. Both ends received **169 additional decoded PCM blocks** during transfer; durable text ACK, reverse calling, recovery, replay rejection, revocation and teardown passed. No Pi, physical network throughput or Android OS end-to-end call claim. |
| Genuine group media | `test-android-group-voice-media-integration.ps1`: three participants through an isolated local WSL coturn fixture, distinct received tones from both remote sources, mixing, late join, mute/deafen, stale/revoked signaling and teardown passed. Synthetic audio devices; real TURN/Opus/DTLS-SRTP/NetEq. No live Pi. |
| Native group authorization/JNI | `test-android-group-voice-authorization.ps1` and `test-android-group-voice-jni-scope.ps1` passed: real group roster gate, wrong account/device, alternate-participation duplicate device, stale bindings, invalidation and teardown. |
| Native diagnostic filtering | Allowlist test passed, including empty numeric/performance values, malformed input and private/free-text rejection. Empty-value indexing was corrected during final review. |
| Windows regressions | Group voice session, group room protocol, group screen share, voice topology, messaging durability audit, group History Sync and reliable-transfer interop all passed. Logs: `build/tests/regression-logs/`. |
| Screen-share plus transfer | Existing lifecycle fixture passed with `PEERCHAT_SCREEN_TEST_RELAY=1`: actual H.264 media stop, 10 MiB verified transfer, local mailbox rejection, relay re-pair and confirmed-prefix recovery. Initial direct-listener mode failed **before media** because production no longer exposes that ordinary listener. Correct relay mode passed without changing production code or weakening assertions. |
| Windows build | `build.ps1 -All` produced dev.181. Packaged native dependencies/isolated load, HPKE, 15 group voice and 15 screen-share native lifetimes passed. Existing compiler warnings retained. |
| Production Android build | ARM64 dev.181 assembly and lint passed (zero errors, 25 warnings). The full 436-test suite also passed again against the final production app/Firebase configuration, not only the isolated emulator configuration. |
| APK audit | `com.peerchat.android`, version `4.0.2-dev.181`, API 26–35, ARM64 only; accepted signing certificate SHA-256 `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`. Four native libraries have 16 KiB ELF LOAD alignment and 16 KiB stored APK entry alignment. Native voice JNI exports present, internal C++/receive symbols hidden. Native notice asset hash matches the pin above. |

Final stripped `libpeerchat_messaging.so` SHA-256:
`d45bb904853d8e2b664d39207d00106c93d5fa15a1162eba6fb96282ccd03f78`;
`libpeerchat_jni.so`: `fa38d85ba065f9246b791f9eb8d5575f33e8f7c30a8763115361cb8c906eb157`.
The messaging library depends only on Android log/dl/m/c; JNI also depends on
the packaged messaging library. The installed build-tools 34 `zipalign` lacks
the newer `-P` option, so 16 KiB stored-library alignment was verified directly
from ZIP central-directory/local-header offsets instead; no APK bytes changed.

The DM fixture requires ordinary Windows Schannel key-container access for isolated test identities; a sandboxed attempt failed at NCrypt import before transport. Approved host execution passed. No real profile or identity was used. Compiled fixture: `%TEMP%/android-voice-media-integration-32c5d5863237443487d1b29eb8bdf1d7`; test profiles: `%TEMP%/pc-voice-integration-elevated-172918`.

The durability suite also prints its intentional legacy-policy observation about an unprepared sibling copy; its current frozen-target-manifest assertions pass. That fixture observation is not a new voice delivery change.

### Explicit unresolved test boundary

ARM64 binaries through Android's x86_64 `libndk_translation` emulator fail the direct Opus silence assertion with both tested compilers and scalar/NEON variants. The same unchanged ARM64 archive passes Bionic/QEMU, and native x86_64 Android passes. A translation/runtime issue is suspected; its exact instruction-level cause is **not proven**. Assertions and codec behavior were not weakened. Native Motorola ARM64 acceptance is still required.

## Performance, compatibility and remaining limitations

- Device audio uses bounded 16 kHz mono, 20 ms capture blocks and device-paced playback. Group voice uses one encoder and per-remote NetEq/DTLS-SRTP endpoints. Upload fanout and decode/memory costs grow with participant count. The admission cap is 32 remote sources, **not a claim that a phone supports 32 smooth simultaneous speakers**. Genuine network testing covered three participants; receive-only functional tests covered eight sources.
- Media uses the existing separate encrypted transport; no bulk-transfer prioritization/protocol/window rewrite. Concurrent host audio+text+transfer passed. Physical call latency, cellular switching, sustained load with History Sync, thermal behavior, CPU, battery and mouth-to-ear delay remain unmeasured.
- Active call ownership survives Activity navigation through a microphone foreground service, not a wakelock or battery exemption. Process death ends the call; no automatic microphone restart. Background/lock/focus behavior still needs physical OS acceptance.
- Incoming calls use existing live signaling and notification entry. No new cold-process incoming-call FCM protocol was added. An app with no receiving signaling runtime cannot be promised phone-like incoming ringing.
- Group event-session loss/revocation stops safely. The existing mesh does not provide automatic same-participation group ICE recovery here; use explicit Leave/Join after reconnection. Failed routes can remain connecting until rejoin. No silent authorization reuse.
- Speaker toggle and OS default wired/Bluetooth communication routing are available. A full Bluetooth/device picker and physical headset hotplug/focus behavior are not validated. Existing platform AEC remains off; NS/AGC defaults are retained. Speakerphone echo and fan suppression require physical evaluation, preferably beginning with headphones.
- Participant membership and mute controls are shown. Active-speaker highlighting, per-participant volume and per-source group jitter are **not exposed**, rather than fabricated. DM receive/buffer/concealment counters are real; NetEq playout delay is not end-to-end latency. Most retired media counters are not retained as a historical call report.
- Windows native/network behavior and deployed Pi room/TURN protocol are unchanged. Private Android JNI additions are shipped atomically inside the APK. No Android video or screen capture was implemented.

## Physical acceptance checklist

1. Update using the same signed APK with `adb install -r`; do not uninstall or clear data. Run the matching Windows package. Confirm dev.181 and existing identities/history.
2. Call Windows → Android and Android → Windows; test Answer, Decline, permission denial/retry, two-way speech, mute/deafen, speaker and hangup from both UI and notification. Repeat calls without restarting.
3. Test Android → Android if another supported device is available. Verify wired/Bluetooth connect/disconnect and focus interruption. Start with headphones; separately assess speakerphone echo.
4. During an answered call, navigate conversations, switch apps, lock for several minutes and unlock. Verify microphone stops immediately on hangup and no stale notification/service remains.
5. Join the same group from Windows and Android; add a third participant, speak simultaneously, late-join, leave/rejoin and remove a participant using ordinary authorized controls. Test reconnect with explicit rejoin if necessary.
6. Send text and a file during DM/group voice; verify delivery/integrity and listen for disruption. Test a network interruption and Wi-Fi/cellular transition; export diagnostics after any failure.

Do not infer successful physical calling from the automated fixtures. Stop after handing off the packages; no deployment is required for this milestone.
