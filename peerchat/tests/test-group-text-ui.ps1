param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$client=(Resolve-Path -LiteralPath $ClientDirectory).Path
$out=Join-Path $root ('build\tests\group-text-ui-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$references=@('System.dll','System.Core.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
Copy-Item -LiteralPath (Join-Path $client 'PeerChat.Desktop.exe') -Destination $out
& (Join-Path $fx 'csc.exe') /nologo /target:exe /platform:x64 ("/out:"+(Join-Path $out 'group-text-ui-test.exe')) ("/reference:"+(Join-Path $out 'PeerChat.Desktop.exe')) $references (Join-Path $root 'tests\group_text_ui_test.cs')
if($LASTEXITCODE -ne 0){throw 'Group text UI projection regression compilation failed.'}
& (Join-Path $out 'group-text-ui-test.exe')
if($LASTEXITCODE -ne 0){throw 'Group text UI projection regression failed.'}
