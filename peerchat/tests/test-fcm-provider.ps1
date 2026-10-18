$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\fcm-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
Push-Location $root
try {
 & "$fx\csc.exe" /nologo /target:exe /platform:x64 /main:FcmProviderTest "/out:$out\test.exe" "/reference:$fx\System.Web.Extensions.dll" "/reference:$fx\System.Security.dll" .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\ArchiveAuthorization.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\ServerPresence.cs .\server\ServerRpc.cs .\server\PeerChat.Server.cs .\server\DeviceIdentityEndorsement.cs .\server\DeviceIdentityEnrollment.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\tests\fcm_provider_test.cs
 if($LASTEXITCODE){throw 'FCM test compile failed'}
 & "$out\test.exe"
 if($LASTEXITCODE){throw 'FCM provider regression failed'}
} finally {Pop-Location}
