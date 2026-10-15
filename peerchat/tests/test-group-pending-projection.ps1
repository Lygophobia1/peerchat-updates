param([switch]$PortableCrypto)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/group-pending-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $root
try {
    $crypto=@()
    if($PortableCrypto){$native='C:/Peerchatdep/native/mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08';$crypto=@('-DPEERCHAT_PORTABLE_CRYPTO',"-I$native/include","-L$native/lib",'-lmbedcrypto','-leverest','-lp256m')}
    & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/group_pending_projection_test.cpp group_message_envelope.cpp group.cpp storage.cpp -o "$out/pending.exe" @crypto -lbcrypt -lcrypt32 -lws2_32 -lncrypt
    if($LASTEXITCODE){throw 'Group pending projection compilation failed'}
    & "$out/pending.exe" "$out/profiles"
    if($LASTEXITCODE){throw 'Group pending projection regression failed'}
    Write-Host "Evidence: $out"
}finally{Pop-Location}
