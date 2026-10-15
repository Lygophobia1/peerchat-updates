param([string]$Version='4.0.2-dev.160',[string]$Report='docs/WINDOWS_DEV160_ATTACHMENT_REPAIR.md')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if($Version -notmatch '^4\.0\.2-dev\.\d+$'){throw 'Unexpected development version'}
    if(git status --porcelain){throw 'Checkpoint all source changes before packaging'}
    $checkpoint=(git rev-parse HEAD).Trim()
    $out=Join-Path $root ('build/development/'+$Version)
    $runtime=Join-Path $out 'Package'
    $assembly=[Reflection.Assembly]::LoadFrom((Join-Path $runtime 'PeerChat.Desktop.exe'))
    if($assembly.GetType('PeerChat.VersionInfo').GetField('Display').GetRawConstantValue() -ne $Version){throw 'Desktop version mismatch'}
    $stage=Join-Path $out ('handoff-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item -Path (Join-Path $runtime '*') -Destination $stage -Recurse
    $docs=New-Item -ItemType Directory -Path (Join-Path $stage 'Docs')
    Copy-Item -LiteralPath $Report -Destination $docs.FullName
    $payload=@(Get-ChildItem -LiteralPath $stage -File -Recurse | ForEach-Object {
        @{path=[IO.Path]::GetRelativePath($stage,$_.FullName).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    $sources=@(git ls-files | Where-Object {$_ -match '\.(cs|cpp|hpp|h|xaml|ps1)$'} | ForEach-Object {
        @{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    @{version=$Version;checkpoint=$checkpoint;physicalAcceptance='Pending';piModified=$false;androidModified=$false;payload=$payload;sources=$sources} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $stage 'candidate-manifest.json') -Encoding utf8
    $archive=Join-Path $out ('PeerChat-Windows-'+$Version+'.zip')
    if(Test-Path -LiteralPath $archive){throw 'Preserve existing package; choose a new version instead'}
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    try {
        $files=@($zip.Entries | Where-Object {$_.Name})
        if($files.Count -ne $payload.Count+1){throw 'ZIP file count differs from staged payload'}
        foreach($item in $payload){
            $entry=$zip.GetEntry($item.path)
            if(!$entry -or $entry.Length -ne $item.bytes){throw ('ZIP entry mismatch: '+$item.path)}
            $stream=$entry.Open();$hash=[Security.Cryptography.SHA256]::Create()
            try {$digest=[Convert]::ToHexString($hash.ComputeHash($stream)).ToLowerInvariant()} finally {$hash.Dispose();$stream.Dispose()}
            if($digest -ne $item.sha256){throw ('ZIP integrity mismatch: '+$item.path)}
        }
    } finally {$zip.Dispose()}
    $audit=@{version=$Version;checkpoint=$checkpoint;archive=$archive;bytes=(Get-Item -LiteralPath $archive).Length;sha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant();verifiedFiles=$payload.Count+1}
    $audit | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'windows-repair-package-audit.json') -Encoding utf8
    $audit | ConvertTo-Json
} finally {Pop-Location}
