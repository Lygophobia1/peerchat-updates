param([string]$ApplicationDirectory='build\candidates\v0.3-beta-meters-20260911\Client')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $outputDirectory='build\tests\voice-ui-'+[Guid]::NewGuid().ToString('N')
    $dataDirectory=Join-Path $outputDirectory 'profile'
    New-Item -ItemType Directory -Force -Path $outputDirectory,$dataDirectory | Out-Null
    Copy-Item "$ApplicationDirectory\PeerChat.Desktop.exe","$ApplicationDirectory\PeerChat.Core.dll","$ApplicationDirectory\PeerChat.Desktop.exe.config","$ApplicationDirectory\peerchat-client.ico","$ApplicationDirectory\PeerChat.Updater.Core.dll","$ApplicationDirectory\PeerChat.Updater.exe","$ApplicationDirectory\PeerChat.Updater.exe.config","$ApplicationDirectory\update-feeds.json" $outputDirectory -Force
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object {'/reference:'+(Join-Path $framework $_)}
    & "$framework\csc.exe" /nologo /target:exe /platform:x64 "/out:$outputDirectory\VoiceUiSmoke.exe" "/reference:$outputDirectory\PeerChat.Desktop.exe" $references .\tests\VoiceUiSmoke.cs
    if($LASTEXITCODE -ne 0){throw 'Voice UI smoke build failed'}
    $result=Join-Path $outputDirectory 'result.txt'
    & (Join-Path $outputDirectory 'VoiceUiSmoke.exe') $dataDirectory 29641 $result
    Get-Content $result
    if($LASTEXITCODE -ne 0){throw 'Voice UI smoke failed'}
} finally {Pop-Location}
