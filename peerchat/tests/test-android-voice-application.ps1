$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/android-voice-application-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $root
try {
    & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/android_voice_application_test.cpp -o "$out/voice-application.exe"
    if($LASTEXITCODE){throw 'Voice application unit compilation failed'}
    & "$out/voice-application.exe"
    if($LASTEXITCODE){throw 'Voice application unit regression failed'}
    Write-Host "Evidence: $out"
}finally{Pop-Location}
