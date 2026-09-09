$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $buildRoot='build\v0.1-beta';$releaseRoot='releases\v0.1-beta'
    if(Test-Path -LiteralPath "$releaseRoot\FROZEN.txt"){
        throw 'releases\v0.1-beta is frozen. Use scripts\package-v0.2-beta.ps1 for active development.'
    }
    & (Join-Path $PSScriptRoot 'build-desktop.ps1') -OutputDirectory "$buildRoot\Client"
    & (Join-Path $PSScriptRoot 'build-server-admin.ps1') -OutputDirectory "$buildRoot\Server"
    New-Item -ItemType Directory -Force -Path "$releaseRoot\Client","$releaseRoot\Server" | Out-Null
    Copy-Item "$buildRoot\Client\PeerChat.Desktop.exe","$buildRoot\Client\PeerChat.Core.dll","$buildRoot\Client\PeerChat.Desktop.exe.config","$buildRoot\Client\peerchat-client.ico" "$releaseRoot\Client" -Force
    Copy-Item "$buildRoot\Server\PeerChat.Server.exe","$buildRoot\Server\PeerChat.Server.Admin.exe","$buildRoot\Server\peerchat-server.ico" "$releaseRoot\Server" -Force
    Write-Host "Packaged PeerChat v0.1-beta in $releaseRoot"
} finally {Pop-Location}
