param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\mailbox-protocol-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $out | Out-Null
& (Join-Path $root 'scripts\build-go-hpke.ps1') -OutputDirectory $out
Push-Location $root
try {
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -D_WIN32_WINNT=0x0601 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/mailbox_protocol_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp -o "$out\mailbox-protocol-test.exe" -lncrypt -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw "Mailbox protocol build failed: $LASTEXITCODE"}
    & "$out\mailbox-protocol-test.exe" "$out\profiles"
    if($LASTEXITCODE -ne 0){throw "Mailbox protocol tests failed: $LASTEXITCODE"}
    $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 /optimize+ /main:EncryptedMailboxServerTest "/out:$out\mailbox-server-test.exe" "/reference:$fx\System.Web.Extensions.dll" .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\ArchiveAuthorization.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\ServerPresence.cs .\server\ServerRpc.cs .\server\PeerChat.Server.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\desktop\ArchiveEnrollmentExecution.cs .\desktop\DiscoveryClient.cs .\desktop\ProfileAccounts.cs .\tests\encrypted_mailbox_server_test.cs
    if($LASTEXITCODE -ne 0){throw 'Mailbox server test compilation failed'}
    $certificate=New-SelfSignedCertificate -DnsName 'localhost' -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
    $password=[Guid]::NewGuid().ToString('N');$pfx=Join-Path $out 'test-server.pfx'
    try {
        Export-PfxCertificate -Cert $certificate -FilePath $pfx -Password (ConvertTo-SecureString $password -AsPlainText -Force)|Out-Null
        & "$out\mailbox-server-test.exe" "$out\profiles" $pfx $password
        if($LASTEXITCODE -ne 0){throw 'Mailbox server regressions failed'}
    } finally {
        Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$certificate.Thumbprint)
        if(Test-Path -LiteralPath $pfx){Remove-Item -LiteralPath $pfx}
    }
} finally {Pop-Location}
