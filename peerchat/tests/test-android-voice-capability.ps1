$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root ('build/tests/android-voice-capability-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output|Out-Null
for($mask=0;$mask -lt 4;$mask++){
    $flags=@()
    if($mask -band 1){$flags+='-DPEERCHAT_ANDROID_STATIC_RECEIVE'}
    if($mask -band 2){$flags+='-DPEERCHAT_ANDROID_VOICE_ENABLED'}
    & g++ -std=c++17 -Wall -Wextra -Werror -static @flags "-I$root" "$root/tests/android_voice_capability_test.cpp" "$root/android/voice-media/media_capabilities.cpp" -o "$output/capability-$mask.exe"
    if($LASTEXITCODE){throw "Capability configuration $mask did not compile"}
    & "$output/capability-$mask.exe"
    if($LASTEXITCODE){throw "Capability configuration $mask failed"}
}
Write-Output "Capability matrix evidence: $output"
