param([switch]$BaselineOnly,[switch]$IncludeReconnect,[switch]$ReconnectOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$cache=if($env:PEERCHAT_NATIVE_DEPENDENCY_ROOT){$env:PEERCHAT_NATIVE_DEPENDENCY_ROOT}else{'C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'}
if(-not(Test-Path -LiteralPath (Join-Path $cache 'lib\libdatachannel.a'))){throw 'Verified shared dependency cache required; this test never rebuilds dependencies'}
$out=Join-Path $root ('build\tests\node-characterization-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
& (Join-Path $root 'scripts\build-go-hpke.ps1') -OutputDirectory $out
Push-Location $root
try {
    # Capture committed behavior without resetting or editing the current tree.
    & git -C (Split-Path $root -Parent) archive HEAD peerchat/node.cpp -o (Join-Path $out 'baseline.tar')
    if($LASTEXITCODE -ne 0){throw 'Committed Node source capture failed'}
    & tar -xf (Join-Path $out 'baseline.tar') -C $out
    if($LASTEXITCODE -ne 0){throw 'Committed Node source extraction failed'}
    $sources=@('platform/go_hpke_provider.cpp','tests/node_test.cpp','tls.cpp','storage.cpp','group.cpp','group_message_envelope.cpp','share_id.cpp',
        'voice_media.cpp','voice_receive_engine.cpp','group_voice_session.cpp','voice_media_topology.cpp','group_voice_bridge.cpp',
        'screen_media_topology.cpp','group_screen_share_bridge.cpp','audio.cpp')
    $link=@('-ldatachannel','-lopus','-lsrtp2','-ljuice','-lusrsctp','-lmbedtls','-lmbedx509','-lmbedcrypto','-leverest','-lp256m',
        '-lssp','-lwinmm','-lws2_32','-lsecur32','-lcrypt32','-lbcrypt','-lncrypt','-liphlpapi')
    $variants=if($ReconnectOnly){@()}elseif($BaselineOnly){@('baseline')}else{@('baseline','current')}
    foreach($variant in $variants){
        $source=if($variant -eq 'baseline'){Join-Path $out 'peerchat\node.cpp'}else{'node.cpp'}
        $exe=Join-Path $out ($variant+'-node.exe')
        & 'C:\TDM-GCC-64\bin\g++.exe' -DPEERCHAT_TEST_MEDIA_DIRECT -std=c++17 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. ("-I"+(Join-Path $cache 'include')) $sources $source ("-L"+(Join-Path $cache 'lib')) $link -o $exe
        if($LASTEXITCODE -ne 0){throw "$variant Node characterization build failed"}
        & $exe
        if($LASTEXITCODE -ne 0){throw "$variant Node characterization failed"}
        Write-Host "PASS: $variant actual Node/Schannel lifecycle, initial/stale/account/fingerprint trust, text/ACK/history/reconnect characterization"
    }
    if($IncludeReconnect -or $ReconnectOnly){
        $reconnectSources=@($sources|Where-Object{$_ -ne 'tests/node_test.cpp'})
        $exe=Join-Path $out 'reconnect.exe'
        & 'C:\TDM-GCC-64\bin\g++.exe' -DPEERCHAT_TEST_MEDIA_DIRECT -DPEERCHAT_TEST_SHORT_RECONNECT_BACKOFF -std=c++17 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. ("-I"+(Join-Path $cache 'include')) tests/reconnect_test.cpp $reconnectSources node.cpp ("-L"+(Join-Path $cache 'lib')) $link -o $exe
        if($LASTEXITCODE -ne 0){throw 'Reconnect characterization build failed'}
        & $exe
        if($LASTEXITCODE -ne 0){throw 'Reconnect characterization failed'}
    }
}finally{Pop-Location}
