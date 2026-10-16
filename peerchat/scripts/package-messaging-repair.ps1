param([string]$Version='4.0.2-dev.165',[string]$SourceCheckpoint='396cd9b',[switch]$IndependentDeviceDelivery)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if($Version -notmatch '^4\.0\.2-dev\.\d+$'){throw 'Invalid candidate version'}
    $source=(& git rev-parse $SourceCheckpoint).Trim()
    & git merge-base --is-ancestor $source HEAD
    if($LASTEXITCODE){throw 'Candidate source is not an ancestor'}
    $production=@(& git ls-files | Where-Object {$_ -notmatch '^(tests|docs|scripts)/' -and $_ -notmatch '(^|/)(src/test|src/sharedUiTest|src/androidTest)/'})
    & git diff --exit-code $source -- $production
    if($LASTEXITCODE){throw 'Production source changed since candidate build checkpoint'}
    $out=Join-Path $root "build/development/$Version"
    $runtime=Join-Path $out 'Package'
    $desktop=Join-Path $runtime 'PeerChat.Desktop.exe'
    if([Reflection.Assembly]::LoadFrom($desktop).GetType('PeerChat.VersionInfo').GetField('Display').GetRawConstantValue() -ne $Version){throw 'Desktop version mismatch'}
    & ./scripts/verify-client-native-dependencies.ps1 -ClientDirectory $runtime -RequireScreenMedia -RequireHpke
    if($LASTEXITCODE){throw 'Client runtime dependency audit failed'}
    $apkSource=Join-Path $root 'android/app/build/outputs/apk/debug/app-debug.apk'
    $tools=Join-Path $env:LOCALAPPDATA 'Android/Sdk/build-tools/34.0.0'
    $env:JAVA_HOME='C:\Program Files\Java\jdk-22'
    $signature=@(& (Join-Path $tools 'apksigner.bat') verify --print-certs $apkSource)
    if($LASTEXITCODE -or !($signature -match 'SHA-256 digest: db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141$')){throw 'APK signature differs from accepted Android signing identity'}
    $badging=@(& (Join-Path $tools 'aapt.exe') dump badging $apkSource)
    if($LASTEXITCODE -or !($badging -match ("versionName='"+[regex]::Escape($Version)+"'")) -or !($badging -match "native-code: 'arm64-v8a'")){throw 'APK version or ABI mismatch'}
    $android=Join-Path $out 'Android';New-Item -ItemType Directory -Path $android -Force|Out-Null
    $apk=Join-Path $android "PeerChat-Android-$Version-debug.apk"
    $zip=Join-Path $out "PeerChat-Windows-$Version.zip"
    $stage=Join-Path $out ('repair-package-'+[guid]::NewGuid().ToString('N'))
    if((Test-Path -LiteralPath $zip)-or(Test-Path -LiteralPath $apk)){throw 'Never overwrite a handed-off artifact'}
    New-Item -ItemType Directory -Path $stage|Out-Null
    Copy-Item -Path (Join-Path $runtime '*') -Destination $stage -Recurse
    $payload=@(Get-ChildItem -LiteralPath $stage -Recurse -File|ForEach-Object {
        [ordered]@{path=[IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    $scope=if($IndependentDeviceDelivery){'Phase 2 frozen verified DM targets and independent ACKs; groups unchanged'}else{'Phase 1 repairs only'}
    $phase2=if($IndependentDeviceDelivery){'Authenticated snapshot contract; no fresh snapshot means canonical-only unresolved, not all-device Delivered'}else{'Blocked pending authenticated target-snapshot contract review'}
    [ordered]@{version=$Version;sourceCheckpoint=$source;branch='codex/native-unicode-emoji';physicalAcceptance='Pending';scope=$scope;independentDeviceDelivery=$IndependentDeviceDelivery.IsPresent;phase2=$phase2;piDeployed=$false;phoneInstalled=$false;payload=$payload}|
        ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $stage 'candidate-manifest.json') -Encoding utf8
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip)
    $archive=[IO.Compression.ZipFile]::OpenRead($zip)
    try {
        if(@($archive.Entries|Where-Object Name).Count -ne $payload.Count+1){throw 'ZIP file count mismatch'}
        foreach($file in $payload){
            $entry=$archive.GetEntry($file.path);if(!$entry-or$entry.Length -ne $file.bytes){throw 'ZIP entry mismatch'}
            $stream=$entry.Open();$hash=[Security.Cryptography.SHA256]::Create()
            try{$actual=[Convert]::ToHexString($hash.ComputeHash($stream)).ToLowerInvariant()}finally{$hash.Dispose();$stream.Dispose()}
            if($actual -ne $file.sha256){throw 'ZIP payload hash mismatch'}
        }
    }finally{$archive.Dispose()}
    Copy-Item -LiteralPath $apkSource -Destination $apk
    if((Get-FileHash $apkSource).Hash -ne (Get-FileHash $apk).Hash){throw 'APK copy hash mismatch'}
    $artifacts=@($zip,$apk)+@(Get-ChildItem -LiteralPath (Join-Path $root "build/linux/$Version") -Filter '*.zip' -File|ForEach-Object FullName)
    $records=@($artifacts|ForEach-Object {[ordered]@{path=$_;bytes=(Get-Item -LiteralPath $_).Length;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}})
    [ordered]@{version=$Version;sourceCheckpoint=$source;physicalAcceptance='Pending';independentDeviceDelivery=$IndependentDeviceDelivery.IsPresent;artifacts=$records}|
        ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $out 'messaging-repair-package-audit.json') -Encoding utf8
    $records|ConvertTo-Json
    Write-Output 'PASS version, signature, dependency, source and ZIP byte audit. No install or deployment.'
}finally{Pop-Location}
