param(
 [string]$TurnHost='172.27.162.181',
 [int]$TurnPort=34790,
 [string]$TurnUsername='peerchat-test',
 [string]$TurnPassword='fixture-only',
 [string]$NativeDependencyRoot='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08',
 [string]$ReceiveDll='C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64\PeerChat.AudioReceive.dll',
 [string]$ProcessingDll='C:\Peerchatdep\artifacts\audio-processing\webrtc-6cf6bcb-voice-presets\x64\PeerChat.AudioProcessing.dll'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path ([IO.Path]::GetTempPath()) ('android-group-voice-media-'+[guid]::NewGuid().ToString('N'))
foreach($required in @($ReceiveDll,$ProcessingDll,(Join-Path $NativeDependencyRoot 'lib/libdatachannel.a'))){if(!(Test-Path -LiteralPath $required)){throw "Missing existing genuine dependency: $required"}}
New-Item -ItemType Directory -Path $output -Force | Out-Null
Copy-Item -LiteralPath $ReceiveDll,$ProcessingDll -Destination $output
Copy-Item -LiteralPath 'C:\TDM-GCC-64\bin\libssp_64-0.dll' -Destination $output
Push-Location $root
try{
 $exe=Join-Path $output 'group-voice-media.exe'
 $ErrorActionPreference='Continue'
 & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -static-libgcc -static-libstdc++ -I. "-I$NativeDependencyRoot/include" tests/android_group_voice_media_integration.cpp tests/android_voice_media_integration_audio.cpp group_voice_session.cpp voice_media_topology.cpp voice_media.cpp voice_receive_engine.cpp "-L$NativeDependencyRoot/lib" -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o $exe -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi 2> (Join-Path $output 'compile.stderr.txt')
 $compileCode=$LASTEXITCODE
 $ErrorActionPreference='Stop'
 if($compileCode){Get-Content -LiteralPath (Join-Path $output 'compile.stderr.txt') -TotalCount 100;throw 'Genuine group media host compilation failed'}
 & $exe (Join-Path $output 'PeerChat.AudioReceive.dll') $TurnHost $TurnPort $TurnUsername $TurnPassword 2>&1 | Tee-Object -FilePath (Join-Path $output 'result.log')
 if($LASTEXITCODE){throw "Genuine group media host test failed; evidence $output"}
 Write-Output "Evidence: $output"
}finally{Pop-Location}
