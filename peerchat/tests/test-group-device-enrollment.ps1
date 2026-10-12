param([switch]$PortableCrypto)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\group-device-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$savedPath=$env:PATH
Push-Location $root
try{
    $crypto=@()
    if($PortableCrypto){$native='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08';$crypto=@('-DPEERCHAT_PORTABLE_CRYPTO',('-I'+(Join-Path $native 'include')),('-L'+(Join-Path $native 'lib')),'-lmbedcrypto','-leverest','-lp256m')}
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/group_device_binding_fixture.cpp group.cpp storage.cpp -o "$out\fixture.exe" $crypto -lbcrypt -lcrypt32 -lws2_32
    if($LASTEXITCODE){throw 'Native binding fixture compilation failed'}
    $env:PATH="$env:WINDIR\System32;$env:WINDIR"
    & "$out\fixture.exe" "$out\state"
    if($LASTEXITCODE){throw 'Native binding fixture failed'}
    $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 /main:GroupDeviceServerTest "/out:$out\server-test.exe" /reference:System.Web.Extensions.dll shared\VersionInfo.cs server\ServerConfiguration.cs server\GroupRosterProof.cs server\ArchiveAuthorization.cs server\GroupCiphertextFanout.cs server\GroupMediaRooms.cs server\ServerPresence.cs server\ServerRpc.cs server\PeerChat.Server.cs server\MailboxProtocol.cs server\EncryptedMailbox.cs server\EncryptedMailboxChannel.cs tests\group_device_server_test.cs
    if($LASTEXITCODE){throw 'Server device proof compilation failed'}
    & "$out\server-test.exe" "$out\state"
    if($LASTEXITCODE){throw 'Server device proof regression failed'}
}finally{$env:PATH=$savedPath;Pop-Location}
