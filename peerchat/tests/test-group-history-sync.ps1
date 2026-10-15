param([switch]$PortableCrypto)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/group-history-sync-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
 $crypto=@()
 if($PortableCrypto){$native='C:/Peerchatdep/native/mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08';$crypto=@('-DPEERCHAT_PORTABLE_CRYPTO',"-I$native/include","-L$native/lib",'-lmbedcrypto','-leverest','-lp256m')}
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/group_history_sync_test.cpp group.cpp group_message_envelope.cpp storage.cpp tls.cpp @crypto -o "$out/group-history.exe" -lbcrypt -lcrypt32 -lncrypt -lsecur32 -lws2_32
 if($LASTEXITCODE){throw 'Group history fixture compilation failed'}
 & "$out/group-history.exe"
 if($LASTEXITCODE){throw 'Group history fixture failed'}
 Write-Output "Evidence: $out"
}finally{Pop-Location}
