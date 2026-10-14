param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/unified-completion-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item "$ClientDirectory/*" $out
$fx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
$refs=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','WPF/WindowsBase.dll','WPF/PresentationCore.dll','WPF/PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
$binary=Join-Path $out 'UnifiedCompletion.exe';$application=Join-Path $out 'PeerChat.Desktop.exe';$source=Join-Path $PSScriptRoot 'unified_completion_diagnostics_test.cs';$runner=Join-Path $PSScriptRoot 'unified_completion_diagnostics_runner.cs'
& (Join-Path $fx 'csc.exe') /nologo /target:exe /platform:x64 "/out:$binary" "/reference:$application" $refs $source $runner
if($LASTEXITCODE){throw 'Unified completion diagnostics compile failed'}
$profile=Join-Path $out 'profile';New-Item -ItemType Directory -Path $profile | Out-Null
& $binary $profile
if($LASTEXITCODE){throw 'Unified completion diagnostics regression failed'}
"Unified completion artifacts: $out"
