param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/mailbox-reenrollment-server-'+[Guid]::NewGuid().ToString('N'))
& (Join-Path $PSScriptRoot 'test-mailbox-reenrollment.ps1') -OutputDirectory $out
Push-Location $root
try {
    $fx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
    & "$fx/csc.exe" /nologo /target:exe /platform:x64 /optimize+ /main:MailboxReenrollmentServerTest "/out:$out/mailbox-reenrollment-server-test.exe" "/reference:$fx/System.Web.Extensions.dll" .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\ArchiveAuthorization.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\ServerPresence.cs .\server\ServerRpc.cs .\server\PeerChat.Server.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\tests\mailbox_reenrollment_server_test.cs
    if($LASTEXITCODE -ne 0){throw 'Reenrollment server fixture compilation failed'}
    & "$out/mailbox-reenrollment-server-test.exe" "$out/fixtures"
    if($LASTEXITCODE -ne 0){throw 'Reenrollment server fixture failed'}
    Write-Output ('Evidence: '+$out)
}finally{Pop-Location}
