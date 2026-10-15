param([switch]$PortableCrypto)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/group-attachment-body-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try{
 $crypto=@()
 if($PortableCrypto){$native=if($env:PEERCHAT_NATIVE_DEPENDENCY_ROOT){$env:PEERCHAT_NATIVE_DEPENDENCY_ROOT}else{'C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'};$crypto=@('-DPEERCHAT_PORTABLE_CRYPTO',('-I'+(Join-Path $native 'include')),('-L'+(Join-Path $native 'lib')),'-lmbedcrypto','-leverest','-lp256m')}
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/group_attachment_body_test.cpp group_message_envelope.cpp group.cpp storage.cpp -o "$out/body.exe" @crypto -lbcrypt -lcrypt32 -lws2_32
 if($LASTEXITCODE){throw 'Attachment body compile failed'}
 & "$out/body.exe" "$out/profiles"
 if($LASTEXITCODE){throw 'Attachment body test failed'}
 Write-Output "Evidence: $out. Existing group envelope/storage test, not enabled cache fanout."
}finally{Pop-Location}
