param([Parameter(Mandatory=$true)][string]$ClientDirectory,[Parameter(Mandatory=$true)][string]$EvidenceDirectory,[switch]$SourcePreview)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\history-visual-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item "$ClientDirectory\*" $out
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$refs=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','System.Net.Http.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
if($SourcePreview){
 $names=@('CoreAdapter','ArchiveEnrollmentExecution','MailboxExecution','ChatViewModel','DiscoveryClient','GroupVoiceService','GroupScreenShareService','RelayTransport','FriendConversations','ProfileAccounts','VoicePreferences','FirewallRules','LocalProfiles','Diagnostics','UpdateClient','App','ChatAttachments','HistorySettings')
 $sources=@($names|ForEach-Object{Join-Path $root "desktop\$_.cs"})
 $compiler=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe'
 & $compiler /nologo /target:winexe /platform:x64 /optimize+ "/out:$out\PeerChat.Desktop.exe" /resource:desktop\Main.xaml,Main.xaml "/reference:$out\PeerChat.Updater.Core.dll" $refs .\shared\VersionInfo.cs $sources
 if($LASTEXITCODE){throw 'Source preview compilation failed'}
}
& "$fx\csc.exe" /nologo /target:exe /platform:x64 "/out:$out\visual.exe" "/reference:$out\PeerChat.Desktop.exe" $refs "$PSScriptRoot\history_visual_test.cs"
if($LASTEXITCODE){throw 'Visual test compilation failed'}
$evidence=[IO.Path]::GetFullPath($EvidenceDirectory)
$p=Start-Process -WindowStyle Hidden -PassThru "$out\visual.exe" -ArgumentList ('"'+$out+'\profile" "'+$evidence+'"') -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
if(!$p.WaitForExit(60000)){throw 'Visual test timeout'}
Get-Content "$out\stdout.log","$out\stderr.log"
if($p.ExitCode){throw 'Visual layout regression failed'}
