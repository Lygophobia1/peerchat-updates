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
$opus=Join-Path $DependencyRoot 'opus'
foreach($required in @($cmake,$ninja,$toolchain,$readelf,(Join-Path $opus 'CMakeLists.txt'))){
    if(!(Test-Path -LiteralPath $required)){throw "Missing offline voice foundation prerequisite: $required"}
}
$source=Join-Path $root 'android/voice-foundation'
$output=Join-Path $root 'build/android-voice-foundation/arm64-v8a'
New-Item -ItemType Directory -Force $output | Out-Null
& $cmake -S $source -B $output -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$toolchain" '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-26' '-DANDROID_STL=c++_static' '-DCMAKE_BUILD_TYPE=Release' "-DPEERCHAT_OPUS_SOURCE=$($opus.Replace('\','/'))"
if($LASTEXITCODE){throw 'Android voice codec configuration failed'}
& $cmake --build $output --target peerchat_android_opus_probe --parallel 4
if($LASTEXITCODE){throw 'Android voice codec compilation failed'}
$probe=Join-Path $output 'peerchat_android_opus_probe'
$elf=& $readelf -h -l -d $probe
if($LASTEXITCODE -or ($elf -join "`n") -notmatch 'Machine:\s+AArch64'){throw 'Voice codec proof is not Android ARM64'}
if(($elf -join "`n") -notmatch 'Requesting program interpreter: /system/bin/linker64'){throw 'Voice codec proof does not use the Android linker'}
$needed=[regex]::Matches(($elf -join "`n"),'\(NEEDED\).*?\[([^\]]+)\]') | ForEach-Object {$_.Groups[1].Value}
if(!$needed -or @($needed | Where-Object {$_ -notin @('libc.so','libm.so','libdl.so')}).Count){throw 'Unexpected voice codec runtime dependency'}
$elf | Set-Content -LiteralPath (Join-Path $output 'elf-audit.txt')
[ordered]@{
    opusRevision='ddbe48383984d56acd9e1ab6a090c54ca6b735a6';ndk='27.0.12077973';abi='arm64-v8a';api=26
    executableSha256=(Get-FileHash -LiteralPath $probe).Hash
    sourceSha256=(Get-FileHash -LiteralPath (Join-Path $root 'tests/opus_voice_test.cpp')).Hash
    runtimeExecuted=$false;apkIntegration=$false;microphone=$false;network=$false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output 'build-evidence.json')
Write-Host "PASS: existing pinned Opus compiles/links for Android ARM64: $probe"
Write-Host 'COMPILE ONLY: no device execution, ICE/TURN/DTLS-SRTP, APM, audio device or call acceptance.'
