param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\remembered-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$cert=$null
Push-Location $root
try {
 $cert=New-SelfSignedCertificate -DnsName localhost -CertStoreLocation 'Cert:\CurrentUser\My' -NotAfter (Get-Date).AddDays(1)
 $password=[Guid]::NewGuid().ToString('N');$pfx=Join-Path $out 'test.pfx'
 Export-PfxCertificate -Cert $cert -FilePath $pfx -Password (ConvertTo-SecureString $password -AsPlainText -Force)|Out-Null
 & "$fx\csc.exe" /nologo /target:exe /platform:x64 /main:RememberedLoginTest "/out:$out\test.exe" "/reference:$fx\System.Web.Extensions.dll" "/reference:$fx\System.Security.dll" .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\ServerPresence.cs .\server\ServerRpc.cs .\server\PeerChat.Server.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\tests\remembered_login_test.cs
 if($LASTEXITCODE){throw 'Remembered login compile failed'}
 & "$out\test.exe" "$out\state" $pfx $password
 if($LASTEXITCODE){throw 'Remembered login regression failed'}
} finally {
 Pop-Location
 if($cert){Remove-Item -LiteralPath ('Cert:\CurrentUser\My\'+$cert.Thumbprint)}
 if(Test-Path -LiteralPath (Join-Path $out 'test.pfx')){Remove-Item -LiteralPath (Join-Path $out 'test.pfx')}
}
