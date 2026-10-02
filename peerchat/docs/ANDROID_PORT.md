# Android messaging portability foundation

This development worktree starts at `c87e380a6991330e83f24863efd302d4ca7c714f`.
The Windows testing worktree and its physical-test packages remain unchanged.

## Scope and current status

This is a **partial foundation**, not a completed Android messaging engine.
The ARM64 native libraries, Compose shell APK and instrumentation APK now build.
Physical installation/runtime validation passed on a Motorola moto g power (2025)
running Android 16/API 36, ARM64, on 2026-10-02. This proves the foundation only;
identity, secure storage, TLS and real messaging are still not implemented.
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

Verified Android SDK root: `C:\Users\Livel\AppData\Local\Android\Sdk`.
SDK paths need not be added to global PATH. Set JAVA_HOME to the installed JDK;
the helper resolves the SDK explicitly and uses the committed Gradle wrapper.
NDK Clang 18.0.1 builds the ARM64 targets with API 26 as their minimum platform.
Gradle 8.9 and AGP 8.7.3 successfully build with Oracle JDK 22.0.2; no JDK downgrade
or plugin upgrade was needed. SDK processing emits a nonfatal XML-version warning
with this newer installed SDK manager; compilation and packaging succeed.

Verified project pins:

| Tool/component | Required |
|---|---|
| Android Gradle Plugin | 8.7.3 |
| Gradle | 8.9 |
| Kotlin + Compose compiler plugin | 2.0.21 |
| JDK | Oracle 22.0.2 used; Java/Kotlin target 17 |
| Android platform/target | API 35; min SDK 26 |
| SDK build tools | 34.0.0 |
| NDK | 27.0.12077973 |
| Android SDK CMake | 3.22.1 |
| ABI | arm64-v8a |
| Test execution | adb 1.0.41 / platform-tools 37.0.1; physical ARM64 device |

AGP compatibility reference:
https://developer.android.com/build/releases/agp-8-7-0-release-notes

From this worktree's `peerchat` directory:

```powershell
$env:JAVA_HOME = 'C:\Program Files\Java\jdk-22'
.\android\build.ps1 -SdkRoot 'C:\Users\Livel\AppData\Local\Android\Sdk'
# Build the instrumentation APK without requiring a device:
$env:ANDROID_HOME = 'C:\Users\Livel\AppData\Local\Android\Sdk'
Push-Location android
.\gradlew.bat :app:assembleDebug :app:assembleDebugAndroidTest --console=plain
Pop-Location
# When a usable ARM64 device is attached:
.\android\build.ps1 -SdkRoot $env:ANDROID_HOME -ConnectedTests
```

The script preflights without installing SDK tools. The committed wrapper downloads
Gradle 8.9 and verifies distribution SHA256:
`d725d707bfabd4dfdc958c624003b3c80accc03f7037b5122c4b1d0ef15cecab`.
The first build downloads pinned Maven dependencies if not already cached.
No global Git, Java, Android or Windows build configuration was changed.

Produced outputs, relative to this isolated worktree's `peerchat` directory:

- `android/app/build/outputs/apk/debug/app-debug.apk`
- `android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk`
- APK libraries `lib/arm64-v8a/libpeerchat_messaging.so`, `libpeerchat_jni.so`
- Unstripped libraries: `android/app/build/intermediates/cxx/Debug/592r5010/obj/arm64-v8a/`
  (the CMake configuration identifier can change on another checkout).
- Standalone ARM64 native regression executable: `build/android-foundation/arm64/portable_core_test`.

APK SHA256: `24B8121A520B16DD9B9874467929E507922D7CB5EFA0343CDCF04CDA9266A6C1`.
Test APK SHA256: `783E07A095608FA255FDFDFE3A3FEED6ABAD01FA19FFA66AB2E5D8BEA98A2FE0`.
Exact packaged/stripped library hashes:

- `libpeerchat_messaging.so`: `0FE6F93D843AC0FAB2135C1958E707F9C26D545ADA7C5341D4F3F0B281267B6D`
- `libpeerchat_jni.so`: `0C6F05C742EF0A3A2C8CEB1FD982AD734B1408326E87C1559337DCDBF231877A`

The APK also includes Compose's `libandroidx.graphics.path.so`, ARM64 only.
All three ELF libraries import only Android system libc/libm/libdl, plus JNI's
dependency on the packaged messaging library. C++ runtime archive symbols are
hidden with Android-only `--exclude-libs,ALL`. The exported ABI is exactly the five
`pc_core_*` functions and four `Java_com_peerchat_android_NativeBridge_*` functions.
APK metadata confirms min 26, target/compile 35 and launcher MainActivity; debug
signature verification passes. No Windows/media libraries are packaged.

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
  initial isolated output `build/development/4.0.1-dev.1/Package`, rerun after the
  Android build changes: `build/development/4.0.1-dev.2/Package`. These are regression
  build in this Android worktree, not a replacement for authoritative dev.61/.62.
- Android Gradle app and instrumentation builds, ARM64 CMake library/test compile,
  Kotlin/Compose compile, ELF imports/exports, APK contents/signature: PASS.
- Android instrumentation compiles coverage for 1,000 lifetimes, Unicode/NUL,
  invalid UTF-8, maximum/oversized/empty input and invalid/stale handles.
- Physical Android install, Compose render, ART/JNI execution, five cold launches,
  scoped logcat inspection and standalone ARM64 native regression: PASS (see below).
- Android identity persistence and server interoperability: NOT IMPLEMENTED.

## Physical device validation — 2026-10-02

- Manufacturer/model: Motorola `moto g power - 2025` (`vegas`).
- Android 16, API 36; primary/supported ABI `arm64-v8a` only.
- ADB initially reported unauthorized; after the user accepted the debugging
  prompt, status became `device`. No driver/configuration change was needed.
- Existing debug APK and test APK installed with `install -r`: both `Success`.
- MainActivity cold launch: `Status: ok`; Compose accessibility hierarchy showed
  `Native foundation: Loaded`, `ABI version: 1`, `Passed (100 cycles)`.
- Native loader explicitly reported `base.apk!/lib/arm64-v8a/libpeerchat_jni.so: ok`.
  Its dependency `libpeerchat_messaging.so` was exercised by all four JNI methods.
  JNI binds exported methods by name; there is no separate RegisterNatives table.
- Instrumentation: `OK (1 test)`, 0.919 seconds, 1,000 JNI/native lifecycle cycles
  plus boundary assertions. Unicode, embedded NUL, invalid/stale/negative handles,
  empty/65,536-byte/oversized input and malformed UTF-8 all passed.
- Standalone ARM64 C ABI test: PASS, 1,000 lifecycle cycles, UTF-8/PT1, copied
  output buffers/canaries, bounded/error handling, concurrent handle retirement
  and public/signature representation. Exception containment on malformed UTF-8
  is exercised by both tests. No live session start/stop exists in this foundation.
- Five additional force-stop/cold-start cycles, each with a new process and a
  rendered 100-lifetime proof: PASS. No subsequent-launch crash or initialization
  ordering failure observed; this is not an exhaustive memory-leak proof.
- Installed package reports `primaryCpuAbi=arm64-v8a`. Pulling its base.apk and
  hashing it confirmed an exact match to the APK SHA256 recorded above. Native
  code maps directly from APK entries; separate extracted .so paths are not needed.
- App-scoped logcat: no UnsatisfiedLinkError, JNI error, missing PeerChat native
  symbol, SIGSEGV/SIGABRT, fatal signal, uncaught Java/Kotlin exception or native
  initialization failure. ART logcat reports `-Xcheck:jni` enabled.
- Nonfatal device/platform messages were present: vendor QT `file does not exist`,
  ashmem pinning deprecation, MediaTek perfctl output, HWUI optional
  `fbcNotifySbeRescue`/`fbcNotifyHintFrame`/`updateSbeFeatureOption` symbols unavailable,
  and SELinux denial for the vendor mbrain service. These did not prevent rendering
  or tests; they are not PeerChat JNI-library symbol failures. They were recorded,
  not hidden or worked around with product changes.
- No production source changed. Existing Windows compatibility/build/package
  results remain applicable; Windows suites were not rerun for this docs-only pass.
  Authoritative Windows worktree and its packages remain untouched.

## Exact device validation commands

From the isolated `peerchat` directory, attach/unlock an ARM64 phone, enable USB
debugging and accept its authorization prompt. Replace the serial below with
the device shown as `device`, not `unauthorized` or `offline`:

```powershell
$adb = 'C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe'
& $adb devices -l
$serial = '<authorized-device-serial>'
& $adb -s $serial shell getprop ro.product.cpu.abi # must be arm64-v8a
& $adb -s $serial install -r android/app/build/outputs/apk/debug/app-debug.apk
& $adb -s $serial shell am start -W -n com.peerchat.android/.MainActivity
# Expected UI: Native foundation Loaded; ABI 1; Passed (100 cycles).
& $adb -s $serial install -r android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
& $adb -s $serial shell am instrument -w -r com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner
# Require OK (1 test), not just a successful adb process exit.
$appPid = (& $adb -s $serial shell pidof com.peerchat.android).Trim()
if ($appPid) { & $adb -s $serial logcat -d "--pid=$appPid" }
# Inspect for linker/JNI errors, FATAL EXCEPTION, signals and unexpected failures.
& $adb -s $serial shell am force-stop com.peerchat.android
& $adb -s $serial shell am start -W -n com.peerchat.android/.MainActivity
```

For the same native C ABI boundary test on ARM64, the already-built executable
and its library can also be run independently of ART:

```powershell
& $adb -s $serial shell mkdir -p /data/local/tmp/peerchat-foundation
& $adb -s $serial push build/android-foundation/arm64/portable_core_test /data/local/tmp/peerchat-foundation/
& $adb -s $serial push build/android-foundation/arm64/libpeerchat_messaging.so /data/local/tmp/peerchat-foundation/
& $adb -s $serial shell chmod 700 /data/local/tmp/peerchat-foundation/portable_core_test
& $adb -s $serial shell 'cd /data/local/tmp/peerchat-foundation && LD_LIBRARY_PATH=. ./portable_core_test'
```

## Remaining platform work / next checkpoint

The physical foundation milestone is complete with no remaining runtime blocker.
The next separately authorized milestone may add real P-256
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

Foundation source checkpoint: `40ef623`. Android build/runtime preparation is a
separate checkpoint; no branch merge or Windows source/package replacement.
