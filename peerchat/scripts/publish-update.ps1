[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Development','Release')][string]$Channel,
    [Parameter(Mandatory=$true)][string]$Version,
    [switch]$Publish,
    [switch]$DryRun,
    [string]$UpdateRepositoryPath,
    [string]$Tag,
    [string]$MinimumCurrentVersion='0.3.0-beta',
    [string]$PromotePackagePath,
    [string]$TestedDevelopmentManifestPath,
    [string]$ReleaseSigningCertificateThumbprint,
    [string]$ReleaseSigningKeyId='peerchat-release-2026-01'
)

$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$githubRepository='Lygophobia1/peerchat-updates'
$channelName=$Channel.ToLowerInvariant()
$stages=New-Object System.Collections.Generic.List[string]

function Stop-Publish([string]$Message) { throw $Message }
function Invoke-Checked([string]$File,[string[]]$Arguments,[string]$Failure) {
    & $File @Arguments
    if($LASTEXITCODE -ne 0){Stop-Publish $Failure}
}
function Get-CurrentPeerChatVersion {
    $source=Get-Content -Raw (Join-Path $projectRoot 'shared\VersionInfo.cs')
    $match=[regex]::Match($source,'Display\s*=\s*"([^"]+)"')
    if(!$match.Success){Stop-Publish 'Could not read VersionInfo.Display from shared\VersionInfo.cs.'}
    return $match.Groups[1].Value
}
function Assert-SemanticVersion([string]$Value,[string]$Label) {
    if($Value -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$'){
        Stop-Publish "$Label is not a supported semantic version: $Value"
    }
}
function Assert-UpdateRepository([string]$Path,[string]$ChannelDirectory) {
    if([string]::IsNullOrWhiteSpace($Path)){Stop-Publish 'Publication requires -UpdateRepositoryPath. Usage: .\scripts\publish-update.ps1 -Channel Development -Version <version> -UpdateRepositoryPath "C:\path\to\peerchat-updates" -Publish'}
    $resolved=(Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    $insideWorkTree=& git -C $resolved rev-parse --is-inside-work-tree 2>$null
    if($LASTEXITCODE -ne 0 -or $insideWorkTree -ne 'true'){Stop-Publish "Update repository is not a Git checkout: $resolved"}
    $status=& git -C $resolved status --porcelain
    if($LASTEXITCODE -ne 0){Stop-Publish "Could not inspect update repository status: $resolved"}
    if($status){Stop-Publish "Update repository must be clean before publication. Refusing to commit unrelated changes in $resolved."}
    $remote=& git -C $resolved remote get-url origin
    if($LASTEXITCODE -ne 0 -or $remote -notmatch 'Lygophobia1[/:]peerchat-updates(?:\.git)?$'){
        Stop-Publish "Update repository origin is not $githubRepository. Refusing to publish to an unexpected repository."
    }
    $channelPath=Join-Path $resolved $ChannelDirectory
    if((Test-Path $channelPath) -and !(Get-Item -LiteralPath $channelPath).PSIsContainer){Stop-Publish "Expected channel path is not a directory: $channelPath"}
    if(!(Test-Path $channelPath)){Write-Host "Channel directory will be created only after the remote asset is verified: $channelPath"}
    return $resolved
}
function Find-GitHubCli {
    $command=Get-Command gh -ErrorAction SilentlyContinue
    if($command){return $command.Source}
    $candidates=@(
        (Join-Path $env:ProgramFiles 'GitHub CLI\gh.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'GitHub CLI\gh.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\GitHub CLI\gh.exe')
    )
    foreach($candidate in $candidates){if($candidate -and (Test-Path -LiteralPath $candidate)){return $candidate}}
    Stop-Publish 'GitHub CLI (gh) is required for publication but was not found on PATH or in the standard GitHub CLI install location.'
}
function Assert-GitHubAuthentication {
    $script:GitHubCli=Find-GitHubCli
    & $script:GitHubCli auth status --hostname github.com
    if($LASTEXITCODE -ne 0){Stop-Publish 'GitHub CLI is not authenticated for github.com. Run gh auth login before publication.'}
}
function Invoke-ReleaseManifestSigning([string]$UnsignedManifest,[string]$Package,[string]$SignedManifest,[string]$UpdaterCore) {
    $windowsPowerShell=Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if(!(Test-Path -LiteralPath $windowsPowerShell)){Stop-Publish 'Windows PowerShell is required for the .NET Framework Release signing verifier.'}
    $signingScript=Join-Path $PSScriptRoot 'sign-release-manifest.ps1'
    & $windowsPowerShell -NoProfile -ExecutionPolicy RemoteSigned -File $signingScript `
        -UnsignedManifestPath $UnsignedManifest `
        -PackagePath $Package `
        -OutputManifestPath $SignedManifest `
        -CertificateThumbprint $ReleaseSigningCertificateThumbprint `
        -KeyId $ReleaseSigningKeyId `
        -UpdaterCorePath $UpdaterCore
    if($LASTEXITCODE -ne 0){Stop-Publish 'The reviewed Release manifest signing script failed.'}
}
function New-ClientZip([string]$ClientDirectory,[string]$ZipPath) {
    if(Test-Path -LiteralPath (Join-Path $ClientDirectory 'HPKE-EVALUATION.json')) {
        Stop-Publish 'Evaluation HPKE provider has no approved shipping license; updater packaging/publication is prohibited.'
    }
    $core=Join-Path $ClientDirectory 'PeerChat.Core.dll'
    if((Test-Path -LiteralPath $core) -and [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($core)).Contains('PEERCHAT_HPKE_EVALUATION_UNLICENSED')) {
        Stop-Publish 'Evaluation HPKE binary is not licensed for updater packaging/publication.'
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if(Test-Path -LiteralPath $ZipPath){Stop-Publish "Refusing to overwrite an existing ZIP: $ZipPath"}
    [IO.Compression.ZipFile]::CreateFromDirectory($ClientDirectory,$ZipPath,[IO.Compression.CompressionLevel]::Optimal,$false)
    $diskEntries=@(Get-ChildItem -LiteralPath $ClientDirectory -File -Recurse | ForEach-Object {$_.FullName.Substring($ClientDirectory.Length).TrimStart('\').Replace('\','/') } | Sort-Object)
    $archive=[IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {$zipEntries=@($archive.Entries | Where-Object {!$_.FullName.EndsWith('/')} | ForEach-Object {$_.FullName} | Sort-Object)} finally {$archive.Dispose()}
    if(@(Compare-Object $diskEntries $zipEntries).Count -ne 0){Stop-Publish 'ZIP contents do not exactly match the staged Client runtime files.'}
}
function New-Manifest([string]$ZipPath,[string]$ManifestPath,[string]$ReleaseTag) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $licenseArchive=[IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach($entry in $licenseArchive.Entries) {
            if($entry.Name -eq 'HPKE-EVALUATION.json'){Stop-Publish 'Evaluation package cannot enter a normal updater channel.'}
            if($entry.Name -eq 'PeerChat.Core.dll') {
                if($entry.Length -gt 64MB){Stop-Publish 'Core binary exceeds packaging inspection bound.'}
                $licenseInput=$entry.Open();$buffer=New-Object IO.MemoryStream
                try{$licenseInput.CopyTo($buffer);if([Text.Encoding]::ASCII.GetString($buffer.ToArray()).Contains('PEERCHAT_HPKE_EVALUATION_UNLICENSED')){Stop-Publish 'Evaluation HPKE binary cannot enter a normal updater channel.'}}
                finally{$licenseInput.Dispose();$buffer.Dispose()}
            }
        }
    } finally {$licenseArchive.Dispose()}
    $hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $ZipPath).Hash.ToLowerInvariant()
    $size=(Get-Item -LiteralPath $ZipPath).Length
    if($size -le 0 -or $hash.Length -ne 64){Stop-Publish 'ZIP hash or size is invalid.'}
    $assetName=[IO.Path]::GetFileName($ZipPath)
    $assetUrl="https://github.com/$githubRepository/releases/download/$ReleaseTag/$assetName"
    $releaseUrl="https://github.com/$githubRepository/releases/tag/$ReleaseTag"
    $manifest=[ordered]@{
        SchemaVersion=1
        Product='PeerChat'
        Channel=$channelName
        Version=$Version
        MinimumCurrentVersion=$MinimumCurrentVersion
        PackageUrl=$assetUrl
        PackageSha256=$hash
        PackageSize=$size
        EntryExecutable='PeerChat.Desktop.exe'
        ReleaseNotesUrl=$releaseUrl
        PublishedUtc=[DateTime]::UtcNow.ToString('o')
    }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ManifestPath -Encoding UTF8
    return [pscustomobject]@{Hash=$hash;Size=$size;AssetUrl=$assetUrl;ReleaseUrl=$releaseUrl;AssetName=$assetName}
}
function Assert-RemoteAsset([string]$ReleaseTag,[string]$AssetName,[long]$ExpectedSize,[string]$ExpectedUrl) {
    $releaseJson=& $script:GitHubCli api "repos/$githubRepository/releases/tags/$ReleaseTag"
    if($LASTEXITCODE -ne 0){Stop-Publish "Could not inspect GitHub release $ReleaseTag after upload."}
    $release=$releaseJson | ConvertFrom-Json
    $asset=@($release.assets | Where-Object {$_.name -eq $AssetName})
    if($asset.Count -ne 1){Stop-Publish "GitHub release $ReleaseTag does not contain exactly one $AssetName asset."}
    if([int64]$asset[0].size -ne $ExpectedSize){Stop-Publish 'Remote asset size does not match the generated manifest.'}
    if($asset[0].browser_download_url -ne $ExpectedUrl){Stop-Publish 'Remote asset URL does not match the generated manifest.'}
}

if(($Publish -and $DryRun) -or (!$Publish -and !$DryRun)){Stop-Publish 'Specify exactly one of -DryRun or -Publish.'}
Assert-SemanticVersion $Version 'Version'
Assert-SemanticVersion $MinimumCurrentVersion 'MinimumCurrentVersion'
if([string]::IsNullOrWhiteSpace($Tag)){$Tag='v'+$Version}
if($Tag -notmatch '^v[0-9A-Za-z._-]+$'){Stop-Publish "Tag contains unsupported characters: $Tag"}
if($Channel -eq 'Development' -and $Version -notmatch '-') {Stop-Publish 'Development versions must be semantic pre-releases (for example 4.0.1-dev.1).'}
if($Channel -eq 'Release' -and $Version -match '-') {Stop-Publish 'Release versions must not contain a pre-release suffix.'}
if($Channel -eq 'Release' -and [string]::IsNullOrWhiteSpace($PromotePackagePath)){Stop-Publish 'Release validation requires -PromotePackagePath so it can prove exact tested Development bytes rather than rebuild them.'}
if($Channel -eq 'Release' -and [string]::IsNullOrWhiteSpace($TestedDevelopmentManifestPath)){Stop-Publish 'Release validation requires -TestedDevelopmentManifestPath to compare the promoted ZIP hash and size against the accepted Development artifact.'}
if($Channel -eq 'Release' -and [string]::IsNullOrWhiteSpace($ReleaseSigningCertificateThumbprint)){Stop-Publish 'Release validation and publication require -ReleaseSigningCertificateThumbprint. Unsigned Release manifests are never generated.'}
if($Channel -eq 'Development' -and $PromotePackagePath){Stop-Publish '-PromotePackagePath is reserved for exact-byte Release promotion.'}
if($Channel -eq 'Development' -and ($ReleaseSigningCertificateThumbprint -or $PSBoundParameters.ContainsKey('ReleaseSigningKeyId'))){Stop-Publish 'Release signing parameters cannot be used for Development publication.'}
if((Get-CurrentPeerChatVersion) -ne $Version){Stop-Publish "VersionInfo.Display must equal -Version before publishing. Current source version is $(Get-CurrentPeerChatVersion); requested $Version."}

Push-Location $projectRoot
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $updateRepository=$null
    if($Publish){$updateRepository=Assert-UpdateRepository $UpdateRepositoryPath $channelName}
    $safeVersion=$Version -replace '[^0-9A-Za-z._-]','_'
    $runId=(Get-Date -Format 'yyyyMMddHHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $runDirectory=Join-Path $projectRoot ("build\publish\$channelName-$safeVersion-$runId")
    New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
    $zipPath=Join-Path $runDirectory ("PeerChat-$Version.zip")

    if($Channel -eq 'Release') {
        $zipPath=(Resolve-Path -LiteralPath $PromotePackagePath -ErrorAction Stop).Path
        $stages.Add('exact-byte promotion package selected')
        $archive=[IO.Compression.ZipFile]::OpenRead($zipPath); try {if(!($archive.Entries | Where-Object {$_.FullName -eq 'PeerChat.Desktop.exe'})){Stop-Publish 'Promoted ZIP is missing PeerChat.Desktop.exe.'}} finally {$archive.Dispose()}
    } else {
        $releaseDirectory="build\publish\package-$channelName-$safeVersion-$runId"
        & (Join-Path $PSScriptRoot 'package-v0.3-beta.ps1') -ReleaseDirectory $releaseDirectory
        if($LASTEXITCODE -ne 0){Stop-Publish 'PeerChat packaging pipeline failed.'}
        $client=Join-Path $projectRoot "$releaseDirectory\Client"
        New-ClientZip $client $zipPath
        $stages.Add('fresh runtime-only Client ZIP built and package dependency audit passed')
    }
    if(!(Test-Path -LiteralPath $zipPath)){Stop-Publish "Prepared ZIP is missing: $zipPath"}
    $candidateManifest=Join-Path $runDirectory 'manifest.json'
    $manifestInput=$(if($Channel -eq 'Release'){Join-Path $runDirectory 'manifest.unsigned.json'}else{$candidateManifest})
    $metadata=New-Manifest $zipPath $manifestInput $Tag
    if($Channel -eq 'Release') {
        $tested=(Get-Content -Raw (Resolve-Path -LiteralPath $TestedDevelopmentManifestPath -ErrorAction Stop).Path | ConvertFrom-Json)
        if($tested.Channel -ne 'development'){Stop-Publish 'TestedDevelopmentManifestPath must name a Development manifest.'}
        if($tested.PackageSha256 -ne $metadata.Hash -or [int64]$tested.PackageSize -ne $metadata.Size){Stop-Publish 'Promoted Release ZIP does not exactly match the tested Development manifest hash and byte size.'}
        $stages.Add('promoted ZIP hash and size match tested Development manifest')
        $verificationBuild=Join-Path $runDirectory 'signature-verifier'
        & (Join-Path $PSScriptRoot 'build-updater.ps1') -OutputDirectory $verificationBuild
        if($LASTEXITCODE -ne 0){Stop-Publish 'Could not build the trusted Release signature verifier.'}
        Invoke-ReleaseManifestSigning $manifestInput $zipPath $candidateManifest (Join-Path $verificationBuild 'PeerChat.Updater.Core.dll')
        if(!(Test-Path -LiteralPath $candidateManifest)){Stop-Publish 'Release signing did not produce a verified signed manifest.'}
        $stages.Add("Release manifest signed and accepted by the packaged verifier with key ID $ReleaseSigningKeyId")
    }
    $stages.Add('ZIP contents, SHA-256, size, and candidate manifest verified')

    if($DryRun){
        Write-Host 'DRY RUN: no GitHub or Git update-repository changes were made.'
        Write-Host "Would run: gh auth status --hostname github.com"
        Write-Host "Would run: gh release create $Tag --repo $githubRepository" + $(if($Channel -eq 'Development'){' --prerelease'}else{''})
        Write-Host "Would run: gh release upload $Tag $zipPath --clobber --repo $githubRepository"
        Write-Host "Would update: $channelName/manifest.json in -UpdateRepositoryPath, then git add/commit/push."
    } else {
        Assert-GitHubAuthentication
        try {
            & $script:GitHubCli release view $Tag --repo $githubRepository 2>$null
            if($LASTEXITCODE -ne 0){
                $create=@('release','create',$Tag,'--repo',$githubRepository,'--title',"PeerChat $Version",'--generate-notes')
                if($Channel -eq 'Development'){$create += '--prerelease'}
                Invoke-Checked $script:GitHubCli $create "Could not create GitHub release $Tag."
                $stages.Add('GitHub release created')
            } else {$stages.Add('existing GitHub release reused')}
            Invoke-Checked $script:GitHubCli @('release','upload',$Tag,$zipPath,'--clobber','--repo',$githubRepository) 'Could not upload the update ZIP asset.'
            Assert-RemoteAsset $Tag $metadata.AssetName $metadata.Size $metadata.AssetUrl
            $stages.Add('GitHub ZIP asset uploaded and remotely verified')

            $manifestRelative="$channelName/manifest.json"
            $manifestPath=Join-Path $updateRepository $manifestRelative
            New-Item -ItemType Directory -Force -Path (Split-Path $manifestPath -Parent) | Out-Null
            Copy-Item -LiteralPath $candidateManifest -Destination $manifestPath -Force
            Invoke-Checked 'git' @('-C',$updateRepository,'add','--',$manifestRelative) 'Could not stage the channel manifest.'
            $staged=@(& git -C $updateRepository diff --cached --name-only)
            if($staged.Count -ne 1 -or $staged[0].Replace('\','/') -ne $manifestRelative){Stop-Publish 'Refusing to commit unexpected staged update-repository files.'}
            Invoke-Checked 'git' @('-C',$updateRepository,'commit','-m',"Publish PeerChat $Version $channelName update") 'Could not commit the channel manifest.'
            Invoke-Checked 'git' @('-C',$updateRepository,'push') 'Could not push the channel manifest.'
            $stages.Add('stable channel manifest committed and pushed last')
        } catch {
            Write-Error ("Publication stopped after: "+($stages -join '; ')+". The stable manifest was not intentionally moved before remote asset verification. Preserve the ZIP and candidate manifest; inspect the named release/update repository and rerun only after resolving the failure.")
            throw
        }
    }

    [pscustomobject]@{
        Channel=$Channel;Version=$Version;Tag=$Tag;ZipPath=$zipPath;AssetUrl=$metadata.AssetUrl;
        ManifestPath=$candidateManifest;ManifestUrl="https://raw.githubusercontent.com/$githubRepository/main/$channelName/manifest.json";
        ZipSize=$metadata.Size;Sha256=$metadata.Hash;GitHubReleaseUrl=$metadata.ReleaseUrl;
        SigningKeyId=$(if($Channel -eq 'Release'){$ReleaseSigningKeyId}else{$null});
        SigningCertificateThumbprint=$(if($Channel -eq 'Release'){($ReleaseSigningCertificateThumbprint -replace '[^0-9A-Fa-f]','').ToUpperInvariant()}else{$null});Stages=($stages -join '; ')
    } | Format-List
} finally {Pop-Location}
