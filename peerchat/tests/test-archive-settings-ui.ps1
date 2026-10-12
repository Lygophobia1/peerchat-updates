param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\archive-settings-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$client=(Resolve-Path -LiteralPath $ClientDirectory).Path
Copy-Item -Path "$client\*" -Destination $out
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$refs=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
& "$fx\csc.exe" /nologo /target:exe /platform:x64 "/out:$out\archive-settings.exe" "/reference:$out\PeerChat.Desktop.exe" $refs "$PSScriptRoot\archive_settings_smoke.cs"
if($LASTEXITCODE -ne 0){throw 'Archive Settings smoke compilation failed'}
$process=Start-Process -WindowStyle Hidden -PassThru -FilePath "$out\archive-settings.exe" -ArgumentList ('"'+(Join-Path $out 'profile')+'"') -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
$null=$process.Handle # Retain the process handle so a fast exit still exposes its actual exit code.
if(!$process.WaitForExit(60000)){throw "Archive Settings smoke timeout, process=$($process.Id)"}
Get-Content -LiteralPath "$out\stdout.log","$out\stderr.log"
if($process.ExitCode -ne 0){throw 'Archive Settings smoke failed'}
