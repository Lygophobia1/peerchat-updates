param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $env:TEMP ('audit-server-receipt-pagination-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
Push-Location $root
try {
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 /optimize+ /main:MessagingReceiptPaginationAuditTest "/out:$out\messaging-receipt-pagination-audit.exe" "/reference:$fx\System.Web.Extensions.dll" .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\ArchiveAuthorization.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\ServerPresence.cs .\server\ServerRpc.cs .\server\PeerChat.Server.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\desktop\ArchiveEnrollmentExecution.cs .\desktop\DeviceNameMetadata.cs .\desktop\DiscoveryClient.cs .\desktop\ProfileAccounts.cs .\tests\messaging_receipt_pagination_audit_test.cs
    if($LASTEXITCODE -ne 0){throw 'Receipt pagination audit compilation failed.'}
    & "$out\messaging-receipt-pagination-audit.exe" $out
    if($LASTEXITCODE -ne 0){throw 'Receipt pagination audit observation failed.'}
} finally {Pop-Location}
