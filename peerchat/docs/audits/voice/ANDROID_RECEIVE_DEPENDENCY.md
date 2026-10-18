# Android NetEq receive dependency audit

Status: **genuine Android ARM64 receive archive built and independently link-proven; full ARM64/Bionic receive and mixer suite passes under QEMU 8.2.2; ARM64 codec-silence check still fails under Android x86_64 translation; application voice readiness remains unvalidated**. The audit history below records earlier states. No physical-device performance or end-to-end call acceptance is established.

## Build progress — 2026-10-17

- The approved WSL2 Ubuntu build environment is running. The separate Linux checkout is `/home/peerchatbuild/deps/webrtc-6cf6bcb-android/src`, pinned to `6cf6bcb585b31414aac02377df2bc8fec770d0e8`. The Windows dependency checkout and accepted packages remain separate and untouched by this build.
- The receive archive was built for ARM64 with the NDK 27 target sysroot and Chromium Clang 24, API 26, `rtc_rust=false`, and a complete non-thin static archive. The isolated WebRTC checkout uses the documented patch for unsafe iterator constructions. Output and configuration evidence are under `build/android-neteq/arm64-v8a`; the archive contains 999 ARM64 ELF object members.
- Independent links against the archive passed in both normal and whole-archive modes. The proof directory is `build/tests/android-receive-proof-27a2d0af8350498187a5d797def5e732`; its saved `runtime.log` contains passing receive lifetime, bounds/malformed-input, timing, packet ordering, reorder and loss/FEC cases.
- At the time of this initial progress entry, an Android ARM64-under-x86_64-translation codec-only run failed the direct Opus silence assertion. Later QEMU and comparison results are recorded below. The Android translation failure remains open and these results do not establish production app voice readiness or successful end-to-end calls.
- The emulator proof is limited to ARM64 execution through emulator ARM64 translation. It provides no evidence about physical ARM64 device performance, latency, battery use, audio routing, or call quality. No phone installation or Pi deployment was performed.

## Cross-architecture runtime evidence — 2026-10-17

- Added Ubuntu `qemu-user` and the AArch64 GNU C++ cross compiler to the WSL build environment. The installed package versions are `qemu-user 1:8.2.2+ds-0ubuntu1.18` and `g++-aarch64-linux-gnu 4:13.2.0-7ubuntu1`; the compiler reports `aarch64-linux-gnu-g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0`. The retained emulator version file reports `qemu-aarch64 8.2.2`.
- The unchanged ARM64/Bionic receive and mixer suite passes under QEMU. Evidence is in `build/tests/android-receive-qemu-8f13301bb33f433eb892a6b0dd5c3c4b/runtime.log`; `elf.txt` identifies an AArch64 ELF64 executable, and `qemu-version.txt` records QEMU 8.2.2. The log includes the source-becomes-silent assertion, loss/reorder/jitter cases, lifecycle checks, and 1–8-source mixing checks.
- The codec-only Android x86_64 diagnostic passes. It exercises the synthetic Opus codec-silence function only; it is not a NetEq or receive/mixer test and does not validate the production Android app.
- On the Android x86_64 emulator using `libndk_translation`, the ARM64 codec-only silence assertion fails across the Chromium Clang 24/NDK27 and the NDK Clang18/NDK27 NEON/scalar diagnostic variants. The retained proof logs report loud energy `45186613`, silent energy `38194940`, and failure of `silent < loud/10` (for example, `build/tests/android-receive-proof-e4ddaefcdcc84b0ca0e94735077cfe87/runtime.log` and `build/tests/android-receive-proof-0e33bee8c25247a3b450fcb8732396bd/runtime.log`).
- The different ARM64 results on QEMU and `libndk_translation` suggest a translation/runtime boundary as the investigation focus. No exact instruction-level root cause has been established. QEMU functional success does not resolve the emulator codec failure or establish physical-device behavior.

## Existing implementation and pins

### Subsequent application link and native-emulator proof

- The full, unchanged receive/mixer suite passed natively as x86_64 Android JNI (not ARM translation): `build/tests/android-receive-proof-ad1d90fadfa8422da36b35219cb129e3/runtime.log`. Genuine x64 archive SHA-256 `671d90a6a95d4bfc4a8561dbd28196138662449a98ccefd1a9334d9da190a14e`; all 1006 members are ELF64 X86-64 relocatables. The separate GN output changes only target CPU and matched NDK27 unwind path. The codec-only log is now retained at `build/tests/opus-ndk27-comparison/codec-x64-proof/runtime.log`.
- Actual ARM64 `peerchat_jni` and `peerchat_messaging` targets linked successfully with the complete genuine archive in `build/android-voice-integration/arm64-v8a`. This dependency-link checkpoint retains application voice capability OFF. The messaging SO hash is `a00f6205d1a10ba6887af9fbd6d2eb052f45292ccc0c229cfacf79c63091d7a6`; JNI SO hash `433e97cb13c311079514378f8b085d2bb2837ab384b6a7604c134d0209c70089`. Both remain behind the existing C ABI; final release/package hashes will differ after application integration.
- CMake uses the receive archive's matching WebRTC Opus headers and encoder/decoder together, rather than also linking the old standalone Opus. Final ARM64 messaging ELF loads are 16-KiB aligned, with only `liblog`, `libdl`, `libm`, and `libc` dynamic requirements. Archive symbols remain hidden; static libc++ is private to each C ABI library. No duplicate-symbol suppression is used.
- These are synthetic functional/ABI results, not microphone, network, physical ARM64 performance, or call acceptance. Android app foreground ownership, explicit consent, authenticated TURN and real call integration remain separate gates.

- The pinned WebRTC checkout is `C:\Peerchatdep\deps\webrtc-complete-src\src`, revision `6cf6bcb585b31414aac02377df2bc8fec770d0e8` (verified with `git -c safe.directory=... rev-parse HEAD`). Its parent `.gclient` currently sets `checkout_android: False`.
- `audio_receive/peerchat_audio_receive.cpp` wraps WebRTC `DefaultNetEqFactory`, its Opus decoder, a device-paced clock, and `AudioMixer` behind the existing `pc_rx_*` C ABI in `audio_receive/peerchat_audio_receive.h`. `audio_receive/BUILD.gn` lists the NetEq, Opus, environment, and mixer GN dependencies, but currently declares a Windows-oriented shared library.
- `scripts/build-audio-receive.ps1` builds the x64 Windows `PeerChat.AudioReceive.dll`. Its result at `C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64\PeerChat.AudioReceive.dll` is **not** an Android library. The metadata records SHA-256 `82621EF0C83C6F4844DF0871FE05D7E637F0287179B94CC19CDF272C9DB40C01` for that Windows DLL; it proves only the Windows adapter checkpoint.
- `voice_receive_engine.cpp` already selects direct `pc_rx_*` symbols under `__ANDROID__ && PEERCHAT_ANDROID_STATIC_RECEIVE`; Windows keeps dynamic loading. `android/voice-media/CMakeLists.txt` accepts an ARM64 static archive in `PEERCHAT_ANDROID_RECEIVE_LIBRARY` and defines that receive gate when supplied. `android/voice-media/media_capabilities.cpp` reports unavailable otherwise. The Android JNI target is linked through `portable/CMakeLists.txt` and `android/app/src/main/cpp/CMakeLists.txt`.
- The existing Android media CMake project pins Opus `ddbe48383984d56acd9e1ab6a090c54ca6b735a6`, libdatachannel `c6696d157b5612df2a741d9a03b192b47ab6cefb`, MbedTLS `068ff080b369adfac81509f9b57b2afabaf82dc5`, libjuice `22145ec3473c73d811417ac9a6aee0de530aaa3f`, libsrtp `24b3bf8f19b6f5ab4cd2bcceb4f4064efca86fd5`, usrsctp `fec583d54493f879d2ae44a743423bf8a04371ab`, and plog `94899e0b926ac1b0f4750bfbd495167b4a6ae9ef`.

## Earlier host evidence and blocker (superseded by the WSL setup below)

The pinned `build/config/BUILDCONFIG.gn` asserts `host_os == "linux"` for `target_os == "android"`, with message `Android builds are only supported on Linux.` The earlier probe is documented in `docs/ANDROID_CHECKPOINT_AND_REMAINING_WORK.md`; its saved log was not present in this checkout. This restriction is in the pinned build toolchain, not evidence that NetEq itself lacks Android support.

This Windows host has Android SDK and NDK `27.0.12077973`, but `wsl.exe --status` returned exit 50 with no status text, `wsl.exe --list --quiet` returned usage text and exit 1, and HKCU Lxss had no distribution entries. No `docker`, `podman`, `vagrant`, or Ubuntu command was found. Windows optional-feature state could not be read without elevation, so it is not claimed disabled. There is no runnable Linux environment demonstrated here. A Linux root filesystem alone does not provide a Linux execution environment on Windows. No configured remote Linux build host was found in the project Android handoff/checkpoint documentation; the Raspberry Pi was not queried.

The WebRTC tree occupies approximately 9.15 GB across 442,392 files. Its Android NDK directory is absent because `.gclient` disabled Android checkout. Pinned `DEPS` requests Android NDK package version `2@30.0.16248370`; the app uses NDK `27.0.12077973`, `arm64-v8a`, and minSdk/API 26. **Correction from the 2026-10-17 source review:** Chromium's `build/config/android/config.gni` declares a minimum-SDK default of 29, but this WebRTC checkout's root `.gn` overrides **both** `default_min_sdk_version` and `android_ndk_api_level` to **23**. Set both explicitly to 26; changing only the first does not override the independent native-API default. These are integration checks, not reasons to raise the app's supported API level.

The Android compiler is also not simply the compiler bundled with the NDK. `build/toolchain/android/BUILD.gn` uses the Chromium Clang toolchain; `tools/clang/scripts/update.py` pins `llvmorg-24-init-7747-g62397f8b`, subrevision `31`. The NDK supplies the Android sysroot. Record both toolchain identities separately. The app's NDK 27 linker/runtime must not be assumed compatible with newer LLVM bitcode or every NDK 30 libc++ symbol.

## Build sequence from the original audit

The separate WSL build has since completed the archive and independent-link steps below. The remaining app-link, runtime, and physical-device gates are still open.

On an appropriate Linux build host, use a separate pinned WebRTC checkout so the existing Windows dependency checkout remains intact:

1. Check out WebRTC `6cf6bcb585b31414aac02377df2bc8fec770d0e8`, configure `.gclient` to fetch Android dependencies, and run `gclient sync` and the pinned hooks. Confirm the full source and dependency revisions and the installed Android toolchain. This sync may download substantial data; its size was not measured.
2. Copy the existing `audio_receive` adapter files into that checkout as a dedicated GN target. For Android, use `static_library("peerchat_audio_receive")`, set `complete_static_lib = true`, and remove `//build/config/compiler:thin_archive` from its configs. Keep the existing dependency list and adapter exception configuration. These two archive settings are both required by the pinned `build/config/compiler/BUILD.gn`: a complete thin archive can still reference objects in the build directory and is not a portable deliverable. Do not alter the Windows shared-library target. Inspect the resulting archive and its dependency closure; complete static libraries and shared/system dependencies are not automatically flattened indiscriminately.
3. Generate an Android ARM64 release output with the baseline arguments below. Confirm the effective arguments and compilation commands against the pinned checkout. Register the dedicated adapter target in the isolated checkout's root build graph, as the Windows helper already does for its own checkout. Run `gn gen out/peerchat-android-arm64`, `gn desc out/peerchat-android-arm64 //peerchat_audio_receive:peerchat_audio_receive deps --all`, and build that target through the pinned `autoninja` wrapper. Root `.gn` defaults `use_siso=true`; do not assume a plain system Ninja invocation is equivalent. If deliberately selecting Ninja, set and record `use_siso=false` and validate that path.
4. Supply only the resulting Android ARM64 complete static archive to `PEERCHAT_ANDROID_RECEIVE_LIBRARY`; build the actual app JNI library and APK. A successful GN archive alone does not establish Android runtime compatibility or a working receive pipeline.

This sequence is an implementation plan, **not a build result**. A reproducible script could be drafted on Windows, but its GN generation, dependency sync, compilation, and link behavior cannot be validated until Linux is available.

```gn
target_os = "android"
target_cpu = "arm64"
is_debug = false
is_official_build = false
is_component_build = false
rtc_include_tests = false
rtc_build_examples = false
rtc_build_tools = false
default_min_sdk_version = 26
android_ndk_api_level = 26
use_custom_libcxx = false
use_custom_libcxx_for_host = true
use_thin_lto = false
```

`use_thin_lto=false` intentionally produces ordinary objects for the downstream app link, avoiding a newer Chromium LLVM bitcode dependency on the app's NDK 27 linker. Verify the archive members are AArch64 ELF objects rather than LLVM bitcode; do not infer this solely from the argument. `use_custom_libcxx=false` selects the target toolchain's libc++ headers instead of Chromium's in-tree C++ runtime; it does **not** make NDK 30 headers/runtime ABI identical to NDK 27. The host retains its normal in-tree libc++ through `use_custom_libcxx_for_host=true`. Keep exceptions enabled on the adapter because its C ABI catches failures and its source explicitly throws on decoder setup errors; this does not require enabling exceptions throughout WebRTC.

## Link integration hazards verified in the source review

- The existing app produces both `libpeerchat_messaging.so` and `libpeerchat_jni.so`. No explicit `ANDROID_STL` setting was found in the checked Gradle/CMake sources; the NDK CMake default is static libc++. Existing targets hide archive symbols and communicate through a C ABI. Preserve that boundary, verify actual generated link commands, and do not accidentally embed Chromium's separate C++ runtime in the receive archive. A coherent runtime choice is required for all participating libraries. Android recommends one shared libc++ runtime for applications with multiple native shared libraries and matching NDK versions where possible; a successful link alone is insufficient ABI evidence. See [Android C++ runtime guidance](https://developer.android.com/ndk/guides/cpp-support).
- WebRTC's `third_party/opus/README.chromium` records vendored Opus revision `55513e81d8f606bd75d0ff773d2144e5f2a732f5`, whereas PeerChat's separate encoder build pins `ddbe48383984d56acd9e1ab6a090c54ca6b735a6`. `//modules/audio_coding:webrtc_opus` directly depends on `//third_party/opus`, so simply setting `rtc_build_opus=false` does not remove every bundled Opus dependency. Two static Opus implementations in one final shared object risk duplicate definitions or link-order-dependent selection. Inspect a final link map and symbol providers, then explicitly choose and validate one implementation or deliberate symbol isolation. Do not suppress duplicate-symbol errors or silently substitute the encoder pin merely to complete the link.
- A static archive does not carry GN system-link requirements into CMake. Inspect effective `libs`, `lib_dirs`, propagated flags, undefined symbols, and the final link, including any Android logging dependencies. Express required system libraries on the imported CMake target instead of relying on an unrelated dependency to supply them accidentally.
- The GN Android configuration sets a 16 KiB maximum page size on its final ARM64 links, but those linker flags do not travel inside a static archive. The final CMake-built shared libraries and APK alignment must independently pass 16 KiB checks. The current app uses NDK 27, so do not assume the newer GN configuration fixes downstream ELF or APK alignment. See [Android page-size guidance](https://developer.android.com/guide/practices/page-sizes).

Smallest implementation remains the unchanged `pc_rx` C ABI and genuine NetEq adapter as a complete Android archive, with explicit runtime/Opus/system-library integration in the existing native owner. First compile with the pinned Linux toolchain, then perform the real app link and ABI/runtime checks. If the NDK 30-to-27 runtime combination fails, align the Android build toolchain deliberately and retest the existing messaging/JNI components; do not patch out the host assertion, replace NetEq, or claim compatibility from archive creation alone.

## Acceptance gates for the dependency

- Record source revision, GN args, Android dependency revisions, adapter hashes, dependency closure, archive hash, and final APK/JNI hashes. Preserve WebRTC and transitive license notices in package materials; the pinned WebRTC source has a BSD-style license, while its dependency closure needs a separate notice audit.
- Verify AArch64 ELF objects, API 26 symbols and minimum Android API, C++ runtime and exception settings across the WebRTC archive and app's NDK 27 build. The pinned WebRTC NDK 30 versus app NDK 27 combination must pass a real link and runtime check. Inspect both final `libpeerchat_messaging.so` (the VoiceMedia/NetEq owner) and `libpeerchat_jni.so` with `llvm-readelf` for machine, `NEEDED` libraries, and unresolved symbols; avoid duplicate or incompatible C++ runtimes.
- Execute `pc_rx_*` ABI and WebRTC Opus/NetEq receive tests on ARM64, including packet insertion, loss concealment, device-paced 10 ms pulls, multi-source mixing, generation reset, teardown, and bounded diagnostics. Then exercise authenticated ICE/DTLS-SRTP to speaker playback on Android. Windows host tests and an ELF link check do not prove audible two-way calls.
- Keep production voice capability disabled until the genuine receive library is linked, exercised, and the remaining permissions, audio ownership, TURN, and call lifecycle integration pass their own checks. A library file by itself is insufficient.

No source, toolchain, host feature, phone, or Raspberry Pi state was changed for this audit.
