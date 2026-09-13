param(
    [string]$OutputDirectory='build\tests\updater',
    [string]$ReleaseSigningCertificateThumbprint
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    & (Join-Path $PSScriptRoot '..\scripts\build-updater.ps1') -OutputDirectory $OutputDirectory
    $framework = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $references = @('System.dll','System.Core.dll','System.Web.Extensions.dll') | ForEach-Object { '/reference:' + (Join-Path $framework $_) }
    & "$framework\csc.exe" /nologo /target:exe /platform:anycpu "/out:$OutputDirectory\UpdaterTests.exe" "/reference:$OutputDirectory\PeerChat.Updater.Core.dll" $references .\tests\UpdaterTests.cs
    if ($LASTEXITCODE -ne 0) { throw 'Updater test build failed' }
    & "$framework\csc.exe" /nologo /target:exe /platform:anycpu "/out:$OutputDirectory\UpdaterHealthProbe.exe" "/reference:$OutputDirectory\PeerChat.Updater.Core.dll" $references .\tests\UpdaterHealthProbe.cs
    if ($LASTEXITCODE -ne 0) { throw 'Updater health-probe build failed' }
    & "$OutputDirectory\UpdaterTests.exe" "$OutputDirectory\UpdaterHealthProbe.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Updater tests failed' }
    if(![string]::IsNullOrWhiteSpace($ReleaseSigningCertificateThumbprint)) {
        $fixtureDirectory=Join-Path $OutputDirectory ('release-signing-'+[Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $fixtureDirectory | Out-Null
        $packagePath=Join-Path $fixtureDirectory 'PeerChat-4.0.0.zip'
        [IO.File]::WriteAllBytes($packagePath,[Text.Encoding]::UTF8.GetBytes('PeerChat local Release-signing verification fixture'))
        $packageHash=(Get-FileHash -Algorithm SHA256 -LiteralPath $packagePath).Hash.ToLowerInvariant()
        $unsignedManifest=Join-Path $fixtureDirectory 'manifest.unsigned.json'
        $signedManifest=Join-Path $fixtureDirectory 'manifest.json'
        [ordered]@{
            SchemaVersion=1; Product='PeerChat'; Channel='release'; Version='4.0.0';
            MinimumCurrentVersion='0.3.0-beta';
            PackageUrl='https://github.com/Lygophobia1/peerchat-updates/releases/download/v4.0.0/PeerChat-4.0.0.zip';
            PackageSha256=$packageHash; PackageSize=(Get-Item -LiteralPath $packagePath).Length;
            EntryExecutable='PeerChat.Desktop.exe';
            ReleaseNotesUrl='https://github.com/Lygophobia1/peerchat-updates/releases/tag/v4.0.0';
            PublishedUtc=[DateTime]::UtcNow.ToString('o')
        } | ConvertTo-Json | Set-Content -LiteralPath $unsignedManifest -Encoding UTF8
        & (Join-Path $projectRoot 'scripts\sign-release-manifest.ps1') `
            -UnsignedManifestPath $unsignedManifest `
            -PackagePath $packagePath `
            -OutputManifestPath $signedManifest `
            -CertificateThumbprint $ReleaseSigningCertificateThumbprint `
            -UpdaterCorePath (Join-Path $OutputDirectory 'PeerChat.Updater.Core.dll')
        if(!(Test-Path -LiteralPath $signedManifest)){throw 'Release-signing integration test did not produce a verified manifest.'}
        Write-Host "PASS: reviewed Release signing script produced and verified $signedManifest"
    }
} finally { Pop-Location }
