param([switch]$PortableCrypto)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/android-windows-group-'+[Guid]::NewGuid().ToString('N'))
$native='C:/Peerchatdep/native/mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $root
try {
    & "$root/scripts/build-go-hpke.ps1" -OutputDirectory $out
    $crypto=@();if($PortableCrypto){$crypto=@('-DPEERCHAT_PORTABLE_CRYPTO')}
    & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. "-I$native/include" tests/android_group_windows_interop_test.cpp node.cpp tls.cpp storage.cpp group.cpp group_message_envelope.cpp voice_media.cpp voice_receive_engine.cpp group_voice_session.cpp voice_media_topology.cpp group_voice_bridge.cpp screen_media_topology.cpp group_screen_share_bridge.cpp audio.cpp platform/go_hpke_provider.cpp @crypto -Wno-cast-function-type "-L$native/lib" -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o "$out/interop.exe" -lwinmm -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -luser32
    if($LASTEXITCODE){throw 'Windows/Android native interoperability compilation failed'}
    & "$out/interop.exe" "$out/profiles"
    if($LASTEXITCODE){throw 'Windows/Android native interoperability failed'}
    Write-Host "Evidence: $out"
}finally{Pop-Location}
