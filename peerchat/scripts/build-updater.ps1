param([string]$OutputDirectory='build\v0.3-beta\Updater')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references = @('System.dll','System.Core.dll','System.Net.Http.dll','System.Web.Extensions.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll') | ForEach-Object { '/reference:' + (Join-Path $framework $_) }
    $sources = @('.\shared\VersionInfo.cs','.\updater\UpdateModels.cs','.\updater\UpdateSecurity.cs','.\updater\UpdateDownload.cs','.\updater\UpdateCoordinator.cs','.\updater\UpdateInstaller.cs')
    & "$framework\csc.exe" /nologo /target:library /platform:anycpu /optimize+ "/out:$OutputDirectory\PeerChat.Updater.Core.dll" $references $sources
    if ($LASTEXITCODE -ne 0) { throw 'Updater core build failed' }
    & "$framework\csc.exe" /nologo /target:exe /platform:anycpu /optimize+ "/out:$OutputDirectory\PeerChat.Updater.exe" "/reference:$OutputDirectory\PeerChat.Updater.Core.dll" .\updater\Program.cs
    if ($LASTEXITCODE -ne 0) { throw 'Standalone updater build failed' }
    Copy-Item .\updater\PeerChat.Updater.exe.config "$OutputDirectory\PeerChat.Updater.exe.config" -Force
    Write-Host "Built isolated updater foundation in $OutputDirectory"
} finally { Pop-Location }
