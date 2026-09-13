param([string]$ReleaseDirectory='releases\v0.3-beta-updater-candidate')
$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $buildRoot='build\v0.3-beta';$target=Join-Path $projectRoot $ReleaseDirectory
    if(Test-Path $target) { throw "Package destination already exists: $target. Choose a new release directory; retained packages are never overwritten." }
    $stage=Join-Path $buildRoot ('package-stage-'+[Guid]::NewGuid().ToString('N'))
    try {
        & (Join-Path $PSScriptRoot 'build-updater.ps1') -OutputDirectory "$buildRoot\Updater"
        & (Join-Path $PSScriptRoot 'build-desktop.ps1') -OutputDirectory "$buildRoot\Client" -UpdaterCorePath "$buildRoot\Updater\PeerChat.Updater.Core.dll"
        & (Join-Path $PSScriptRoot 'build-server.ps1') -OutputDirectory "$buildRoot\Server"
        & (Join-Path $PSScriptRoot 'build-server-admin.ps1') -OutputDirectory "$buildRoot\Server"
        New-Item -ItemType Directory -Force -Path "$stage\Client","$stage\Server" | Out-Null
        # Only declared runtime files enter a package. Source, tests, staging payloads, logs,
        # certificates, user data, and prior package contents are deliberately excluded.
        Copy-Item "$buildRoot\Client\PeerChat.Desktop.exe","$buildRoot\Client\PeerChat.Core.dll","$buildRoot\Client\PeerChat.Desktop.exe.config","$buildRoot\Client\peerchat-client.ico","$projectRoot\desktop\service-bootstrap.json" "$stage\Client" -Force
        Copy-Item "$buildRoot\Updater\PeerChat.Updater.exe","$buildRoot\Updater\PeerChat.Updater.Core.dll","$buildRoot\Updater\PeerChat.Updater.exe.config" "$stage\Client" -Force
        Copy-Item "$projectRoot\updater\update-feeds.json" "$stage\Client\update-feeds.json" -Force
        Copy-Item "$buildRoot\Server\PeerChat.Server.exe","$buildRoot\Server\PeerChat.Server.Admin.exe","$buildRoot\Server\peerchat-server.ico" "$stage\Server" -Force
        & (Join-Path $PSScriptRoot 'verify-client-native-dependencies.ps1') -ClientDirectory "$stage\Client"
        $stageFull=[IO.Path]::GetFullPath($stage)
        $hashes=Get-ChildItem $stageFull -File -Recurse | Sort-Object FullName | ForEach-Object { (Get-FileHash -Algorithm SHA256 $_.FullName).Hash+'  '+$_.FullName.Substring($stageFull.Length).TrimStart('\') }
        Set-Content -LiteralPath "$stage\PACKAGE-SHA256.txt" -Value $hashes -Encoding Ascii
        Move-Item -LiteralPath $stage -Destination $target
        Write-Host "Created clean PeerChat package in $target"
        Write-Host "Package hashes: $target\PACKAGE-SHA256.txt"
    } catch { if(Test-Path $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }; throw }
} finally {Pop-Location}
