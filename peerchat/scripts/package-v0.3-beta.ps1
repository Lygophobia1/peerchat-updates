$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $buildRoot='build\v0.3-beta';$releaseRoot='releases\v0.3-beta'
    & (Join-Path $PSScriptRoot 'build-desktop.ps1') -OutputDirectory "$buildRoot\Client"
    & (Join-Path $PSScriptRoot 'build-server-admin.ps1') -OutputDirectory "$buildRoot\Server"
    New-Item -ItemType Directory -Force -Path "$releaseRoot\Client","$releaseRoot\Server" | Out-Null
    Copy-Item "$buildRoot\Client\PeerChat.Desktop.exe","$buildRoot\Client\PeerChat.Core.dll","$buildRoot\Client\PeerChat.Desktop.exe.config","$buildRoot\Client\peerchat-client.ico","$projectRoot\desktop\service-bootstrap.json" "$releaseRoot\Client" -Force
    Copy-Item "$buildRoot\Server\PeerChat.Server.exe","$buildRoot\Server\PeerChat.Server.Admin.exe","$buildRoot\Server\peerchat-server.ico" "$releaseRoot\Server" -Force
    & (Join-Path $PSScriptRoot 'verify-client-native-dependencies.ps1') -ClientDirectory "$releaseRoot\Client"
    Write-Host "Packaged PeerChat v0.3-beta in $releaseRoot"
} finally {Pop-Location}
