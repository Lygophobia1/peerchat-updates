param([string]$Version='4.0.2-dev.18',[string]$PreviousVersion='4.0.2-dev.17',
      [string]$ArtifactDirectory='DevelopmentUpdater',
      [string]$AndroidSdkRoot="$env:LOCALAPPDATA\Android\Sdk")
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root "build\development\$Version"
function Assert-Version([string]$Path) {
    $assembly=[Reflection.Assembly]::LoadFile($Path)
    $type=$assembly.GetType('PeerChat.VersionInfo')
    if(!$type -or $type.GetField('Display').GetRawConstantValue() -ne $Version -or
       $type.GetField('ReleaseFolder').GetRawConstantValue() -ne "v$Version") {throw "Version mismatch: $Path"}
}
$source=Get-Content -Raw -LiteralPath (Join-Path $root 'shared\VersionInfo.cs')
if($source -notmatch ('Display="'+[regex]::Escape($Version)+'"')){throw 'Shared source version mismatch'}
foreach($relative in @('Client\PeerChat.Desktop.exe','Server\PeerChat.Server.exe',
 'ServerAdmin\PeerChat.Server.Admin.exe','Updater\PeerChat.Updater.Core.dll','Package\PeerChat.Desktop.exe')){
    Assert-Version (Join-Path $output $relative)
}
if((Get-Content -Raw -LiteralPath (Join-Path $output 'VersionInfo.cs')) -notmatch [regex]::Escape($Version)){throw 'Generated metadata mismatch'}
$aapt=Join-Path $AndroidSdkRoot 'build-tools\34.0.0\aapt.exe'
foreach($relative in @('debug\app-debug.apk','release\app-release-unsigned.apk')){
    $apk=Join-Path $root "android\app\build\outputs\apk\$relative"
    $badging=& $aapt dump badging $apk
    if($LASTEXITCODE -ne 0 -or ($badging -join "`n") -notmatch ("versionName='"+[regex]::Escape($Version)+"'")){throw "APK version mismatch: $relative"}
}
Write-Host "PASS: shared/generated sources, Windows About/diagnostic version constants and Android APK versions match $Version."
$package=Join-Path $output 'Package'
$allowed=@('PeerChat.Hpke.dll','PeerChat.Desktop.exe','PeerChat.Core.dll','PeerChat.Desktop.exe.config','peerchat-client.ico',
 'PeerChat.Updater.Core.dll','PeerChat.Updater.exe','PeerChat.Updater.exe.config','PeerChat.ScreenMedia.dll',
 'PeerChat.AudioProcessing.dll','PeerChat.AudioReceive.dll','service-bootstrap.json','update-feeds.json')
$files=@(Get-ChildItem -LiteralPath $package -File)
& (Join-Path $root 'tests\test-hpke-package-containment.ps1') -PackageDirectory $package
if($files.Count -ne $allowed.Count -or @($files | Where-Object {$_.DirectoryName -ne $package -or $_.Name -notin $allowed}).Count){throw 'Unexpected package runtime files'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$prepared=Join-Path $output $ArtifactDirectory
if(Test-Path -LiteralPath $prepared){throw 'Prepared artifact directory already exists; never overwrite a candidate'}
New-Item -ItemType Directory -Path $prepared | Out-Null
$zip=Join-Path $prepared "PeerChat-$Version.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($package,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
$hash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest=[ordered]@{SchemaVersion=1;Product='PeerChat';Channel='development';Version=$Version;
 MinimumCurrentVersion=$PreviousVersion;PackageUrl="https://github.com/Lygophobia1/peerchat-updates/releases/download/v$Version/PeerChat-$Version.zip";
 PackageSha256=$hash;PackageSize=(Get-Item -LiteralPath $zip).Length;EntryExecutable='PeerChat.Desktop.exe';
 ReleaseNotesUrl="https://github.com/Lygophobia1/peerchat-updates/releases/tag/v$Version";PublishedUtc=[DateTime]::UtcNow.ToString('o')}
$manifestPath=Join-Path $prepared 'manifest.json'
$manifest | ConvertTo-Json | Set-Content -LiteralPath $manifestPath -Encoding UTF8
Add-Type -Path (Join-Path $output 'Updater\PeerChat.Updater.Core.dll')
$config=[PeerChat.Updater.UpdateFeedConfiguration]::CreateDefaults()
$config.Channel='development';$config.RequireSignature=$false
$config.ManifestUrl='https://example.invalid/development/manifest.json'
$config.StagingRoot=Join-Path $prepared 'ValidationStaging'
$parsed=[PeerChat.Updater.UpdateJson]::ParseManifest((Get-Content -Raw -LiteralPath $manifestPath),$config)
if([PeerChat.Updater.SemanticVersion]::Parse($parsed.Version).CompareTo([PeerChat.Updater.SemanticVersion]::Parse($PreviousVersion)) -le 0){throw 'Not an upgrade from pre-Android baseline'}
$verifier=[PeerChat.Updater.SignatureBoundaryVerifier]::new($false)
$service=[PeerChat.Updater.PeerChatUpdateService]::new($config,$null,$verifier,(Join-Path $output 'Updater\PeerChat.Updater.exe'))
try{
    $downloaded=[PeerChat.Updater.DownloadedUpdate]::new();$downloaded.Manifest=$parsed;$downloaded.PackagePath=$zip
    $extracted=$service.PrepareUpdate($downloaded)
    Assert-Version (Join-Path $extracted.StagedDirectory 'PeerChat.Desktop.exe')
    foreach($file in Get-ChildItem -LiteralPath $package -File -Recurse){$relative=$file.FullName.Substring($package.Length).TrimStart('\');if((Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath (Join-Path $extracted.StagedDirectory $relative)).Hash){throw 'Extracted runtime hash mismatch'}}
}finally{$service.Dispose()}
Write-Host "PASS: $PreviousVersion -> $Version ordering, Development manifest, SHA256, production updater safe extraction and exact runtime hashes."
Write-Host 'Unsigned Development policy only; no release signature claimed; no upload/feed publication performed.'
Write-Host "Package: $zip"
Write-Host "SHA256: $hash"
