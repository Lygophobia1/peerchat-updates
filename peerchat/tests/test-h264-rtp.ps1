$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $output = Join-Path $root 'build\tests\h264-rtp'
    New-Item -ItemType Directory -Force -Path $output | Out-Null
    $test = Join-Path $output 'h264-rtp-receiver-test.exe'
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I. tests\h264_rtp_receiver_test.cpp -o $test
    if ($LASTEXITCODE -ne 0) { throw "RTP reassembly test build failed ($LASTEXITCODE)." }
    & $test
    if ($LASTEXITCODE -ne 0) { throw "RTP reassembly regression failed ($LASTEXITCODE)." }
    $native = if ($env:PEERCHAT_NATIVE_DEPENDENCY_ROOT) { $env:PEERCHAT_NATIVE_DEPENDENCY_ROOT } else {
        'C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $native 'lib\libdatachannel.a'))) {
        throw 'Pinned shared libdatachannel artifact is required; this test never downloads/builds dependencies.'
    }
    $integration = Join-Path $output 'h264-packetizer-recovery-test.exe'
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I. -isystem (Join-Path $native 'include') `
        tests\h264_packetizer_recovery_test.cpp ("-L"+(Join-Path $native 'lib')) `
        -ldatachannel -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m `
        -lssp -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -o $integration
    if ($LASTEXITCODE -ne 0) { throw "Pinned packetizer test build failed ($LASTEXITCODE)." }
    & $integration
    if ($LASTEXITCODE -ne 0) { throw "Pinned packetizer recovery regression failed ($LASTEXITCODE)." }
} finally { Pop-Location }
