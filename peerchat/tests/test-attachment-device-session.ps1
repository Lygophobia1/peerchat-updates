param([Parameter(Mandatory=$true)][string]$ApplicationDirectory,[string]$CacheBaseDirectory='F:\PeerChat-isolated-cache-tests')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$run=Join-Path $env:TEMP ('PeerChat-attachment-device-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
$cacheDirectory=if($CacheBaseDirectory){
    $base=(Resolve-Path -LiteralPath $CacheBaseDirectory).Path
    Join-Path $base ('PeerChat-attachment-cache-test-'+[Guid]::NewGuid().ToString('N'))
}else{Join-Path $run 'cache'}
$certificate=$null
Push-Location $root
try {
    Copy-Item -Path (Join-Path (Resolve-Path -LiteralPath $ApplicationDirectory).Path '*') -Destination $run
    $certificate=New-SelfSignedCertificate -DnsName localhost -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
    $password=[Guid]::NewGuid().ToString('N');$pfx=Join-Path $run 'server.pfx'
    Export-PfxCertificate -Cert $certificate -FilePath $pfx -Password (ConvertTo-SecureString $password -AsPlainText -Force) | Out-Null
    $fx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
    $refs=@('System.dll','System.Core.dll','System.Web.Extensions.dll','System.Security.dll') | ForEach-Object {'/reference:'+(Join-Path $fx $_)}
    $attachmentSources=Get-ChildItem .\server\AttachmentCache*.cs | ForEach-Object FullName
    & "$fx/csc.exe" /nologo /define:PEERCHAT_CACHE_CANDIDATE /target:exe /platform:x64 /main:AttachmentDeviceSessionTest "/out:$run/AttachmentDeviceSessionTest.exe" "/reference:$run/PeerChat.Desktop.exe" $refs @attachmentSources .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\ArchiveAuthorization.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\ServerPresence.cs .\server\ServerRpc.cs .\server\PeerChat.Server.cs .\server\DeviceIdentityEndorsement.cs .\server\DeviceIdentityEnrollment.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\tests\attachment_device_session_test.cs
    if($LASTEXITCODE){throw 'Attachment device session test compile failed'}
    & "$run/AttachmentDeviceSessionTest.exe" "$run/state" $pfx $password $cacheDirectory
    if($LASTEXITCODE){throw 'Attachment device session regression failed'}
    Write-Host "Evidence: $run"
    Write-Host "Cache evidence: $cacheDirectory"
}finally{
    Pop-Location
    if($certificate){Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$certificate.Thumbprint)}
    if(Test-Path -LiteralPath (Join-Path $run 'server.pfx')){Remove-Item -LiteralPath (Join-Path $run 'server.pfx')}
}
