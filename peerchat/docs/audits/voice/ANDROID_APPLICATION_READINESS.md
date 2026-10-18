# Android voice application readiness

Baseline: `be502d2`, after the completed dev.180 Device Manager/name milestone.
This is an application integration audit, not evidence of working Android calls.

## Reusable implementation

- `application/VoiceExecution.kt` already defines account/authentication-generation-scoped native commands for start, answer, decline, hang-up, mute, deafen and ICE configuration, plus a typed call snapshot. The application currently consumes snapshots but does not provide the complete command/UI ownership path.
- `MessagingBackend.kt` owns the existing native backend, authentication generation and network revision. Its existing fenced asynchronous-work pattern is the integration point for fetching TURN credentials without holding the shared application lock.
- `portable/voice_application.hpp` owns the shared DM controller and uses CallManager, VoiceMedia and authenticated peer signaling. It must remain the controller rather than being duplicated in Kotlin.
- `AndroidCallAudio` implements permission checks, transient communication audio focus and output routing. `AndroidAudioDevice` supplies token-bound native capture/playback callbacks, 16 kHz mono 20 ms frames, bounded worker teardown and optional platform processing. These are foundations, not evidence of an exercised end-to-end call.
- `AndroidMessagingRuntime` maintains the application backend through UI, wake, transfer and archive leases, including the existing 60-second passive background grace period. It has no voice-specific ownership lease yet.
- The current transfer foreground service is a bounded data-sync service. It must not be reused as indefinite microphone/call ownership.

## Missing integration and safe sequence

1. Compile and exercise the genuine pinned WebRTC NetEq receive adapter. Keep voice unavailable when it is absent; do not substitute a fake receiver.
2. Add explicit user Start/Answer commands scoped to account, peer, call identity, lifetime and authentication generation. Permission denial must leave microphone capture stopped.
3. Establish a microphone foreground service from an allowed explicit user interaction, with visible ongoing-call notification and scoped hang-up. Incoming background notification must not itself start capture.
4. Give an accepted active call a distinct lease on the same application backend. Activity recreation, opening another conversation and the ordinary background grace deadline must not destroy it. Hang-up, revocation, supersession or terminal failure must release audio, focus, notification and lease.
5. Fetch credentials outside shared locks; reject delayed results for a superseded account/call/generation. Preserve existing TURN and authenticated signaling boundaries.
6. Add bounded diagnostics through the current export system. Never retain microphone samples, keys or credential values.
7. Only after real DM integration passes, connect existing group signaling/membership and shared media topology. Existing group foundations alone do not establish Android group-call support.

## Android execution constraints

The app targets API 35. A call needs the proper microphone foreground-service declaration/permission, not merely RECORD_AUDIO. While-in-use permission restrictions apply at service creation: an arbitrary wake callback is not microphone consent. Audio focus on target API 35 requires the application to be topmost or running a foreground service. Modern communication-device routing must account for Bluetooth/BLE and device removal; the existing speaker/earpiece controls are not a complete route-selection workflow.

Primary references consulted:

- [Foreground-service background-start restrictions](https://developer.android.com/develop/background-work/services/fgs/restrictions-bg-start)
- [Declaring foreground services](https://developer.android.com/develop/background-work/services/fgs/declare)
- [Audio focus](https://developer.android.com/media/optimize/audio-focus)
- [Communication device and BLE audio routing](https://developer.android.com/develop/connectivity/bluetooth/ble-audio/audio-manager)

## Dependency gate and validation boundaries

The pinned WebRTC revision is `6cf6bcb585b31414aac02377df2bc8fec770d0e8`. Its Android GN build requires Linux. The existing Windows source tree has Android dependency checkout disabled and lacks its pinned Android NDK bundle. No runnable WSL distribution or container environment was found. Reading Windows optional-feature state requires administrator elevation, so feature enablement is not asserted. No system feature, Linux distribution or toolchain was installed by this audit.

A supported Linux build needs Android dependencies at the existing pin, API 26 compatibility, a complete static NetEq adapter dependency closure, and validation against the app's ARM64 JNI/C++ runtime. The app uses NDK 27 whereas the pinned WebRTC checkout declares an NDK 30 dependency; a successful compiler invocation alone would not establish compatible linkage or runtime behavior.

The completed dev.180 Android baseline passed 398 tests across 89 suites and lint. Those results validate the existing application, not two-way voice. Controller unit regressions use an explicitly fake media seam and cannot prove ICE, DTLS-SRTP, NetEq, capture/playback or physical call quality. No phone installation or Pi voice deployment is authorized by this milestone.
