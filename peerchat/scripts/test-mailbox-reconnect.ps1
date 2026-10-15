param([switch]$PortableCrypto)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/mailbox-reconnect-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
& (Join-Path $PSScriptRoot 'build-go-hpke.ps1') -OutputDirectory $out
Push-Location $root
try {
    $crypto=@()
    if($PortableCrypto){
        $native='C:/Peerchatdep/native/mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'
        $crypto=@('-DPEERCHAT_PORTABLE_CRYPTO',"-I$native/include","-L$native/lib",'-lmbedcrypto','-leverest','-lp256m')
    }
    $exe=Join-Path $out 'mailbox-reconnect-test.exe'
    & 'C:/TDM-GCC-64/bin/g++.exe' -I. -std=c++17 -D_WIN32_WINNT=0x0601 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/mailbox_reconnect_test.cpp platform/go_hpke_provider.cpp group.cpp @crypto -o $exe -lncrypt -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Mailbox reconnect build failed'}
    & $exe (Join-Path $out 'profiles')
    if($LASTEXITCODE -ne 0){throw 'Mailbox reconnect test failed'}
}finally{Pop-Location}
