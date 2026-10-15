param([string]$SdkRoot='C:\Users\Livel\AppData\Local\Android\Sdk')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'build/android-media/host-tests'
New-Item -ItemType Directory -Force $out | Out-Null
# JNI declaration only. The fixture supplies a VM table and never opens a device.
Copy-Item -LiteralPath (Join-Path $SdkRoot 'ndk/27.0.12077973/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include/jni.h') -Destination (Join-Path $out 'jni.h')
& g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ (Join-Path $root 'tests/android_audio_buffer_test.cpp') -o (Join-Path $out 'buffers.exe')
if($LASTEXITCODE){throw 'Android PCM buffer regression compile failed'}
& (Join-Path $out 'buffers.exe') | Tee-Object -FilePath (Join-Path $out 'buffers.txt')
if($LASTEXITCODE){throw 'Android PCM buffer regression failed'}
& g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ "-I$out" (Join-Path $root 'tests/android_audio_lifecycle_test.cpp') (Join-Path $root 'portable/audio_android.cpp') -o (Join-Path $out 'lifecycle.exe')
if($LASTEXITCODE){throw 'Android Audio JNI lifecycle regression compile failed'}
& (Join-Path $out 'lifecycle.exe') | Tee-Object -FilePath (Join-Path $out 'lifecycle.txt')
if($LASTEXITCODE){throw 'Android Audio JNI lifecycle regression failed'}
