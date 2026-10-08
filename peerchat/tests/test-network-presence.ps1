param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\network-presence-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
Push-Location $root
try {
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 /main:ServerPresenceTest "/out:$out\presence.exe" "/reference:$fx\System.Web.Extensions.dll" .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\GroupRosterProof.cs .\server\ArchiveAuthorization.cs .\server\GroupCiphertextFanout.cs .\server\GroupMediaRooms.cs .\server\ServerPresence.cs .\server\ServerRpc.cs .\server\PeerChat.Server.cs .\server\MailboxProtocol.cs .\server\EncryptedMailbox.cs .\server\EncryptedMailboxChannel.cs .\desktop\ArchiveEnrollmentExecution.cs .\desktop\DiscoveryClient.cs .\desktop\ProfileAccounts.cs .\tests\server_presence_test.cs
    if($LASTEXITCODE -ne 0){throw 'Presence production test compile failed'}
    & "$out\presence.exe" (Join-Path $out 'state')
    if($LASTEXITCODE -ne 0){throw 'Presence production regression failed'}
    $references=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 "/out:$out\AdapterTest.exe" $references .\shared\VersionInfo.cs .\desktop\CoreAdapter.cs .\desktop\ChatViewModel.cs .\desktop\ArchiveEnrollmentExecution.cs .\desktop\DiscoveryClient.cs .\desktop\FriendConversations.cs .\desktop\ProfileAccounts.cs .\desktop\VoicePreferences.cs .\desktop\LocalProfiles.cs .\desktop\Diagnostics.cs .\desktop\AdapterTest.cs
    if($LASTEXITCODE -ne 0){throw 'Presence/composer model compile failed'}
    & "$out\AdapterTest.exe" --offline-composer-only
    if($LASTEXITCODE -ne 0){throw 'Presence/composer model regression failed'}
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 /main:MailboxLifecycleTest "/out:$out\MailboxLifecycleTest.exe" $references .\shared\VersionInfo.cs .\desktop\CoreAdapter.cs .\desktop\MailboxExecution.cs .\desktop\ChatViewModel.cs .\desktop\ArchiveEnrollmentExecution.cs .\desktop\DiscoveryClient.cs .\desktop\FriendConversations.cs .\desktop\ProfileAccounts.cs .\desktop\VoicePreferences.cs .\desktop\LocalProfiles.cs .\desktop\Diagnostics.cs .\tests\mailbox_lifecycle_test.cs
    if($LASTEXITCODE -ne 0){throw 'Mailbox lifecycle compile failed'}
    & "$out\MailboxLifecycleTest.exe"
    if($LASTEXITCODE -ne 0){throw 'Mailbox lifecycle regression failed'}
} finally {Pop-Location}
