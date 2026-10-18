param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\group-rooms-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$certificate=$null
Push-Location $root
try {
    $certificate=New-SelfSignedCertificate -DnsName 'localhost' -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
    $password=[Guid]::NewGuid().ToString('N')
    $pfx=Join-Path $out 'test-server.pfx'
    Export-PfxCertificate -Cert $certificate -FilePath $pfx -Password (ConvertTo-SecureString $password -AsPlainText -Force)|Out-Null
    & (Join-Path $fx 'csc.exe') /nologo /target:exe /platform:x64 /main:GroupMediaRoomsTest ("/out:"+(Join-Path $out 'group-rooms-test.exe')) ("/reference:"+(Join-Path $fx 'System.Web.Extensions.dll')) ("/reference:"+(Join-Path $fx 'System.Security.dll')) .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\ArchiveAuthorization.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\ServerPresence.cs .\server\ServerRpc.cs .\server\PeerChat.Server.cs .\server\DeviceIdentityEndorsement.cs .\server\DeviceIdentityEnrollment.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\desktop\ProfileAccounts.cs .\desktop\ArchiveEnrollmentExecution.cs .\desktop\DeviceNameMetadata.cs .\desktop\DiscoveryClient.cs .\tests\group_roster_proof_test.cs .\tests\group_media_rooms_test.cs
    if($LASTEXITCODE -ne 0){throw 'Group media room regression compilation failed.'}
    & (Join-Path $out 'group-rooms-test.exe') (Join-Path $out 'state') $pfx $password
    if($LASTEXITCODE -ne 0){throw 'Group media room regression failed.'}
} finally {
    Pop-Location
    if($certificate){Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$certificate.Thumbprint)}
    if(Test-Path -LiteralPath (Join-Path $out 'test-server.pfx')){Remove-Item -LiteralPath (Join-Path $out 'test-server.pfx')}
}
