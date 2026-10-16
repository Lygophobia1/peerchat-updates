param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $env:TEMP ('peerchat-windows-mailbox-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$compiler=Join-Path ${env:ProgramFiles} 'Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe'
$display=[regex]::Match((Get-Content -Raw -LiteralPath (Join-Path $root 'shared\VersionInfo.cs')),'Display="([^"]+)"').Groups[1].Value
$updater=Join-Path $root ("build\development\$display\Updater\PeerChat.Updater.Core.dll")
if(-not(Test-Path -LiteralPath $updater)){throw 'Build the matching Windows updater before running mailbox reliability tests'}
$references=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
$desktopSources=@(Get-ChildItem -LiteralPath (Join-Path $root 'desktop') -Filter '*.cs'|ForEach-Object FullName)
Push-Location $root
try {
    & $compiler /nologo /target:exe /platform:x64 /main:MailboxExecutionReliabilityTest ("/out:$out\mailbox-reliability-test.exe") $references ("/reference:$updater") /reference:System.Net.Http.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll .\shared\VersionInfo.cs @desktopSources .\tests\mailbox_execution_reliability_test.cs
    if($LASTEXITCODE -ne 0){throw 'Windows mailbox reliability test compilation failed'}
    & "$out\mailbox-reliability-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Windows mailbox reliability tests failed'}
} finally {Pop-Location}
