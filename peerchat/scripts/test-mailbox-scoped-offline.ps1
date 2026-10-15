param([switch]$PortableCrypto,[string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('build/tests/mailbox-scoped-offline-'+[Guid]::NewGuid().ToString('N'))}
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
& (Join-Path $PSScriptRoot 'build-go-hpke.ps1') -OutputDirectory $out
Push-Location $root
try {
    $crypto=@()
    if($PortableCrypto){
        $native='C:/Peerchatdep/native/mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'
        $crypto=@('-DPEERCHAT_PORTABLE_CRYPTO',"-I$native/include","-L$native/lib",'-lmbedcrypto','-leverest','-lp256m')
    }
    $exe=Join-Path $out 'mailbox-scoped-offline-test.exe'
    & 'C:/TDM-GCC-64/bin/g++.exe' -I. -std=c++17 -D_WIN32_WINNT=0x0601 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/mailbox_scoped_offline_test.cpp platform/go_hpke_provider.cpp group.cpp @crypto -o $exe -lncrypt -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Scoped offline mailbox build failed'}
    & $exe (Join-Path $out 'profiles')
    if($LASTEXITCODE -ne 0){throw 'Scoped offline mailbox test failed'}
}finally{Pop-Location}
