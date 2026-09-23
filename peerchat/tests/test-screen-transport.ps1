param([switch]$Run)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    if (-not (Test-Path 'build\native\install\lib\libdatachannel.a')) {
        throw 'Pinned libdatachannel dependencies must be built before this targeted media test.'
    }
    & "$projectRoot\scripts\build-screen-media.ps1" -OutputDirectory 'build\stage8-transport'
    if ($LASTEXITCODE -ne 0) { throw 'MSVC screen media adapter build failed.' }
    $output = 'build\stage8-transport\screen-media-transport-test.exe'
    & g++ -I. -Ibuild\native\install\include -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror `
        tests\screen_media_transport_test.cpp -Lbuild\native\install\lib `
        -ldatachannel -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto `
        -leverest -lp256m -lssp -o $output -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -luser32
    if ($LASTEXITCODE -ne 0) { throw 'H.264 media transport test build failed.' }
    if ($Run) {
        & (Resolve-Path $output).Path
        if ($LASTEXITCODE -ne 0) { throw 'H.264 media transport test failed.' }
    }
} finally { Pop-Location }
