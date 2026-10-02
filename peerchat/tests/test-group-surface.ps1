param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\group-surface-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Copy-Item -Path (Join-Path $ClientDirectory '*') -Destination $out
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$refs=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
& (Join-Path $fx 'csc.exe') /nologo /target:exe /platform:x64 ("/out:"+(Join-Path $out 'group-surface.exe')) ("/reference:"+(Join-Path $out 'PeerChat.Desktop.exe')) $refs (Join-Path $root 'tests\GroupSurfaceSmoke.cs')
if($LASTEXITCODE -ne 0){throw 'Group surface test compile failed'}
$before=$env:PATH
try{$env:PATH="$env:WINDIR\System32;$env:WINDIR";& (Join-Path $out 'group-surface.exe') (Join-Path $out 'profile');if($LASTEXITCODE -ne 0){throw 'Group surface test failed'}}finally{$env:PATH=$before}
