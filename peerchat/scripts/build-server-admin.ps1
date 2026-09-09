param([string]$OutputDirectory='build\v0.3-beta\Server')
$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & (Join-Path $PSScriptRoot 'build-server.ps1') -OutputDirectory $OutputDirectory
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object { '/reference:'+(Join-Path $framework $_) }
    & "$framework\csc.exe" /nologo /target:winexe /platform:x64 /optimize+ "/out:$OutputDirectory\PeerChat.Server.Admin.exe" /win32icon:desktop\Assets\Icons\peerchat-server.ico $references .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\PeerChat.Server.Admin.cs
    if($LASTEXITCODE -ne 0){throw 'Server Admin build failed'}
    Copy-Item desktop/Assets/Icons/peerchat-server.ico "$OutputDirectory\peerchat-server.ico" -Force
    Write-Host "Built $OutputDirectory\PeerChat.Server.Admin.exe"
} finally {Pop-Location}
