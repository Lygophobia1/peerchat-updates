param([string]$SdkRoot='C:\Users\Livel\AppData\Local\Android\Sdk',[string]$Distro='PeerChat-Build')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$compiler=Join-Path $SdkRoot 'ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
$readelf=Join-Path $SdkRoot 'ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
$receive=Join-Path $root 'build/android-neteq/arm64-v8a'
$archive=Join-Path $receive 'libpeerchat_audio_receive.a'
if((Get-FileHash $archive).Hash -ne 'FCEF3218613460CA02EAE2BD83F997A5A7E6A3097440A7C9E2B19F6472BD47E4'){throw 'Unexpected receive proof archive'}
$output=Join-Path $root ('build/tests/android-receive-qemu-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output|Out-Null
$binary=Join-Path $output 'receive-bionic-static'
& $compiler --target=aarch64-linux-android26 -std=c++17 -O2 -ffunction-sections -fdata-sections "-I$root/audio_receive" "-I$receive/include" "$root/audio_receive/audio_receive_test.cpp" "$root/tests/android-receive-proof/qemu_log.cpp" $archive -static -static-libstdc++ '-Wl,--gc-sections,-z,max-page-size=16384' -lm -ldl -o $binary
if($LASTEXITCODE){throw 'Static Android receive proof did not link'}
$elf=& $readelf -h -l -d $binary
if($LASTEXITCODE -or ($elf -join "`n") -notmatch 'Machine:\s+AArch64' -or ($elf -join "`n") -match '\(NEEDED\)|Requesting program interpreter'){throw 'Expected standalone Android ARM64 executable'}
$elf|Set-Content (Join-Path $output 'elf.txt')
$wsl='C:\Program Files\WSL\wsl.exe'
$linuxBinary=& $wsl -d $Distro -u peerchatbuild --exec wslpath -a $binary
if($LASTEXITCODE){throw 'Could not resolve isolated proof executable in WSL'}
& $wsl -d $Distro -u peerchatbuild --exec qemu-aarch64 --version | Tee-Object (Join-Path $output 'qemu-version.txt')
if($LASTEXITCODE){throw 'QEMU prerequisite unavailable'}
& $wsl -d $Distro -u peerchatbuild --exec qemu-aarch64 $linuxBinary 2>&1 | Tee-Object (Join-Path $output 'runtime.log')
if($LASTEXITCODE){throw 'Android ARM64 receive proof failed under QEMU'}
Get-FileHash $archive,$binary
Write-Output "PASS ARM64/Bionic QEMU receive proof: $output"
Write-Output 'Functional emulation only; no Android app lifecycle, microphone, network or physical-performance acceptance.'
