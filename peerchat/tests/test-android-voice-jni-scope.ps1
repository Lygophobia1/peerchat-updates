$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path ([IO.Path]::GetTempPath()) ('android-voice-jni-scope-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
# Compile the exact production entry point with boundary ports supplied by the
# test. This is a host JNI-command unit test, not an Android or media build.
$source=Get-Content -Raw -LiteralPath (Join-Path $root 'portable/peerchat_core.cpp')
$start=$source.IndexOf('extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_VoiceBridge_request(')
if($start -lt 0){throw 'Production VoiceBridge request entry point not found'}
$end=$source.IndexOf('extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ReliableBridge_presence(', $start)
if($end -le $start){throw 'Production VoiceBridge request boundary not found'}
[IO.File]::WriteAllText((Join-Path $out 'android_voice_jni_request_under_test.inc'),$source.Substring($start,$end-$start))
Push-Location $root
try {
    & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ "-I$out" tests/android_voice_jni_scope_test.cpp -o "$out/voice-jni-scope.exe"
    if($LASTEXITCODE){throw 'Voice JNI command unit compilation failed'}
    & "$out/voice-jni-scope.exe"
    if($LASTEXITCODE){throw 'Voice JNI command unit regression failed'}
    Write-Host "Evidence: $out"
}finally{Pop-Location}
