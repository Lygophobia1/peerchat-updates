param([Parameter(Mandatory=$true)][string]$ApplicationDirectory)
$ErrorActionPreference='Stop'
$dmSourceRoot=Split-Path $PSScriptRoot -Parent
$dmRun=Join-Path $dmSourceRoot ('build/tests/dm-history-projection-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $dmRun|Out-Null
Copy-Item -Path (Join-Path (Resolve-Path -LiteralPath $ApplicationDirectory).Path '*') -Destination $dmRun
$dmFx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$dmRefs=@('System.dll','System.Core.dll','System.Xaml.dll','WPF/WindowsBase.dll','WPF/PresentationCore.dll','WPF/PresentationFramework.dll')|ForEach-Object {'/reference:'+(Join-Path $dmFx $_)}
& "$dmFx/csc.exe" /nologo /target:exe /platform:x64 "/out:$dmRun/DmHistoryProjection.exe" "/reference:$dmRun/PeerChat.Desktop.exe" $dmRefs (Join-Path $PSScriptRoot 'dm_attachment_history_projection_test.cs')
if($LASTEXITCODE){throw 'DM attachment history projection compile failed'}
& "$dmRun/DmHistoryProjection.exe"
if($LASTEXITCODE){throw 'DM attachment history projection failed'}
Write-Output "Evidence: $dmRun"
