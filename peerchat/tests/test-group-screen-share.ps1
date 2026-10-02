param([string]$DevelopmentDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\group-screen-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests\group_screen_share_session_test.cpp -o "$out\group-screen-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Group screen session compilation failed'}
    & "$out\group-screen-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Group screen session regression failed'}
    $session=Get-Content -Raw -LiteralPath group_screen_share_session.hpp
    if($session -match 'RelayMesh|rtc[A-Z]|PeerConnection'){throw 'Concrete transport leaked into group session'}
    $service=Get-Content -Raw -LiteralPath desktop\GroupScreenShareService.cs
    if($service -cmatch 'RelayMeshScreenShareTopology|rtc[A-Z]\w*\(|PeerConnection|new Timer|TcpClient'){throw 'Concrete transport/new scheduler leaked into application service'}
    if($service -match 'public (GroupScreenRoomStatus Snapshot|string Diagnostics)[^\r\n]*lock\(gate\)'){throw 'WPF snapshot getter blocks on native worker gate'}
    if($service -notmatch 'Session\[\] current=await Task.Run'){throw 'Application pump takes its initial native gate on the UI thread'}
    Write-Host 'PASS: replaceable topology boundary; no RTC/topology implementation or scheduler in application-facing owner'
    $native=if($env:PEERCHAT_NATIVE_DEPENDENCY_ROOT){$env:PEERCHAT_NATIVE_DEPENDENCY_ROOT}else{'C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'}
    & g++ -I. ("-I"+(Join-Path $native 'include')) -std=c++17 -O2 -Wall -Wextra -Werror tests\screen_topology_test.cpp screen_media_topology.cpp ("-L"+(Join-Path $native 'lib')) -ldatachannel -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m -lssp -static-libgcc -static-libstdc++ -o "$out\topology-test.exe" -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -luser32
    if($LASTEXITCODE -ne 0){throw 'Production screen topology regression compilation failed'}
    & "$out\topology-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Production screen topology regression failed'}
    if($DevelopmentDirectory){
        Copy-Item -LiteralPath (Join-Path $DevelopmentDirectory 'ScreenMedia\PeerChat.ScreenMedia.dll') -Destination $out
        & g++ -DPEERCHAT_TEST_MEDIA_DIRECT -I. ("-I"+(Join-Path $native 'include')) -std=c++17 -O2 -Wall -Wextra -Werror tests\screen_shared_fanout_test.cpp ("-L"+(Join-Path $native 'lib')) -ldatachannel -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m -lssp -static-libgcc -static-libstdc++ -o "$out\fanout-test.exe" -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -luser32
        if($LASTEXITCODE -ne 0){throw 'Real shared screen fanout compilation failed'}
        & "$out\fanout-test.exe"
        if($LASTEXITCODE -ne 0){throw 'Real shared screen fanout regression failed'}
    }
} finally {Pop-Location}
