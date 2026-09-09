param([string]$OutputDirectory='build\v0.3-beta\Server')
$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    & "$framework\csc.exe" /nologo /target:exe /platform:x64 /optimize+ "/out:$OutputDirectory\PeerChat.Server.exe" /win32icon:desktop\Assets\Icons\peerchat-server.ico /reference:"$framework\System.Web.Extensions.dll" .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\PeerChat.Server.cs
    if($LASTEXITCODE -ne 0){throw 'Server build failed'}
    Copy-Item desktop/Assets/Icons/peerchat-server.ico "$OutputDirectory\peerchat-server.ico" -Force
    Write-Host "Built $OutputDirectory\PeerChat.Server.exe"
} finally { Pop-Location }
