param([switch]$PortableCrypto,[switch]$SchedulerOnly)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/group-relay-admission-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out | Out-Null
Push-Location $root
try {
    $crypto=@()
    if($PortableCrypto){$native='C:/Peerchatdep/native/mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08';$crypto=@('-DPEERCHAT_PORTABLE_CRYPTO',"-I$native/include","-L$native/lib",'-lmbedcrypto','-leverest','-lp256m')}
    & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/group_relay_admission_test.cpp group.cpp storage.cpp tls.cpp -o "$out/group-relay-admission.exe" @crypto -lbcrypt -lcrypt32 -lws2_32 -lncrypt -lsecur32
    if($LASTEXITCODE){throw 'Group relay admission compilation failed'}
    $mode=@()
    if($SchedulerOnly){$mode=@('scheduler-only')}
    & "$out/group-relay-admission.exe" "$out/profiles" @mode
    if($LASTEXITCODE){throw 'Group relay admission regression failed'}
    Write-Host "Evidence: $out"
}finally{Pop-Location}
