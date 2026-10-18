# Approved WSL2 build-environment setup

The user approved WSL2 with Ubuntu for the genuine Android WebRTC NetEq build, with no automatic restart and no Raspberry Pi changes. Approval does not authorize rebooting or substituting the pinned media dependency.

## Initial attempt

- Existing branch: `codex/native-unicode-emoji`, clean baseline `65769bc`.
- Windows registry: Windows 10 Home 22H2, build 19045.6466.
- C: available space approximately 274.6 GB at inspection; recheck before dependency sync.
- Existing `wsl.exe` has installation commands but no demonstrated usable distribution. Optional-feature inspection requires administrator elevation; virtualization firmware availability remains unverified.
- Added `scripts/enable-wsl2-prerequisite.ps1`. It enables only VirtualMachinePlatform, only after administrator consent, with `-NoRestart`; pending state or RestartNeeded exits 3010. A final feature-state check is required before reporting ready. No reboot, distribution install or remote access is in the script.
- PowerShell parser validation passed. Astra architecture review confirmed the limited operation and restart boundary. This is not execution validation of the feature installation.
- The elevated PowerShell launch showed a Windows consent process but ultimately returned **The operation was canceled by the user**. No setup transcript was created and the helper did not execute. No features, Ubuntu distribution or build dependencies were installed by this attempt. No reboot was initiated.

## Approved retry — restart required

The user explicitly requested a retry. Elevated PowerShell launched but exited 1 before creating a transcript. Policy inspection showed all execution-policy scopes Undefined (the usual restrictive Windows default); no security policy was changed. This is a likely explanation for the helper not starting, not a captured script error.

The approved operation was then performed directly with Microsoft's built-in installer:

```powershell
dism.exe /Online /Enable-Feature /FeatureName:VirtualMachinePlatform /All /NoRestart
```

The elevated process exited **3010**. Evidence: `build/wsl-vmp-install-20261017.log`, completed 2026-10-17 21:18 local time. The installer explicitly reports `Reboot required=yes` and `Restart suppressed by /NoRestart command line switch`. VirtualMachinePlatform installation succeeded pending restart. Setup stopped at this boundary: no WSL package/Ubuntu installation, Linux dependency downloads, reboot command, phone installation or Pi access followed. Existing PeerChat source/packages and Windows development tools were preserved.

## After first user restart — WSL installed; second restart required

- User reported restart and approved continuing. Elevated read-only inspection confirmed VirtualMachinePlatform **Enabled** and boot time 2026-10-17 21:19:17 local; evidence `build/wsl-postrestart-features.txt`.
- Downloaded official stable Microsoft WSL release `3.0.3`, published 2026-10-09, from `https://github.com/microsoft/WSL/releases/download/3.0.3/wsl.3.0.3.0.x64.msi` (367,575,040 bytes). SHA-256 `ae977286974255bd26bf925f00f40f5692c6b2084155cbc0563c09aea091f211` matched GitHub release metadata. Authenticode status **Valid**, signer Microsoft Corporation.
- Installed with `msiexec /i <verified-msi> /qn /norestart REBOOT=ReallySuppress /L*v <log>`. Exit 0; log `build/wsl-3.0.3-install.log`. No automatic restart occurred.
- `C:\Program Files\WSL\wsl.exe --version` reports WSL 3.0.3.0, kernel 6.18.54.1-1, WSLg 1.0.79, Windows 10.0.19045.6466.
- Actual `--status`/distribution-list operations reject with `Wsl/WSL_E_WSL_OPTIONAL_COMPONENT_REQUIRED`. Thus this machine also requires `Microsoft-Windows-Subsystem-Linux`; the earlier VMP-only expectation from the modern offline instructions was insufficient here. No claim that Ubuntu is registered or running is made.
- Enabled that required component with `dism.exe /Online /Enable-Feature /FeatureName:Microsoft-Windows-Subsystem-Linux /All /NoRestart`. Exit **3010**, restart required. Evidence `build/wsl-component-install-20261017.log`. Stop at the user's restart boundary again. No Ubuntu install, WebRTC dependency sync, phone install or Pi access followed.

## After second restart — Ubuntu build environment running

- User confirmed the second restart. WSL status succeeds with default version 2; no existing distributions were present.
- Installed official `Ubuntu-24.04` using WSL's web-download path, named `PeerChat-Build`, location `C:\Peerchatdep\wsl\PeerChat-Build`, explicit version 2 and no automatic launch. The pinned WebRTC install-dependencies script lists Ubuntu noble as supported.
- Runtime verified: Ubuntu **24.04.5 LTS**, kernel **6.18.54.1-microsoft-standard-WSL2**, x86_64. WSL lists this distribution as version 2. Initial memory availability about 14 GiB; Windows C: free about 270 GB. Linux's sparse VHD capacity is not the physical Windows free-space budget.
- Created non-admin `peerchatbuild` (UID 1000), made it the distro's default user, and installed Ubuntu prerequisites: ca-certificates, git, curl, python3, python3-venv, build-essential, pkg-config, ninja-build, lsb-release, unzip, zip, xz-utils, zstd and file. Apt updated dependencies inside this new distribution only; no Windows development tools were replaced. Inventory: `build/wsl-linux-toolchain-inventory.txt` and Ubuntu `/var/log/apt/history.log`.
- Official Chromium depot_tools cloned to `/home/peerchatbuild/deps/depot_tools`, revision `d38909eb68d85b91259f4bf20fe5ebcd38461bc2`; commands use `DEPOT_TOOLS_UPDATE=0` to retain that revision. The first bootstrap exited before completion; retry initialized its pinned CIPD tools and `gclient help sync` passed.
- Separate Linux checkout: `/home/peerchatbuild/deps/webrtc-6cf6bcb-android`. `scripts/android-neteq.gclient` enables Android dependencies without editing the Windows `.gclient`. Sync uses `--no-history --shallow --nohooks --revision src@6cf6bcb585b31414aac02377df2bc8fec770d0e8 -j 4 --output-json sync-revisions.json`; `sync.log` retains output. The source pin was verified during synchronization. At this stage, compilation/runtime proof was still pending; later build and link evidence is recorded below.
- Build recipe and separate copied GN target are being validated. GN's `--root-target=//peerchat_audio_receive` allows loading the adapter without changing WebRTC's root BUILD.gn. The Windows GN file and accepted packages remain unchanged.

## Pinned dependency preparation and first compilation

- `gclient sync` and `gclient runhooks` completed successfully. Retained Linux evidence: `sync.log`, `sync-revisions.json` and `hooks.log` under `/home/peerchatbuild/deps/webrtc-6cf6bcb-android`. The resource-download hook took 345 seconds; the quiet interval was downloads, not a deadlocked build. Debian bullseye host sysroots and pinned Python tools were installed by the upstream hooks.
- GN/autoninja initially failed because gclient's vpython bootstrap had not created `python3_bin_reldir.txt`. Running the pinned depot_tools `ensure_bootstrap` initialized those wrappers without updating its Git revision. The build script checks this prerequisite.
- GN generation succeeded with the alternate root target. GN introspection must also receive `--root-target=//peerchat_audio_receive`; otherwise it loads the default graph and reports the adapter absent. Generated Ninja regeneration retains the alternate root automatically.
- The first compilation uses the unchanged pinned NDK30 dependency and Chromium Clang24 as a **dependency proof only**, not an application-ready Android artifact. It builds the complete non-thin ARM64 archive and directly linked existing receive test with six parallel jobs. `build-neteq.log` retains compiler output. Compilation/link/runtime results must be recorded separately; generation alone is not validation.
- A read-only compiler-path review found a supported `target_sysroot` override for NDK27 alignment, including C++ headers, API26 CRT objects and target libraries. `android_ndk_root` is not an overridable GN argument. The planned aligned build must independently verify commands and runtime; the first NDK30 proof must not be silently linked into the NDK27 application. The GN test defaults to shared libc++, whereas the existing app uses static libc++ behind hidden C ABI boundaries.
- No accepted package, Windows dependency checkout, Android phone or Raspberry Pi was modified. The requested agent preference is Astra low for delegated tasks.

## NDK27-aligned archive and independent link proof

- Built the genuine pinned WebRTC `6cf6bcb585b31414aac02377df2bc8fec770d0e8` ARM64 NetEq receive archive in the separate WSL checkout. The target uses the NDK 27 target sysroot with Chromium Clang 24, Android/API 26, `rtc_rust=false`, a complete non-thin static archive, and the pinned adapter's receive-only configuration. Evidence and outputs are retained under `build/android-neteq/arm64-v8a`; the source-side build remains `/home/peerchatbuild/deps/webrtc-6cf6bcb-android/src`.
- Compilation required the isolated WebRTC workaround in `audio_receive/webrtc-ndk-iterators.patch` for unsafe iterator constructions. The archive contains 999 ARM64 ELF object members. The saved archive was independently linked in both normal and whole-archive modes using the NDK27 target sysroot and runtime inputs; both links passed. Proof artifacts, including `proof-link.map`, APK and `runtime.log`, are under `build/tests/android-receive-proof-27a2d0af8350498187a5d797def5e732`.
- The earlier ARM64 emulator codec-only run failed the direct Opus silence assertion. Subsequent QEMU and Android x86_64 codec-only evidence is recorded below; these are distinct tests and do not establish application call readiness.
- The emulator result exercises ARM64 binaries through emulator ARM64 translation only. It is not a physical ARM64 device performance, latency, battery, audio-route, or call-quality result. No physical-device performance claim is made.
- This advances the archive and independent link gates only. It does not validate the production APK integration, end-to-end authenticated calls, or physical acceptance. No phone installation or Raspberry Pi deployment was performed as part of this build proof.

## ARM64/Bionic receive suite under QEMU

- Added Ubuntu `qemu-user` and the AArch64 GNU C++ cross compiler to the WSL environment. Installed versions: `qemu-user 1:8.2.2+ds-0ubuntu1.18`, `g++-aarch64-linux-gnu 4:13.2.0-7ubuntu1`; `aarch64-linux-gnu-g++ --version` reports Ubuntu GCC **13.3.0-6ubuntu2~24.04.1 (13.3.0)**. The retained artifact reports QEMU `8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18)`.
- The full unchanged ARM64/Bionic receive and mixer suite passed under QEMU. Evidence: `build/tests/android-receive-qemu-8f13301bb33f433eb892a6b0dd5c3c4b/runtime.log`; `elf.txt` confirms an AArch64 ELF64 executable and `qemu-version.txt` records the emulator version. The passing log includes source-to-silence, packet loss/reordering/jitter, lifecycle, and one-to-eight-source mixer checks.
- This is functional emulation evidence only. QEMU measurements are not physical ARM64 performance results, and the run does not cover Android app lifecycle, microphone, network calls, routing, or physical call quality.
- A separate native Android x86_64 codec-only diagnostic passed. The corresponding ARM64 codec-only test through Android's x86_64 `libndk_translation` emulator fails the direct Opus silence threshold across the Chromium Clang 24/NDK27 and NDK Clang18/NDK27 NEON/scalar variants. Saved failing logs include `build/tests/android-receive-proof-e4ddaefcdcc84b0ca0e94735077cfe87/runtime.log` and `build/tests/android-receive-proof-0e33bee8c25247a3b450fcb8732396bd/runtime.log`.
- The QEMU pass and translated-emulator failure point the investigation toward a possible translation/runtime boundary, but no exact instruction-level cause has been identified. The failed silence check remains open; no application voice completion or physical acceptance is claimed.

## Earlier restart procedure (completed)

1. Ask the user to save their work and perform the second Windows restart manually. Wait for confirmation that it is complete; do not repeat installation while the feature is pending.
2. Recheck both optional components and installed `C:\Program Files\WSL\wsl.exe --status` after restart. The WSL MSI is already installed; do not reinstall unnecessarily. The earlier prerequisite script checks only VMP, not the additional component now demonstrated necessary. Do not change execution policy. Preserve installer exit status and logs.
3. On exit 3010 or any restart-required output, **stop and ask the user to restart**. Do not run a reboot command or continue distro/toolchain setup across this gate. On failure, diagnose the recorded error; do not assume installation succeeded.
4. Once both prerequisites are enabled and no restart is pending, verify the already-installed Microsoft WSL package and install the official Ubuntu distribution through supported WSL commands. Record exact versions and confirm the distribution uses WSL2 before downloads/builds. Do not modify existing distributions or Windows development tools.
5. Use a separate Linux dependency checkout at WebRTC `6cf6bcb585b31414aac02377df2bc8fec770d0e8`. Keep the existing Windows source tree, its `.gclient`, tools, worktrees and accepted packages unchanged. Follow [dependency build and ABI/runtime gates](ANDROID_RECEIVE_DEPENDENCY.md), including the NDK/API compatibility checks. Record Ubuntu packages, compiler versions, GN arguments, dependency revisions and artifact hashes.
6. Continue the [voice implementation plan](2026-10-17-architecture-and-completion-plan.md) only with honest dependency/runtime evidence; genuine DM integration precedes group application integration. No physical acceptance, phone installation or Pi deployment is implied.

Microsoft source: [WSL installation](https://learn.microsoft.com/en-us/windows/wsl/install), including the VirtualMachinePlatform no-restart prerequisite. No unverified third-party audio library is proposed.
