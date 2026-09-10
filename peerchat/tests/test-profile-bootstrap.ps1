param([string]$ApplicationDirectory='releases\v0.3-beta\Client',[string]$OutputDirectory='build\tests\profile-bootstrap',[uint16]$ListenerPort=29642)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references=@('System.dll','System.Core.dll','System.Web.Extensions.dll','WPF\UIAutomationClient.dll','WPF\UIAutomationTypes.dll') | ForEach-Object {'/reference:'+(Join-Path $framework $_)}
    $harness=Join-Path $OutputDirectory 'BootstrapUiSmoke.exe'
    & "$framework\csc.exe" /nologo /target:exe /platform:x64 "/out:$harness" $references .\tests\BootstrapUiSmoke.cs
    if($LASTEXITCODE -ne 0){throw 'Packaged profile-bootstrap test build failed'}
    $application=(Resolve-Path (Join-Path $ApplicationDirectory 'PeerChat.Desktop.exe')).Path
    $runtime=Join-Path (Resolve-Path 'build\tests\runtime').Path ('bootstrap-'+[Guid]::NewGuid().ToString('N'))
    & $harness $application $runtime $ListenerPort
    if($LASTEXITCODE -ne 0){throw 'Packaged profile-bootstrap regression failed'}
} finally {Pop-Location}
