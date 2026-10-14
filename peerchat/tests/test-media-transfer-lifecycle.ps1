param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$native='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'
$out=Join-Path $root ('build\tests\media-transfer-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Copy-Item (Join-Path $ClientDirectory '*.dll') $out
Push-Location $root
$prior=$env:PEERCHAT_SCREEN_TEST_BACKEND
try {
    & g++ -DPEERCHAT_TEST_MEDIA_DIRECT -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. ("-I"+(Join-Path $native 'include')) tests/screen_share_node_test.cpp node.cpp tls.cpp storage.cpp group.cpp group_message_envelope.cpp voice_media.cpp voice_receive_engine.cpp group_voice_session.cpp voice_media_topology.cpp group_voice_bridge.cpp screen_media_topology.cpp group_screen_share_bridge.cpp audio.cpp platform/go_hpke_provider.cpp -Wno-cast-function-type ("-L"+(Join-Path $native 'lib')) -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o "$out\lifecycle.exe" -lwinmm -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -luser32
    if($LASTEXITCODE){throw 'Concurrent media/transfer fixture compile failed'}
    $env:PEERCHAT_SCREEN_TEST_BACKEND='compat'
    & "$out\lifecycle.exe"
    if($LASTEXITCODE){throw 'Concurrent media/transfer lifecycle failed'}
    Write-Output "Evidence: $out"
}finally{$env:PEERCHAT_SCREEN_TEST_BACKEND=$prior;Pop-Location}
