$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path ([IO.Path]::GetTempPath()) ('android-group-voice-jni-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $root 'portable/peerchat_core.cpp')
$start=$source.IndexOf('extern "C" JNIEXPORT jbyteArray JNICALL Java_com_peerchat_android_GroupVoiceBridge_request(')
if($start -lt 0){throw 'Production GroupVoiceBridge entry point not found'}
$end=$source.IndexOf('extern "C" JNIEXPORT jint JNICALL Java_com_peerchat_android_ReliableBridge_presence(', $start)
if($end -le $start){throw 'Production GroupVoiceBridge boundary not found'}
[IO.File]::WriteAllText((Join-Path $out 'android_group_voice_jni_under_test.inc'),$source.Substring($start,$end-$start).Replace('#endif',''))
Push-Location $root
try {
    & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ "-I$out" tests/android_group_voice_jni_scope_test.cpp -o "$out/group-voice-jni.exe"
    if($LASTEXITCODE){throw 'Group voice JNI boundary compilation failed'}
    & "$out/group-voice-jni.exe"
    if($LASTEXITCODE){throw 'Group voice JNI boundary regression failed'}
    Write-Host "Evidence: $out"
}finally{Pop-Location}
