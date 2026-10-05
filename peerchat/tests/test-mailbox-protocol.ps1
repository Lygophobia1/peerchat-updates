param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$evaluation=Join-Path $root 'build\deps\wolfcrypt-evaluation'
$source=Join-Path $evaluation 'wolfssl-3c5eead44904df64e6a5a1f4ebdce377d35a849a';$native=Join-Path $evaluation 'mingw-x64'
$out=Join-Path $root ('build\tests\mailbox-protocol-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -D_WIN32_WINNT=0x0601 -DWOLFCRYPT_ONLY -DNO_CERTS -DPEERCHAT_HPKE_EVALUATION -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I $native -I $source tests/mailbox_protocol_test.cpp platform/wolfcrypt_evaluation_hpke.cpp group.cpp storage.cpp (Join-Path $native 'libwolfssl.a') -o "$out\mailbox-protocol-test.exe" -lncrypt -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw "Mailbox protocol build failed: $LASTEXITCODE"}
    & "$out\mailbox-protocol-test.exe" "$out\profiles"
    if($LASTEXITCODE -ne 0){throw "Mailbox protocol tests failed: $LASTEXITCODE"}
    $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 /optimize+ /main:EncryptedMailboxServerTest "/out:$out\mailbox-server-test.exe" "/reference:$fx\System.Web.Extensions.dll" .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\PeerChat.Server.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\desktop\DiscoveryClient.cs .\desktop\ProfileAccounts.cs .\tests\encrypted_mailbox_server_test.cs
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
