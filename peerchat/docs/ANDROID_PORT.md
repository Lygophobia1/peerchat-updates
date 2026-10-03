# Android messaging portability foundation

## Current peer-TLS provider checkpoint — 2026-10-02

The user approved host-only TLS preparation without waiting for the phone.
The existing SHA256-only, non-exportable Keystore identity is preserved. Android
now has a profile-bound native `TlsSession` byte-feed provider through
`PeerTlsBridge`, using Bouncy Castle TLS **1.86.1** and support modules **1.86**.
See [ANDROID_PEER_TLS.md](ANDROID_PEER_TLS.md) for exact artifact/source hashes,
license, callback source evidence, limits and trust policy. No socket, discovery,
login, presence or message transport is enabled. Windows remains on Schannel.

Host/build gates: PASS — 1,000 original-message signatures + 100 concurrent;
explicit raw-digest/double-hash rejection; unchanged Windows CNG verification;
TLS1.2/1.3 mutual handshake and invalid pin/certificate/time/provider tests;
100 real native/JNI/Java-TLS lifecycles under `-Xcheck:jni`; 1,000 native provider
contract lifecycles; real TLS1.2 interoperability with unchanged Windows Schannel.
Existing JNI/profile, seven Storage/DPAPI/identity/history/outbox/ACK/dedupe tests,
group envelope and session-scope regressions pass. Existing Windows
discovery/auth/account/device/presence/restart regressions pass. Full isolated
Windows build/package audit passes at **4.0.1-dev.11**. Debug/instrumentation/release
Android builds pass. ARM64 imports, exact Java TLS runtime closure, release hook
isolation and absence of packaged TLS fixtures/credential files pass. The existing
Compose graphics-path 1.0.1 ARM64 library is verified unchanged, not a TLS dependency.

Host commands: `tests/test-peer-tls-host.ps1`, `tests/test-profile-jni-host.ps1`,
`tests/test-portable-storage.ps1`, `tests/test-android-tls-artifacts.ps1`, plus the
Android and Windows build commands below. TLS dependency/signing/native-binding
checkpoints: `ea21ca6`, `5ed6a06`, `a55530a`.

**Physical production Profile/Storage and Keystore-backed TLS remain untested.**
Reconnect the same unlocked, USB-debugging-authorized moto g power (2025).
Keep existing app/data and identity; use the existing replacement-install commands
below. Run the full instrumentation suite, then force-stop and rerun
`NativeProfileTest#existingNormalIdentityIsNotReplaced` and
`PeerTlsTest#existingIdentityNativeMutualTls` in fresh instrumentation processes.
The TLS test runs 100 lifecycles with the ONE existing normal identity, creates no
extra alias/application, and tests synthetic byte traffic without a network.
Do not claim Android/Windows network interoperability from this test.

```powershell
& $adb -s $serial shell am instrument -w -r -e class com.peerchat.android.PeerTlsTest com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner
& $adb -s $serial shell am force-stop com.peerchat.android
& $adb -s $serial shell am instrument -w -r -e class 'com.peerchat.android.NativeProfileTest#existingNormalIdentityIsNotReplaced,com.peerchat.android.PeerTlsTest#existingIdentityNativeMutualTls' com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner
```

Only this physical gate remains for the current provider milestone. After it,
native Node socket/wait portability and server CA/hostname TLS/discovery adapters
are separate next work. SessionTransportManager/DeliveryManager remain native;
there is no parallel Kotlin engine or merge into the Windows/UI branches.

## Current production Profile/Storage boundary — 2026-10-02

The isolated backend branch now binds actual native `Storage`, `Profile`,
`SessionTransportManager` and `DeliveryManager` through `ProfileBridge` and
`AndroidStoragePlatform`. Kotlin supplies SQLite execution, Keystore signing and
PCR1 record protection only; native owners retain schema, transactions, ordering,
history, outbox, ACK and dedupe policy. No network route is enabled; the current
byte-feed TLS provider is described above.

`StoragePlatform` requires instance-owned SQL, record and identity providers.
Android private keys never cross JNI; PCS1 stores the unchanged public identity
with an empty private-key field. Existing databases fail closed when identity
material is missing. Windows default opening/CNG/DPAPI remain unchanged.
Provider references are held until admitted native calls complete; no `JNIEnv`
is cached across threads. A native instance serializes complete synchronous
operations, including transactions, on the calling thread. Future networking
must preserve this storage execution discipline, not move transaction fragments
independently between threads.

Use explicit provider binding (`NativeProfile`), not `pc_core_open_profile(path)`:
the latter intentionally remains unavailable without platform providers. The
activity is still the accepted foundation/security surface; it does not yet
start a messaging engine or open the new native profile automatically.

Current host/build validation is distinct from device execution. The new
`NativeProfileTest` covers 32 native profile reopens, Keystore-backed signing,
history/outbox/dedupe persistence, ACK across reopen, corrupted-record rejection,
missing identity rejection and normal-profile test-hook exclusion. These new
tests have NOT run on the phone. The earlier physical results below validate
the standalone providers, not this new production binding.

Final host/build gate for this binding: PASS for the seven-test portable Storage
set (including DPAPI/history/outbox/ACK/dedupe/tamper), Stage 1B envelope controls,
opaque-profile verification using pinned MbedTLS, existing session authorization
scope, 1,000 native C ABI lifetimes, 1,000 foundation JNI lifetimes and 1,100 JNI
provider-failure lifetimes (100 concurrent). `-Xcheck:jni` reports no JNI misuse.
Debug/test/release APK builds and release lint-vital pass. The ARM64 artifact
audit verifies both libraries, production binding exports, expected Android
dependencies only and test-hook exclusion from release. Full Windows build and
restricted-search package/import audits pass at isolated `4.0.1-dev.10`.
`git diff --check` passes. Existing Windows compiler warnings and Android's
deprecated hardware-security query warning are unchanged; no unrelated fixes
were included.

Debug-only synthetic storage hooks require a `test_` profile and are absent from
the release native library. Test directories/aliases are removed in `finally`.
The normal profile is never deleted or tampered with. PCR1 remains version 1;
its production bound is now 1,048,536 plaintext bytes / 1,048,576 protected bytes,
matching the existing native record ceiling (no format or encryption change).
Signing accepts at most 70,000 bytes, matching native device-payload limits.

Android framework SQLite supplies the database engine. Native schema/UPSERT
semantics are unchanged; older Android vendor SQLite versions below 3.24 are
not validated and may fail closed on these operations. API 26 is an APK minimum,
not proof that every vendor SQLite revision is compatible.

Host commands (no phone required):

```powershell
.\tests\test-portable-storage.ps1
.\tests\test-profile-jni-host.ps1
$env:JAVA_HOME='C:\Program Files\Java\jdk-22'
$env:ANDROID_HOME='C:\Users\Livel\AppData\Local\Android\Sdk'
$env:PEERCHAT_DEPENDENCY_ROOT='C:\Peerchatdep\deps'
.\android\gradlew.bat -p android :app:assembleDebug :app:assembleDebugAndroidTest :app:assembleRelease --console=plain
.\tests\test-android-profile-artifacts.ps1
.\build.ps1 -All
```

Pending physical gate: reconnect the existing moto g power (2025), unlock it,
enable/authorize USB debugging, and retain its installed app/data. Use replacement
installs only (one visible app; instrumentation package is test-only):

```powershell
$adb='C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe'
& $adb devices -l
# Select the authorized device serial; do not uninstall or clear app data.
& $adb -s $serial install -r android\app\build\outputs\apk\debug\app-debug.apk
& $adb -s $serial install -r android\app\build\outputs\apk\androidTest\debug\app-debug-androidTest.apk
& $adb -s $serial shell am instrument -w -r com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner
# A fresh instrumentation process must reopen the same normal native profile.
& $adb -s $serial shell am force-stop com.peerchat.android
& $adb -s $serial shell am instrument -w -r -e class com.peerchat.android.NativeProfileTest#existingNormalIdentityIsNotReplaced com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner
```

Run the persistence-only invocation in several fresh processes, verify the
existing normal Peer ID, and inspect logcat for JNI/SQLite/Keystore/native faults.
The public/signature-only native interoperability fixture must also verify with
the unchanged Windows verifier. No raw record or private key is logged.
TLS host preparation is now approved; do not claim the physical production-path
gate passed until its instrumentation results are obtained.

## Storage integration checkpoints (host validation, phone disconnected)

Android compilation now includes the actual shared `storage.cpp`/`group.cpp`.
Windows-only opening, DPAPI, CNG generation and private-blob import remain under
Windows guards. Android hashing/random/public-signature verification use the
existing pinned MbedTLS 3.6.7 source (`068ff080b369adfac81509f9b57b2afabaf82dc5`),
crypto dependency closure only. Missing providers throw; no portable software
private-key fallback exists. Public key/signature representations remain CNG
P-256/72-byte public and raw 64-byte r/s. A host build selecting portable crypto
verified CNG signatures and the full opaque-profile persistence regression.
ARM64 app/test APK compilation passes with the shared Storage code included.
JNI SQLite/Keystore binding is described above; physical execution is pending.

Step 4 (native host boundary): the explicit `StoragePlatform` constructor requires
database, record and opaque identity providers. Profile create/reopen binds the
same provider, persists the existing PCS1 record with an empty private-key field,
and checks the stored public identity plus a signing proof. Wrong/missing/failing
providers do not regenerate identity or fall back to a private blob. Default
Windows CNG persistence remains unchanged. A host CNG signing control proves
32 profile reopens, protected persistence, history/outbox/ACK/dedupe and absence
of private material in the serialized opaque profile. Android binding/compilation
now includes the actual Storage implementation; APK builds alone do not prove
device execution of this new native path.

Step 3 adds per-instance production `RecordProtector` injection. All object,
message, history and outbox reads/writes use it; default Windows constructors
still select unchanged DPAPI. Missing/empty/oversized provider results fail
closed with no plaintext/decode fallback. Host tests use observed DPAPI (not
an Android Keystore simulation), covering 32 restarts, missing provider/key,
corruption/truncation/tampering, default-provider cross-read and disk scan.
Android PCR1 binding now exists; device validation is pending. These host tests
do not claim to validate Android Keystore runtime behavior.
Run `tests/test-portable-storage.ps1` for the consolidated host regression gate.

Step 2 binds production SQL operations to `SqlDatabase`/`SqlStatement` and
the checked cursor/transaction boundary. Windows opens the same System32 SQLite
implementation; schema SQL and protected record formats are unchanged. An
explicit database constructor permits injection without choosing identity or
record-protection policy yet. Production adapter tests cover 32 reopen cycles,
legacy profile/history readability, durable outbox/dedupe/order/ACK, competing
connection busy rollback and malformed database rejection. This is a portable
execution boundary, not yet an Android SQLite binding.

Step 1 isolates Windows path creation, System32 SQLite opening and busy timeout
in `WindowsStorageConnection`. The production SQL/schema, transactions, DPAPI
contexts, serialization and private-key persistence remain unchanged. Provider
interfaces and independently tested Windows SQL adapters are preparatory only.

The historical `storage_test` tamper assertion was conclusively a fixture bug:
the test erased profile 0 history and then updated a nonexistent `rowid=1`.
It now inserts one synthetic encrypted message, selects it by stable message ID,
and verifies the row exists and becomes zeroed before testing rejection. The
assertion and DPAPI error checks remain intact; production crypto is unchanged.
The complete Storage regression now passes including ciphertext rejection.

The earlier execution-review blocker below is historical: the user has approved
small independently tested production integration steps. No previously rejected
large patch has been applied. Steps 1–4 have passed host checkpoints; production
Android execution remains the final physical gate.

## Backend extraction progress — 2026-10-02 (not a completed milestone)

The current backend branch has an instance-owned `IdentityProvider` boundary.
Provider signatures are checked using the existing Windows verifier; provider
public-key mismatches, signatures from the wrong key, provider failures and a
simultaneously populated private-key blob fail closed. No Android private key
was exported. The provider-backed profile remains unbound to Android Storage.
The legacy Windows store refuses to persist an opaque profile until reopen can
explicitly bind the provider, rather than writing a record it cannot reopen.

`platform/storage_backend.hpp` defines execution-only SQLite cursor/database
interfaces and a checked cursor/transaction helper. A Windows adapter was tested
separately; the production store still uses its established path. Schema,
encryption contexts, history, outbox and delivery policy are unchanged.

Host validation:

- `opaque_identity_test`: 1,000 binding/signing cycles, unchanged CNG verifier,
  bounds, mismatch, provider failure and group signatures passed.
- `storage_backend_test`: protected blobs, integer bounds, rollback, invalid SQL
  rejection and reopen passed.
- `portable_windows_compat_test`: DPAPI compatibility/context binding, identity
  persistence, history, outbox, authenticated ACK and dedupe passed.
- Existing host `portable_core` CTest passed (previously built foundation).
- Android ARM64 debug APK and instrumentation APK built successfully. The
  backend has no JVM unit-test sources; `testDebugUnitTest` reported NO-SOURCE,
  which is not a passing JVM test suite. No device tests ran.
- Full Windows build/package/import audit passed as `4.0.1-dev.4`.

Production Storage adapter integration was rejected by execution safety review
because the proposed persistence refactor was not sufficiently isolated and
independently validated. Rejected patches were not applied. This is an execution
approval blocker, not evidence of a PeerChat storage defect or a phone-only
blocker. Native Android profile reopen, TLS, discovery/authentication, presence
and real message delivery remain NOT IMPLEMENTED. No completed backend milestone
is claimed from these preparatory tests.

This development worktree starts at `c87e380a6991330e83f24863efd302d4ca7c714f`.
The Windows testing worktree and its physical-test packages remain unchanged.

## Scope and current status

This is a **partial foundation**, not a completed Android messaging engine.
The ARM64 native libraries, Compose shell APK and instrumentation APK now build.
Physical installation/runtime validation passed on a Motorola moto g power (2025)
running Android 16/API 36, ARM64, on 2026-10-02. This proves the foundation only;
identity and record protection are now physically validated providers. TLS,
native profile-store binding and real messaging are not implemented yet.
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

Current identity/storage APK SHA256: `7AC781B8E1F7312C71BD7F47299E106692E257FF7BD5BBF362F669084C84FA2F`.
Test APK SHA256: `67306899E9AD9706A8F33577D434D2DB7BA6FCCBE70BB87A2A26ED66E376A249`.
Original foundation-only packaged/stripped library hashes (JNI gains one
representation-normalization method in the identity checkpoint):

- `libpeerchat_messaging.so`: `0FE6F93D843AC0FAB2135C1958E707F9C26D545ADA7C5341D4F3F0B281267B6D`
- `libpeerchat_jni.so`: `0C6F05C742EF0A3A2C8CEB1FD982AD734B1408326E87C1559337DCDBF231877A`

The APK also includes Compose's `libandroidx.graphics.path.so`, ARM64 only.
All three ELF libraries import only Android system libc/libm/libdl, plus JNI's
dependency on the packaged messaging library. C++ runtime archive symbols are
hidden with Android-only `--exclude-libs,ALL`. The exported ABI contains five
`pc_core_*` functions and five `Java_com_peerchat_android_NativeBridge_*` functions
(including the representation-only `normalizeIdentity`).
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
- Android identity and record-provider persistence: PASS. Native profile binding
  and server interoperability: NOT IMPLEMENTED.

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

## Identity and protected records — 2026-10-02

`platform/SecurityProviders.kt` (inside the Android application source tree) exposes
small IdentityProvider and RecordProtector operations. Android APIs stay here; no
platform APIs enter shared protocol/session/delivery code. There are no static JNI
references, cached JNIEnv pointers or global keys. The JNI addition only copies and
normalizes public/signature bytes using the existing native representation helper.
Providers are not yet bound into native Storage; `pc_core_open_profile` deliberately
remains unavailable, and native info does not falsely advertise an opened store.

Identity is AndroidKeyStore EC secp256r1 with SHA256withECDSA. Private material is
non-exportable. Public representation is the existing 72-byte CNG P-256 header/X/Y;
signatures normalize DER to existing 64-byte big-endian r/s. Peer ID hashes all 72
public bytes. An actual Android signature, including Unicode and embedded-NUL
payload, passed unchanged Windows `verifyDevicePayload` in group.cpp; changed
payload failed. No alternate wire representation or crypto algorithm was added.

Aliases are versioned, app-private and profile-hash-scoped (`peerchat.identity.v1.`
and `peerchat.records.v1.`). Tests use `peerchat.test.*` aliases and UUID-namespaced
profile directories. Public identity metadata is atomic; reopen checks it against
Keystore. Missing key, missing metadata with an existing alias, mismatched public
key, provider failure and interrupted initialization fail closed with bounded
recovery errors; there is no replacement-key or plaintext fallback. A creation
lock/file lock prevents concurrent duplicate creation. Cryptographic operations
use fresh Signature/Cipher instances. No raw key, plaintext or blob is logged.

Record format PCR1 is explicitly versioned: 12-byte header (magic, version 1,
AES-GCM algorithm 1, IV length 12, tag length 16, big-endian ciphertext-plus-tag
length), 12-byte provider-generated IV, then ciphertext plus 128-bit tag. AES-256
GCM uses a non-exportable Keystore key with randomized encryption required.
AAD binds the entire header, profile hash and length-prefixed record context.
The originally validated plaintext bound was 65,536 bytes and context 4,096 bytes;
the production integration bound is documented above. Invalid lengths/version,
truncation, modified IV/ciphertext/tag and wrong context/profile fail closed.
This protects records, not rollback of previously valid records; durable history,
ACK/dedupe and database integration remain the existing shared owners' next work.

Physical results on the same moto g power (2025), Android 16/API 36, ARM64:

- Full instrumentation twice: `OK (3 tests)` (22.797s and 22.639s).
- Each suite: 1,000 foundation lifetimes; 250 identity reopen/sign/verify cycles;
  250 record-provider round trips; 40 concurrent sign/protect/unprotect tasks;
  boundary, tamper, empty/binary/Unicode/max-size and recovery assertions.
- Private and AES key `encoded` values are null. Identity KeyInfo reports TEE,
  not StrongBox; this is device-reported local information, not remote attestation.
- Five cold launches kept the same Peer ID and decrypted the same protected
  restart record. A fresh persistence-only process passed `OK (1 test)` (0.148s).
- Tests assert no remaining `peerchat.test.*` aliases and exactly one normal
  identity plus one normal record key. Test directories are removed in finally;
  the public interoperability fixture was removed after the Windows check.
- Only replacement installs were used; no uninstall/clear-data occurred.
  Uninstall/intentional clear-data removes the app identity and protected data;
  the next clean installation creates a different identity. This behavior was
  documented, not destructively tested on the normal profile.
- No PeerChat provider/JNI/Keystore crash/error observed in cold-launch logcat.
  Previously recorded device/vendor warnings remain unrelated.
- Windows DPAPI, identity, history/outbox/ACK/dedupe, group envelope/tamper,
  sender/history UI, three-client fanout and host C/JNI regressions passed.
  Full Windows build/package audit passed in isolated `4.0.1-dev.3`; the
  authoritative Windows tree/packages were untouched. Diff check passed.

Build/install/launch commands above still apply. The existing instrumentation
command now runs three tests; the persistence-only rerun is:

```powershell
& $adb -s $serial shell am force-stop com.peerchat.android
& $adb -s $serial shell am instrument -w -r -e class com.peerchat.android.SecurityProviderTest#productionProfilePersistence com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner
```

## Remaining platform work / next checkpoint

### Production physical gate completed (2026-10-02)

Same Motorola moto g power (2025), Android 16/API 36, arm64-v8a. Replacement
installs only; no uninstall, clear-data or normal identity replacement. SQLite
startup initially failed because Android `execSQL` rejects result-producing
PRAGMAs. The narrow adapter correction drains PRAGMA results through `rawQuery`;
schema and SQL remain unchanged. Physical assertions confirmed WAL, synchronous
FULL, memory temp store and secure delete are actually configured.

Full instrumentation: `OK (9 tests)`; the phase-only restart test is intentionally
skipped in this generic suite and executed separately by
`tests/test-android-production-restart.ps1 -Serial ZA2235NTXZ`.
That driver passed seed/reopen/ACK/reopen/reopen in fresh processes, preserving
history, pending outbox, delivered state, dedupe and protected records. It cleans
only its UUID test profile before five normal-profile cold launches. All five
preserved the identity, reopened Profile/Storage and passed 100 mutual native
Keystore TLS lifecycles each. No test aliases remained. Native/JNI suite also
passed 1,000 foundation lifetimes and the production test 32 profile reopens.

Install and launch (existing authorized device):

```powershell
$adb = 'C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe'
$serial = 'ZA2235NTXZ'
& $adb -s $serial install -r android/app/build/outputs/apk/debug/app-debug.apk
& $adb -s $serial install -r android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
& $adb -s $serial shell am start -W -n com.peerchat.android/.MainActivity
& $adb -s $serial shell am instrument -w -r com.peerchat.android.test/androidx.test.runner.AndroidJUnitRunner
./tests/test-android-production-restart.ps1 -Serial $serial
```

Real Keystore signatures passed the unchanged Windows verifier. Certificate/pin/
provider rejection passed. Full host/shared regressions and Android variants/
ARM64 audit passed; isolated Windows build/package audit passed `4.0.1-dev.12`.
No final app crash or JNI/SQLite/Keystore/TLS error was found. OS/runtime warnings
are documented in `ANDROID_PEER_TLS.md`. The production physical foundation gate
is green; discovery-server trust, native session networking and real messaging
remain subsequent work, not implied by the in-phone mutual TLS harness.

Foundation and identity/record-provider milestones are physically green. The
conditional continuation authorizes secure TLS/discovery/DM interoperability,
but does not authorize exporting this Keystore identity or duplicating the engine.
Native profile binding has now isolated the raw-private-key assumption on Android.
Windows still uses CNG/DPAPI/System32 SQLite; Android Profile/Storage use providers.
Windows TlsCredentials still imports the Windows blob into Schannel. Android
TlsSession now delegates to the opaque profile provider; Node is not yet ported.
Adding only Java sockets
would not prove existing SessionTransportManager/DeliveryManager, trust, ACK or
dedupe behavior. After the production profile physical gate, the next boundary
is Android TLS/certificate signing behind the existing byte-feed/TlsSession
contract and existing
reliable-session path. Do not claim login, presence or real Android/Windows DM
interoperability until those owners actually run on Android.

Physical peer-TLS/profile/record gates are complete. The bounded discovery adapter
now physically passes actual server registration/login/logout, rejected passwords,
relay-only presence, five event reconnects and wrong-pin/untrusted-cert rejection.
It uses pinned BC TLS with instance-local providers, not global registration.
Reproduce using `tests/test-android-discovery-device.ps1 -ServerDirectory
build\development\4.0.1-dev.13\Server`. The server is an isolated current build;
ADB reverse exposes its test endpoint without changing production server config.
No account credentials are hard-coded into the application. This is not yet a
functional account UI, persistent session, or real DM interoperability result.

Remaining messaging implementations: native Node reliable-runtime/socket/wait
separation and application integration; Windows CNG group-AEAD is later group work.
Signing/hash/random/public verification and native profile/SQL/protected-record
providers now have physically validated Android bindings. DeliveryManager and
SessionTransportManager remain the owners; do
not implement substitutes in Kotlin. Discovery/outer relay can use an Android
application adapter analogous to the existing C# services once identity is proven.

Known later limitations retained: friend snapshots select one device; session_info
reports a primary identity; GroupKeyManager rejects duplicate account IDs in the
roster. Android must get its OWN device identity, not replace the Windows identity.
Offline mailbox/FCM, full multi-device repair, DM/group UI, voice, screen and files
are out of this foundation checkpoint.

Foundation source checkpoint: `40ef623`. Android build/runtime preparation is a
separate checkpoint; no branch merge or Windows source/package replacement.
