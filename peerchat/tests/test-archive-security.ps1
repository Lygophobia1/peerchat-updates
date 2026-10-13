param([switch]$Provenance,[switch]$ServerMetadata,[switch]$SessionScope,[switch]$PeerTls,[switch]$HistoryStorage,[switch]$Enrollment)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\archive-security-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$compiler=if(Get-Command g++ -ErrorAction SilentlyContinue){(Get-Command g++).Source}else{'C:\TDM-GCC-64\bin\g++.exe'}
Push-Location $root
try {
    if($Enrollment){
        & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/archive_enrollment_test.cpp group.cpp -o "$out\archive-enrollment-test.exe" -lbcrypt -lcrypt32 -lncrypt
        if($LASTEXITCODE -ne 0){throw 'Archive enrollment compilation failed'}
        & "$out\archive-enrollment-test.exe"
        if($LASTEXITCODE -ne 0){throw 'Archive enrollment regression failed'}
    }
    if($HistoryStorage){
        & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/archive_history_storage_test.cpp group.cpp -o "$out\archive-history-storage-test.exe" -lbcrypt -lcrypt32 -lncrypt
        if($LASTEXITCODE -ne 0){throw 'Archive history storage compilation failed'}
        & "$out\archive-history-storage-test.exe"
        if($LASTEXITCODE -ne 0){throw 'Archive history storage regression failed'}
    }
    if($PeerTls){
        & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/archive_reliable_endpoint_test.cpp tls.cpp storage.cpp group.cpp -o "$out\archive-peer-tls-test.exe" -lbcrypt -lcrypt32 -lncrypt -lsecur32
        if($LASTEXITCODE -ne 0){throw 'Archive peer TLS compilation failed'}
        & "$out\archive-peer-tls-test.exe"
        if($LASTEXITCODE -ne 0){throw 'Archive peer TLS regression failed'}
    }
    if($SessionScope){
        & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/archive_session_scope_test.cpp -o "$out\archive-session-scope-test.exe"
        if($LASTEXITCODE -ne 0){throw 'Archive session scope compilation failed'}
        & "$out\archive-session-scope-test.exe"
        if($LASTEXITCODE -ne 0){throw 'Archive session scope regression failed'}
        & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/group_control_scope_test.cpp -o "$out\group-session-scope-test.exe"
        if($LASTEXITCODE -ne 0){throw 'Existing group scope compilation failed'}
        & "$out\group-session-scope-test.exe"
        if($LASTEXITCODE -ne 0){throw 'Existing group scope regression failed'}
    }
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
        $certificate=$null
        $pfx=Join-Path $out 'isolated-relay.pfx'
        try {
            $certificate=New-SelfSignedCertificate -DnsName 'localhost' -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
            $password=[Guid]::NewGuid().ToString('N')
            Export-PfxCertificate -Cert $certificate -FilePath $pfx -Password (ConvertTo-SecureString $password -AsPlainText -Force)|Out-Null
            & "$fx\csc.exe" /nologo /target:exe /platform:x64 /optimize+ /main:ArchiveRelayLifecycleTest "/out:$out\archive-relay-test.exe" "/reference:$fx\System.Web.Extensions.dll" $sources (Join-Path $root 'tests/archive_relay_lifecycle_test.cs')
            if($LASTEXITCODE -ne 0){throw 'Archive relay integration compilation failed'}
            & "$out\archive-relay-test.exe" $fixtures $pfx $password
            if($LASTEXITCODE -ne 0){throw 'Archive relay integration regression failed'}
        } finally {
            if($certificate){Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$certificate.Thumbprint)}
            if(Test-Path -LiteralPath $pfx){Remove-Item -LiteralPath $pfx}
        }
    }
} finally {Pop-Location}
