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

## Resume procedure

1. Confirm the user is ready to accept the Windows administrator prompt; do not automatically retry canceled elevation.
2. Launch the prerequisite script through elevated Windows PowerShell, without changing execution policy, keeping its helper window hidden. Preserve the process handle and inspect its exit code as well as `build/wsl-setup-*/prerequisite.log`.
3. On exit 3010 or any restart-required output, **stop and ask the user to restart**. Do not run a reboot command or continue distro/toolchain setup across this gate. On failure, diagnose the recorded error; do not assume installation succeeded.
4. Once the prerequisite is enabled and no restart is pending, install the official Microsoft WSL2 package and official Ubuntu distribution through supported WSL commands. Record exact versions and confirm the distribution uses WSL2 before downloads/builds. Do not modify existing distributions or Windows development tools.
5. Use a separate Linux dependency checkout at WebRTC `6cf6bcb585b31414aac02377df2bc8fec770d0e8`. Keep the existing Windows source tree, its `.gclient`, tools, worktrees and accepted packages unchanged. Follow [dependency build and ABI/runtime gates](ANDROID_RECEIVE_DEPENDENCY.md), including the NDK/API compatibility checks. Record Ubuntu packages, compiler versions, GN arguments, dependency revisions and artifact hashes.
6. Continue the [voice implementation plan](2026-10-17-architecture-and-completion-plan.md) only with honest dependency/runtime evidence; genuine DM integration precedes group application integration. No physical acceptance, phone installation or Pi deployment is implied.

Microsoft source: [WSL installation](https://learn.microsoft.com/en-us/windows/wsl/install), including the VirtualMachinePlatform no-restart prerequisite. No unverified third-party audio library is proposed.
