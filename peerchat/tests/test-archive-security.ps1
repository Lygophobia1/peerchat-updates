param([switch]$Provenance,[switch]$ServerMetadata)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\archive-security-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$compiler=if(Get-Command g++ -ErrorAction SilentlyContinue){(Get-Command g++).Source}else{'C:\TDM-GCC-64\bin\g++.exe'}
Push-Location $root
try {
    $test=if($Provenance){'tests/archive_provenance_test.cpp'}else{'tests/archive_authorization_test.cpp'}
    & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. $test group.cpp -o "$out\archive-security-test.exe" -lbcrypt -lcrypt32 -lncrypt
    if($LASTEXITCODE -ne 0){throw 'Archive security test compilation failed'}
    $fixtures=Join-Path $out 'fixtures'
    New-Item -ItemType Directory -Path $fixtures | Out-Null
    if($ServerMetadata){& "$out\archive-security-test.exe" $fixtures}else{& "$out\archive-security-test.exe"}
    if($LASTEXITCODE -ne 0){throw 'Archive security regression failed'}
    if($ServerMetadata){
        if($Provenance){throw 'Server metadata fixtures require authorization test'}
        $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
        $sources=@('shared/VersionInfo.cs','server/ServerConfiguration.cs','server/GroupRosterProof.cs','server/ArchiveAuthorization.cs','server/GroupCiphertextFanout.cs','server/GroupMediaRooms.cs','server/ServerPresence.cs','server/ServerRpc.cs','server/PeerChat.Server.cs','server/MailboxProtocol.cs','server/EncryptedMailbox.cs','server/EncryptedMailboxChannel.cs','tests/archive_authorization_server_test.cs')
        $sources=@($sources | ForEach-Object {Join-Path $root $_})
        & "$fx\csc.exe" /nologo /target:exe /platform:x64 /optimize+ /main:ArchiveAuthorizationServerTest "/out:$out\archive-server-test.exe" "/reference:$fx\System.Web.Extensions.dll" $sources
        if($LASTEXITCODE -ne 0){throw 'Archive server metadata compilation failed'}
        & "$out\archive-server-test.exe" $fixtures
        if($LASTEXITCODE -ne 0){throw 'Archive server metadata regression failed'}
    }
} finally {Pop-Location}
