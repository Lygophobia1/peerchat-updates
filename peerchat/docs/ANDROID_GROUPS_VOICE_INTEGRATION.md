# Android groups and voice integration manifest

## Ownership and checkpoint

Worktree: `C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`, branch
`codex/android-groups-voice`, base `e0940de`. The original
`codex/native-unicode-emoji` worktree contains separate active attachment work.
Nothing here adopts, stages, modifies or depends on its uncommitted implementation.
No merge, phone installation, live Pi change or release publication is performed.

## Inspected parallel interfaces — 2026-10-14

Read the current **PeerChat Main Work** chat
(`01a08777-56b5-70f2-b978-351f46780959`) and its in-progress attachment design.
Its user-approved scope is attachment-only per-device key envelopes using existing
verified HPKE recipient keys, with historical-access and revocation checks and
shipping cache disabled pending validation. The current implementation introduces
`group_attachment_key_envelope.hpp` and extends signed cache descriptors/server
storage. These are in-progress interfaces, not a validated dependency of this branch.

The reported envelope binds a selected attachment private manifest to its signed
descriptor, recipient key generation and membership incarnations. It does not
release a group epoch key. Envelope retrieval/ACK is separate from verified file
completion. Retained and current membership, remove/rejoin, expiry and key revocation
remain admission checks. Do not create a competing Android attachment envelope,
reuse a text ACK as an attachment ACK, or infer historical access from current login.

Before any future shared API, group format, authentication or protocol edit here,
refresh that project's planned interfaces and record the exact committed dependency.
Integrate only reviewed commits; never copy its uncommitted source or change its
worktree. Coordinate conflict resolution before modifying the shared owners below.

Also inspected **Peerchat UI Work**: its NS/AGC preset and APM ABI work belongs to
the existing Windows audio path. Android must preserve those existing mappings
when a platform audio adapter is eventually added, rather than invent a second APM.

## Current implementation change

Android-only mailbox execution isolates a null native recipient-registration result
from signed receipt reconciliation and outgoing envelope preparation. Local receive
is skipped until native registration reconciles. All outgoing recipient selection,
verified device/account mapping, encryption, durable state and receipt validation
remain in the existing native owners. Server failures and nonzero native acceptance
results still abort. No failed native receive produces a server ACK or notification.

Changed production file:
`android/app/src/main/java/com/peerchat/android/application/MailboxExecution.kt`.
The new internal `MailboxNative` port delegates the identical existing JNI methods;
it permits application-flow tests without loading a substitute crypto engine.
New regression file:
`android/app/src/test/java/com/peerchat/android/application/MailboxIsolationTest.kt`.

Modified shared native files: **none**. Modified JNI declarations/exports: **none**.
Protocol or encryption-format changes: **none**. Server changes: **none**.
Storage schema, migration and private-key handling changes: **none**.
No screen/video or calling permission/control is enabled.

A separate build-only voice checkpoint adds
`android/voice-foundation/CMakeLists.txt` and
`scripts/build-android-voice-foundation.ps1`. It checks the existing Opus revision,
cross-builds its static library and the unchanged `tests/opus_voice_test.cpp` for
Android ARM64/API 26, and records ELF architecture and hashes. No shared voice
interface or source is modified. The probe is not linked into the APK, executed
on a phone, or evidence of audio quality or calling functionality.

## Compatibility requirements

Keep `com.peerchat.android`, versionCode 2, ARM64, the existing debug certificate,
profile names, no-backup data paths and Keystore aliases. Build this candidate with
an Android-only version override; do not allocate the other project's shared product
revision or edit `shared/VersionInfo.cs`. Verify the packaged certificate against
preserved dev.155 before describing it as an update-compatible candidate. Matching
identity/signature permits an upgrade but does not prove on-device data preservation.

The existing server RPCs and Windows wire formats are unchanged. With a failed
recipient provider, outbound still requires native verified remote recipient keys;
this does not repair an unverified peer or grant trust. Incoming mailbox delivery
remains blocked until the local registration failure is resolved.

## Phase 1 audit and remaining dependencies

The portable Android library compiles shared GroupLedger, GroupKeyManager and
MessageLayer code, but its application instance owns DM/archive endpoints only.
`MessagingSnapshot` and `NativePresenter` project direct conversations only.
`ReliableEndpoint` admits DM/archive traffic; Windows Node owns current group
control/key handoff, membership/session invalidation and group message dispatch.
Android group text is therefore **not implemented or physically accepted**.

Required integration is a shared native group application/session owner, bound to
authenticated server rosters and verified signed replica evidence, then narrow JNI
commands/snapshots and Compose UI. Preserve the existing ledger, key manager,
MessageLayer, ciphertext fanout, durable history/dedupe and scoped reliable-session
authorization. Do not fake a group by issuing independent DM plaintext messages.
Do not duplicate ownership or silently approve additional same-account devices.

Any offline attachment support must consume the other project's committed
attachment-only envelope/admission boundary after review. Historical epoch keys
must not be released as a workaround. Shipping attachment cache stays disabled.

## Phase 2 audit and remaining dependencies

The production Android CMake explicitly excludes Node, Audio, RTC and media. Existing VoiceMedia
uses Opus/libdatachannel; Audio uses Windows devices and Windows APM/receive DLLs.
The installed native dependency directory is MinGW x64, not Android ARM64.
There is no linked Android RTC/DTLS-SRTP/APM/receive runtime or Android device adapter.
Windows DLLs are not usable in an APK. No Android call is claimed working.

An Android voice foundation must cross-build the pinned dependencies, retain the
existing CallManager/signaling authority and implement platform audio execution,
explicit microphone permission, audio focus, lifecycle cleanup and render-reference
AEC. DM ICE/TURN/DTLS-SRTP interoperability and physical audio acceptance precede
group voice. Screen/video stays unsupported. This manifest is not a protocol change
approval or a claim that these implementation prerequisites are complete.

## Validation ledger

Evidence is kept separately under `build/tests/android-session`.

| Checkpoint | Result | Boundary |
| --- | --- | --- |
| `d6c3a64` Android mailbox isolation | 157 JVM/Compose tests, zero failures/errors/skips; eight new execution tests | Injected application ports test control flow; native crypto is tested independently below |
| Debug APK and JNI | ARM64 debug build passes | No ART/Keystore/device execution in this session |
| Instrumentation APK | Compiles successfully | Not installed or executed |
| Release APK and JNI | ARM64 release build passes, unsigned | Native storage test hooks OFF; not a distribution artifact |
| Android lint | Zero errors, 14 warnings | Existing API/dependency/device-coverage warnings retained |
| Shared reliable endpoint | 102 Schannel/runtime relay lifecycles pass | Synthetic host profiles; durable text/ACK/dedupe, transfer, capability/winner/logout gates |
| Encrypted mailbox protocol/server | Pass | Real HPKE/canonical wire; pinned loopback TLS offline sender, independent-device ACK/restart, quota, expiry, rotation/revocation and tamper rejection |
| Group envelope, portable crypto | Pass | Three/four signed members, key handoffs, ciphertext authentication, additional-device approval/revocation, history dedupe/reopen; host only |
| `158d317` Android codec foundation | Pinned Opus and existing 16-kHz/20-ms probe compile/link for ARM64/API 26 | Android linker and system-only dependencies verified; **not executed**, no voice runtime/APK integration |
| Candidate upgrade metadata | Pass | Package ID/versionCode and certificate match dev.155; actual data preservation untested |

The initial sandbox build lacked the normal user's Gradle cache; a repeat used
the existing cache/SDK and original debug signing identity. The existing pinned
BC hook was copied read-only from the original build and its SHA-256 verified.
The initial isolated Windows identity test could not import its synthetic key
under the sandbox provider; rerunning under the normal Windows crypto provider
passed. The pinned Go archive was copied read-only into this branch's own build
cache; no replacement dependency version was downloaded. These initial failures
are not counted as passing runs. No real user profile was opened by any test.

Native recipient registration returning null is now isolated at application
execution. A registry initialization failure may still cause native outgoing
preparation to fail; this change does not claim to repair every provider failure.
No peer authentication failure or historical-pin mismatch is bypassed.

Physical Android acceptance remains pending regardless of host test results.
The physical phone's registration and peer-authentication causes remain unknown.
Its Online/Queued report is **not repaired or accepted by this checkpoint**.

## Test package

Separate user-facing candidate:
`C:/Users/Livel/Documents/Codex/2026-10-14/peerchat-android-groups-voice-session/outputs/android-dev156/PeerChat-4.0.2-dev.156-android-mailbox-debug.apk`.

- SHA-256: `8C6B4DB1233E7C57F56F32B871F58FE0F728444F9207B82F79FE2459EAE540C8`.
- Certificate SHA-256: `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`, verified equal to preserved dev.155.
- `com.peerchat.android`, versionCode `2`, ARM64, minSdk 26, targetSdk 35.
- Version `4.0.2-dev.156` is an isolated Android build override, not a shared
  revision reservation or release allocation. Distinguish it by filename/hash.
- FCM remains unconfigured in this checkout, as in its source build settings;
  this is a foreground/reconnect test candidate, not a push-delivery acceptance.
- Not installed or deployed. No uninstall, data clear, key regeneration or
  migration is required or included. Replacing an app with the same signing
  identity is compatible with retaining its data; verify that on the device.

The original dev.155 diagnostic APK is untouched and remains available for the
previously requested diagnostic comparison. The new candidate does not replace
that evidence or make the existing physical checklist unnecessary.

## Integration order

1. Review the Android-only mailbox fix and its tests as one small commit.
2. Review the independent Opus cross-build prerequisite as a separate commit.
3. Review this audit/manifest independently; retain the base and exact test evidence.
4. Reconcile the other project's committed shared-interface changes before any
   shared group/runtime work; rerun shared and Android regression suites together.
5. Validate preserved-account physical DM sends both directions with the Windows
   PHONE session closed, then reconnect/restart, recipient registration, native
   message IDs, durable receives and ACKs. Never infer delivery from Online.
6. Complete native Android group integration and physical membership/text/history
   acceptance before enabling group UI or advancing group voice.

No automatic merge or cherry-pick into another active checkout is authorized by
this manifest. Review the small commits against the eventual target branch first.
