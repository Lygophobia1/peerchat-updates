# Android messaging portability foundation

This development worktree starts at `c87e380a6991330e83f24863efd302d4ca7c714f`.
The Windows testing worktree and its physical-test packages remain unchanged.

## Scope and current status

This is a **partial foundation**, not a completed Android messaging engine or APK.
The native target reuses the existing `SessionTransportManager` and PT1 format
code; it includes the existing DeliveryManager declarations but does **not**
instantiate delivery/history until a real profile storage provider is available.
Node remains the Windows orchestration boundary. Its Windows sockets, TLS,
storage and media code are deliberately not compiled into this first target.
There is no copied Android messaging engine, dummy crypto, plaintext storage,
alternative retry scheduler or invented device identity.

Implemented:

- `portable/peerchat_core.h`: version 1 C ABI, fixed-width values, non-reused handle
  IDs, copied caller-owned UTF-8 buffers, bounded sizes, explicit statuses and
  exception containment. `pc_core_open_profile` fails with
  `PC_CORE_PLATFORM_UNAVAILABLE` until real providers are implemented.
- Existing PT1 string encode/parse is used for the exchange proof. This is NOT a
  sent message, authenticated session, DM interoperability or delivery proof.
- `portable/utf8.hpp`: strict standard UTF-8 and UTF-16-unit limit accounting.
  Existing Windows protocol conversion remains unchanged behind its default
  compile path. Only the portable target selects the portable conversion path.
- `portable/identity_format.hpp`: CNG-compatible 72-byte P-256 public layout and
  64-byte raw r/s signature normalization. Curve/signature validation and SHA-256
  remain real cryptographic-provider operations, not custom implementations.
- `platform/RecordProtector`: narrow record protection interface. Windows Storage
  delegates to the extracted DPAPI implementation with the same record description,
  entropy/context, flags and error semantics. No format/migration change.
- Kotlin/Compose `com.peerchat.android` shell and small byte-array JNI adapter.
  Only arm64-v8a is configured. No account/chat UI or permissions are added.
- Host C ABI, host JVM JNI, Windows storage/identity compatibility tests and an
  Android instrumentation test source.

The handle registry is process-wide bookkeeping, **not** a singleton core/profile.
Each instance owns its session policy separately. Calls are serialized per instance;
an admitted call retains its instance while destroy invalidates future lookup.
Callers still must provide valid pointer extents. Invalid arbitrary memory cannot
be made safe by a C ABI. No native thread invokes WPF, Compose or JNI callbacks.

## Architecture

```text
Compose UI -> Android application adapter -> byte-array JNI -> portable C ABI
                                                       -> existing session policy
                                                       -> existing protocol format
Future real profile -> existing storage/delivery/group logic -> platform providers
```

Windows retains its existing PInvoke ABI and all-features `build.ps1` unchanged.
`portable/CMakeLists.txt` is a separate explicit-source build with no RTC, Opus,
APM, AudioReceive, ScreenMedia, WinMM, WGC, MF or D3D11 dependencies. Its Windows
host DLL imports only KERNEL32/msvcrt in the current host proof; that is not an
Android ELF/import audit.

## Tooling and build

Actually used for host validation: TDM GCC 10.3.0, Visual Studio's installed CMake
and CTest, and Oracle JDK 22.0.2 (`C:\Program Files\Java\jdk-22`) for JNI.

**No Android SDK/NDK/Gradle/adb was found in PATH, Android environment variables or
the checked standard installation locations. No Android SDK/NDK version has been
used or proven.** No tools were installed/downloaded.

Project pins, selected for compatibility rather than claimed installed:

| Tool/component | Required |
|---|---|
| Android Gradle Plugin | 8.7.3 |
| Gradle | 8.9 |
| Kotlin + Compose compiler plugin | 2.0.21 |
| JDK | 17+; host JDK 22 is present |
| Android platform/target | API 35; min SDK 26 |
| SDK build tools | 34.0.0 |
| NDK | 27.0.12077973 |
| Android SDK CMake | 3.22.1 |
| ABI | arm64-v8a |
| Test execution | adb plus an arm64 device/emulator |

AGP compatibility reference:
https://developer.android.com/build/releases/agp-8-7-0-release-notes

Once these tools are available, from this worktree's `peerchat` directory:

```powershell
.\android\build.ps1 -SdkRoot '<installed SDK path>' -Gradle '<Gradle 8.9 executable>'
.\android\build.ps1 -SdkRoot '<installed SDK path>' -Gradle '<Gradle 8.9 executable>' -ConnectedTests
```

The script preflights without installing anything. A Gradle wrapper is not yet
generated/downloaded because Gradle is absent. Gradle's first successful build
will require the pinned Maven artifacts if not cached.

Expected outputs (NOT produced yet):

- `android/app/build/outputs/apk/debug/app-debug.apk`
- APK libraries `lib/arm64-v8a/libpeerchat_messaging.so`, `libpeerchat_jni.so`
- Native debug intermediates under `android/app/.cxx/`

Android version is `Android dev.1`, independent of Windows package numbering.

## Validation performed

- Host CMake shared-library build/CTest: PASS.
- 1,000 native lifecycle cycles: PASS; Unicode, embedded NUL, invalid UTF-8,
  bounded buffer sizing/copy, untouched undersized buffers, invalid/stale handles,
  generation-independent instance retirement and concurrent lookup/destroy.
- Actual JNI C++ bridge on host JVM with `-Xcheck:jni`, 1,000 cycles: PASS.
  This is **not** Android runtime validation or Kotlin/Compose compilation.
- Windows DPAPI old/new provider cross-read and wrong-context rejection: PASS.
- P-256 generator public-key vector Peer ID:
  `0bc654178e6f3d5e4ab8b170722a7b7a388a76d58c97031fcad019cf4bdb5e91`: PASS.
- Windows raw ECDSA verification, identity persistence/reopen, encrypted history,
  durable outbox, authenticated ACK and dedupe: PASS.
- Existing group envelope/tamper/key rotation/protected history regression: PASS.
- Existing group sender/history UI and three-client ciphertext fanout: PASS.
- Windows all-features build/package restricted dependency/load audit: PASS,
  isolated output `build/development/4.0.1-dev.1/Package`. This is a regression
  build in this Android worktree, not a replacement for authoritative dev.61/.62.
- Android preflight: correctly BLOCKED on missing SDK/NDK/Gradle tools.
- Android APK, ELF, Kotlin/Compose build, emulator, physical Android, Android
  identity persistence and server interoperability: NOT TESTED / NOT IMPLEMENTED.

## Remaining platform work / next checkpoint

First provision/point to the agreed Android toolchain, compile the shell and run
the existing JNI instrumentation proof. Only after that succeeds, add real P-256
generation/signing and a Keystore-wrapped software identity with restart proof.
Do not call the existing non-exportable key/TLS problem solved by this scaffold.

Remaining messaging implementations: CNG signing/hash/random/AEAD provider,
Schannel replacement behind existing `TlsSession` contract, bundled SQLite and
profile-bound record protection, portable filesystem paths and Node socket/wait
separation. DeliveryManager and SessionTransportManager remain the owners; do
not implement substitutes in Kotlin. Discovery/outer relay can use an Android
application adapter analogous to the existing C# services once identity is proven.

Known later limitations retained: friend snapshots select one device; session_info
reports a primary identity; GroupKeyManager rejects duplicate account IDs in the
roster. Android must get its OWN device identity, not replace the Windows identity.
Offline mailbox/FCM, full multi-device repair, DM/group UI, voice, screen and files
are out of this foundation checkpoint.

The source preparation is uncommitted pending actual Android build/runtime proof.
