param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\history-first-ui-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item "$ClientDirectory\*" $out
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$refs=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','System.Net.Http.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
$names=@('CoreAdapter','ArchiveEnrollmentExecution','MailboxExecution','ChatViewModel','DiscoveryClient','GroupVoiceService','GroupScreenShareService','RelayTransport','FriendConversations','ProfileAccounts','VoicePreferences','FirewallRules','LocalProfiles','Diagnostics','UpdateClient','App','ChatAttachments','HistorySettings')
$sources=@($names|ForEach-Object{Join-Path $root "desktop\$_.cs"})
$compiler=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe'
Push-Location $root
try {
 & $compiler /nologo /target:exe /main:HistoryFirstEnrollmentUiTest /platform:x64 /optimize+ "/out:$out\first-ui.exe" /resource:desktop\Main.xaml,Main.xaml "/reference:$out\PeerChat.Updater.Core.dll" $refs .\shared\VersionInfo.cs $sources "$PSScriptRoot\history_first_enrollment_ui_test.cs"
 if($LASTEXITCODE){throw 'First enrollment UI compilation failed'}
 $p=Start-Process -WindowStyle Hidden -PassThru "$out\first-ui.exe" -ArgumentList ('"'+$out+'\profiles"') -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
 $null=$p.Handle
 if(!$p.WaitForExit(60000)){Stop-Process -Id $p.Id;throw 'First enrollment UI timeout'}
 Get-Content "$out\stdout.log","$out\stderr.log"
 if($p.ExitCode){throw 'First enrollment UI integration failed'}
} finally {Pop-Location}
