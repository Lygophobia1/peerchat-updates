param(
    [string]$Destination = 'build\stage8-screen-share-candidate'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $target = [IO.Path]::GetFullPath((Join-Path $projectRoot $Destination))
    if (Test-Path -LiteralPath $target) { throw "Candidate destination already exists: $target" }
    $buildClient = 'build\stage8-client'
    $buildMedia = 'build\stage8-media'
    & (Join-Path $PSScriptRoot 'build-desktop.ps1') -OutputDirectory $buildClient
    & (Join-Path $PSScriptRoot 'build-screen-media.ps1') -OutputDirectory $buildMedia
    $parent = Split-Path $target -Parent
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $stage = Join-Path $parent ('.stage8-candidate-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    $runtime = @('PeerChat.Desktop.exe','PeerChat.Core.dll','PeerChat.Desktop.exe.config',
        'PeerChat.Updater.exe','PeerChat.Updater.Core.dll','PeerChat.Updater.exe.config',
        'peerchat-client.ico')
    foreach ($name in $runtime) { Copy-Item -LiteralPath (Join-Path $buildClient $name) -Destination $stage }
    Copy-Item -LiteralPath (Join-Path $buildMedia 'PeerChat.ScreenMedia.dll') -Destination $stage
    Copy-Item -LiteralPath 'desktop\service-bootstrap.json' -Destination $stage
    Copy-Item -LiteralPath 'updater\update-feeds.json' -Destination $stage
    & (Join-Path $PSScriptRoot 'verify-client-native-dependencies.ps1') -ClientDirectory $stage
    $imports = @(& objdump -p (Join-Path $stage 'PeerChat.ScreenMedia.dll') |
        ForEach-Object { if ($_ -match 'DLL Name:\s*(\S+)') { $Matches[1].ToLowerInvariant() } } |
        Sort-Object -Unique)
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect the screen adapter imports.' }
    $windows = @('d3d11.dll','mfplat.dll','dwmapi.dll','user32.dll','kernel32.dll','oleaut32.dll')
    $unexpected = @($imports | Where-Object { -not $_.StartsWith('api-ms-win-') -and $windows -notcontains $_ })
    if ($unexpected.Count -ne 0) { throw 'Unexpected screen adapter dependency: ' + ($unexpected -join ', ') }
    $hashes = Get-ChildItem -LiteralPath $stage -File | Sort-Object Name |
        ForEach-Object { (Get-FileHash -Algorithm SHA256 $_.FullName).Hash + '  ' + $_.Name }
    Set-Content -LiteralPath (Join-Path $stage 'PACKAGE-SHA256.txt') -Value $hashes -Encoding Ascii
    Move-Item -LiteralPath $stage -Destination $target
    Write-Host "Created isolated Stage 8 candidate: $target"
    Write-Host 'No release artifact or update feed was modified.'
} finally { Pop-Location }
