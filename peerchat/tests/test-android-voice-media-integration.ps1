param(
 [string]$NativeDependencyRoot='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08',
 [string]$ReceiveDll='C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64\PeerChat.AudioReceive.dll',
 [string]$ProcessingDll='C:\Peerchatdep\artifacts\audio-processing\webrtc-6cf6bcb-voice-presets\x64\PeerChat.AudioProcessing.dll'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path ([IO.Path]::GetTempPath()) ('android-voice-media-integration-'+[guid]::NewGuid().ToString('N'))
$profiles=Join-Path ([IO.Path]::GetTempPath()) ('pc-voice-integration-'+[guid]::NewGuid().ToString('N'))
foreach($required in @($ReceiveDll,$ProcessingDll,(Join-Path $NativeDependencyRoot 'lib/libdatachannel.a'))){if(!(Test-Path -LiteralPath $required)){throw "Missing existing genuine dependency: $required"}}
New-Item -ItemType Directory -Path $output -Force | Out-Null
Copy-Item -LiteralPath $ReceiveDll,$ProcessingDll -Destination $output
Copy-Item -LiteralPath 'C:\TDM-GCC-64\bin\libssp_64-0.dll' -Destination $output
Get-FileHash -LiteralPath $ReceiveDll,$ProcessingDll | Format-List | Out-File -LiteralPath (Join-Path $output 'dependency-hashes.txt')
Push-Location $root
try{
 $exe=Join-Path $output 'voice-media-integration.exe'
 # No audio.cpp/portable/audio_android.cpp: only the explicit synthetic device
 # port is substituted. All call, signaling, TLS, media and receiver code is real.
 $ErrorActionPreference='Continue' # GCC may emit a harmless long-line note on stderr.
 & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -static-libgcc -static-libstdc++ -DPEERCHAT_TEST_MEDIA_DIRECT -DPEERCHAT_ANDROID_STATIC_RECEIVE -DPEERCHAT_ANDROID_VOICE_ENABLED -I. "-I$NativeDependencyRoot/include" tests/android_voice_media_integration.cpp tests/android_voice_media_integration_audio.cpp tls.cpp storage.cpp group.cpp voice_media.cpp voice_receive_engine.cpp android/voice-media/media_capabilities.cpp "-L$NativeDependencyRoot/lib" -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o $exe -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi 2> (Join-Path $output 'compile.stderr.txt')
 $ErrorActionPreference='Stop'
 if($LASTEXITCODE){Get-Content -LiteralPath (Join-Path $output 'compile.stderr.txt') -TotalCount 90;throw 'Genuine host DM integration compilation failed'}
 Write-Output "HOST ONLY, shared Android controller + genuine Windows media/NetEq; no physical audio. Profiles retained: $profiles"
 & $exe $profiles 2>&1 | Tee-Object -FilePath (Join-Path $output 'result.log')
 if($LASTEXITCODE){throw "Genuine host DM integration failed; evidence $output"}
 Get-FileHash -LiteralPath $exe | Format-List
 Write-Output "Evidence: $output"
}finally{Pop-Location}
