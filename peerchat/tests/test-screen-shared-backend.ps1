param([Parameter(Mandatory=$true)][string]$DevelopmentDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$development=(Resolve-Path -LiteralPath $DevelopmentDirectory).Path
$native=if($env:PEERCHAT_NATIVE_DEPENDENCY_ROOT){$env:PEERCHAT_NATIVE_DEPENDENCY_ROOT}else{'C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'}
$output=Join-Path $root ('build\tests\screen-shared-backend-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $output|Out-Null
Push-Location $root
try {
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I. tests\screen_shared_backend_test.cpp -o "$output\backend-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Shared screen backend test compilation failed'}
    & "$output\backend-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Shared screen backend regression failed'}
    Copy-Item -LiteralPath "$development\ScreenMedia\PeerChat.ScreenMedia.dll" -Destination $output -Force
    & g++ -DPEERCHAT_TEST_MEDIA_DIRECT -I. ("-I"+(Join-Path $native 'include')) -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\screen_media_transport_test.cpp ("-L"+(Join-Path $native 'lib')) -ldatachannel -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m -lssp -o "$output\transport-test.exe" -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -luser32
    if($LASTEXITCODE -ne 0){throw 'Accepted screen transport test compilation failed'}
    foreach($mode in @('native','--compat','--mixed')) {
        if($mode -eq 'native'){& "$output\transport-test.exe"}else{& "$output\transport-test.exe" $mode}
        if($LASTEXITCODE -ne 0){throw "Accepted screen transport regression failed: $mode ($LASTEXITCODE)"}
    }
} finally {Pop-Location}
