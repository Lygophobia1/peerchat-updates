param([string]$NativeCache='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root ('build\tests\one-to-one-receive-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output|Out-Null
Push-Location $root
try {
    Copy-Item -LiteralPath 'C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64\PeerChat.AudioReceive.dll' -Destination $output
    Copy-Item -LiteralPath 'C:\Peerchatdep\artifacts\audio-processing\webrtc-6cf6bcb-voice-presets\x64\PeerChat.AudioProcessing.dll' -Destination $output
    Copy-Item -LiteralPath (Join-Path (Split-Path (Get-Command g++).Source -Parent) 'libssp_64-0.dll') -Destination $output
    & g++ -DPEERCHAT_TEST_MEDIA_DIRECT -DPEERCHAT_TEST_SHORT_CALL_TIMEOUTS -DPEERCHAT_TEST_SHORT_RECONNECT_BACKOFF -I. ('-I'+(Join-Path $NativeCache 'include')) -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests\voice_call_test.cpp node.cpp tls.cpp storage.cpp group.cpp group_message_envelope.cpp voice_media.cpp voice_receive_engine.cpp audio.cpp ('-L'+(Join-Path $NativeCache 'lib')) -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m -lssp -lwinmm -liphlpapi -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -o "$output\voice-call-test.exe"
    if($LASTEXITCODE -ne 0){throw 'One-to-one CallManager/receive integration compilation failed'}
    $previous=$env:PATH
    try {
        $env:PATH="$env:WINDIR\System32;$env:WINDIR"
        & "$output\voice-call-test.exe"
        if($LASTEXITCODE -ne 0){throw 'One-to-one CallManager/receive integration failed'}
    }finally{$env:PATH=$previous}
}finally{Pop-Location}
