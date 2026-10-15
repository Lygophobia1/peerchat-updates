param(
    [string]$SdkRoot='C:\Users\Livel\AppData\Local\Android\Sdk',
    [string]$DependencyRoot='C:\Peerchatdep\deps'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$cmake=Join-Path $SdkRoot 'cmake/3.22.1/bin/cmake.exe'
$ninja=Join-Path $SdkRoot 'cmake/3.22.1/bin/ninja.exe'
$ndk=Join-Path $SdkRoot 'ndk/27.0.12077973'
$toolchain=Join-Path $ndk 'build/cmake/android.toolchain.cmake'
$readelf=Join-Path $ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
foreach($required in @($cmake,$ninja,$toolchain,$readelf)){
    if(!(Test-Path -LiteralPath $required)){throw "Missing offline media prerequisite: $required"}
}
$output=Join-Path $root 'build/android-media/arm64-v8a'
New-Item -ItemType Directory -Force $output | Out-Null
& $cmake -S (Join-Path $root 'android/voice-media') -B $output -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$toolchain" '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-26' '-DANDROID_STL=c++_static' '-DCMAKE_BUILD_TYPE=Release' "-DPEERCHAT_DEPENDENCY_ROOT=$($DependencyRoot.Replace('\','/'))" 2>&1 | Tee-Object -FilePath (Join-Path $output 'configure.log')
if($LASTEXITCODE){throw 'Android media configuration failed'}
& $cmake --build $output --target peerchat_android_media_probe --parallel 4 2>&1 | Tee-Object -FilePath (Join-Path $output 'build.log')
if($LASTEXITCODE){throw 'Android media compilation failed'}
$probe=Join-Path $output 'peerchat_android_media_probe'
$elf=& $readelf -h -l -d $probe
if($LASTEXITCODE -or ($elf -join "`n") -notmatch 'Machine:\s+AArch64'){throw 'Media proof is not Android ARM64'}
if(($elf -join "`n") -notmatch 'Requesting program interpreter: /system/bin/linker64'){throw 'Media proof does not use Android linker'}
$needed=[regex]::Matches(($elf -join "`n"),'\(NEEDED\).*?\[([^\]]+)\]') | ForEach-Object {$_.Groups[1].Value}
if(!$needed -or @($needed | Where-Object {$_ -notin @('libc.so','libm.so','libdl.so','liblog.so')}).Count){throw 'Unexpected media runtime dependency'}
$elf | Set-Content -LiteralPath (Join-Path $output 'elf-audit.txt')
[ordered]@{
    opusRevision='ddbe48383984d56acd9e1ab6a090c54ca6b735a6'
    rtcRevision='c6696d157b5612df2a741d9a03b192b47ab6cefb'
    mbedtlsRevision='068ff080b369adfac81509f9b57b2afabaf82dc5'
    mbedtlsConfigSha256=(Get-FileHash -LiteralPath (Join-Path $DependencyRoot 'mbedtls/include/mbedtls/mbedtls_config.h')).Hash
    libjuiceRevision='22145ec3473c73d811417ac9a6aee0de530aaa3f'
    libsrtpRevision='24b3bf8f19b6f5ab4cd2bcceb4f4064efca86fd5'
    usrsctpRevision='fec583d54493f879d2ae44a743423bf8a04371ab'
    plogRevision='94899e0b926ac1b0f4750bfbd495167b4a6ae9ef'
    ndk='27.0.12077973';abi='arm64-v8a';api=26
    executableSha256=(Get-FileHash -LiteralPath $probe).Hash
    runtimeExecuted=$false;apkIntegration=$false;microphone=$false;network=$false
    receive='Pinned NetEq Android artifact still required; no substitute engine'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'build-evidence.json')
Write-Host 'PASS: pinned RTC/ICE/DTLS-SRTP/Opus and Android Audio adapter compile/link for ARM64.'
Write-Host 'COMPILE ONLY: no Android execution, microphone, network call, or physical acceptance.'
