param([string]$OutputDirectory='build\tests\transfer-session',[string]$NativeCache='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08',[switch]$Benchmark,[string]$NodeSource='node.cpp')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    Copy-Item -LiteralPath (Join-Path (Split-Path (Get-Command g++).Source -Parent) 'libssp_64-0.dll') -Destination $OutputDirectory -Force
    & g++ -DPEERCHAT_TEST_SHORT_RECONNECT_BACKOFF -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. ("-I"+(Join-Path $NativeCache 'include')) tests/transfer_session_test.cpp $NodeSource tls.cpp storage.cpp group.cpp group_message_envelope.cpp voice_media.cpp voice_receive_engine.cpp audio.cpp platform/go_hpke_provider.cpp -Wno-cast-function-type ("-L"+(Join-Path $NativeCache 'lib')) -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o "$OutputDirectory\transfer-session-test.exe" -lwinmm -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi
    if($LASTEXITCODE -ne 0){throw 'File session test build failed'}
    if($Benchmark){ & "$OutputDirectory\transfer-session-test.exe" --benchmark }else{ & "$OutputDirectory\transfer-session-test.exe" }
    if($LASTEXITCODE -ne 0){throw 'File session test failed'}
} finally {Pop-Location}
