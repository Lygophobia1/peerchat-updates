param([string]$Version='4.0.2-dev.158',[string]$SourceCheckpoint='6803c3a')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 $windows=Join-Path $root "build/development/$Version"
 $linux=Join-Path $root "build/linux/$Version"
 $package=Join-Path $windows 'Package'
 $versionText=Get-Content -Raw -LiteralPath shared/VersionInfo.cs
 if($versionText -notmatch ('Display="'+[regex]::Escape($Version)+'"')){throw 'Source version mismatch'}
 & git merge-base --is-ancestor $SourceCheckpoint HEAD
 if($LASTEXITCODE){throw 'Source checkpoint is not an ancestor'}
 $SourceCheckpoint=(& git rev-parse $SourceCheckpoint).Trim()
 # Never describe uncommitted application code as the checkpoint's package.
 $production=@(& git ls-files '*.cs' '*.cpp' '*.hpp' '*.h' '*.xaml' '*.config' '*.zip' '*.ico' '*.png' '*.json' | Where-Object {$_ -notmatch '^(tests|docs)/'})
 & git diff --exit-code $SourceCheckpoint -- $production
 if($LASTEXITCODE){throw 'Production source differs from candidate checkpoint'}
 $configuration=Join-Path $windows 'Server/PeerChat.Server.exe.config'
 if((Get-FileHash $configuration).Hash -ne (Get-FileHash server/PeerChat.Server.exe.config).Hash){throw 'Server runtime configuration missing/mismatched'}
 & ./scripts/verify-server-cache-gate.ps1 -Assembly (Join-Path $windows 'Server/PeerChat.Server.exe') -Candidate
 if($LASTEXITCODE){throw 'Windows server gate audit'}
 foreach($rid in @('linux-arm','linux-arm64','linux-x64')){
  & ./scripts/verify-server-cache-gate.ps1 -Assembly (Join-Path $linux "$rid/PeerChat.Server.dll") -Candidate
  if($LASTEXITCODE){throw "Linux server gate audit: $rid"}
 }
 $documents=Join-Path $package 'Documentation'
 New-Item -ItemType Directory -Path $documents -Force|Out-Null
 Copy-Item -LiteralPath docs/GROUP_ATTACHMENTS_DEV158_VALIDATION.md,docs/GROUP_ATTACHMENTS_DEV158_DEPLOYMENT.md,server/portable/deployment/attachment-cache-settings.example.json -Destination $documents
 function FileManifest([string]$directory){
  $result=[ordered]@{}
  foreach($file in Get-ChildItem -LiteralPath $directory -File -Recurse | Sort-Object FullName){
   if($file.Name -eq 'candidate-manifest.json'){continue}
   $relative=[IO.Path]::GetRelativePath($directory,$file.FullName).Replace('\','/')
   $result[$relative]=[ordered]@{Bytes=$file.Length;SHA256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
  }
  return $result
 }
 $sources=[ordered]@{};foreach($file in $production){$sources[$file]=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()}
 [ordered]@{Version=$Version;Branch='codex/native-unicode-emoji';SourceCheckpoint=$SourceCheckpoint;WindowsBuild='build.ps1 -All -CacheCandidate; matching server runtime configuration';ServerCache='Explicit candidate only, administrator opt-in, disabled by default';PhysicalAcceptance='Pending';SourceSHA256=$sources;Payload=FileManifest $package} |
  ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $package 'candidate-manifest.json') -Encoding UTF8
 $archive=Join-Path $windows "PeerChat-Windows-$Version.zip"
 if(Test-Path -LiteralPath $archive){throw 'Never overwrite a candidate ZIP'}
 Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $package).FullName -DestinationPath $archive
 # Re-open the ZIP and verify every payload byte against the staging manifest.
 $zip=[IO.Compression.ZipFile]::OpenRead($archive)
 try {
  if(@($zip.Entries | Where-Object Name).Count -ne @(Get-ChildItem -LiteralPath $package -Recurse -File).Count){throw 'ZIP file count mismatch'}
  foreach($entry in $zip.Entries){
   if(!$entry.Name){continue}
   $expected=Join-Path $package $entry.FullName
   if(!(Test-Path -LiteralPath $expected -PathType Leaf)){throw 'Unexpected ZIP payload'}
   $input=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
   try{$actual=[BitConverter]::ToString($sha.ComputeHash($input)).Replace('-','')}finally{$sha.Dispose();$input.Dispose()}
   if($actual -ne (Get-FileHash -LiteralPath $expected -Algorithm SHA256).Hash){throw 'ZIP byte audit failed'}
  }
 }finally{$zip.Dispose()}
 $archives=@($archive)+@(Get-ChildItem -LiteralPath $linux -Filter '*.zip' -File | ForEach-Object FullName)
 $hashes=[ordered]@{};foreach($file in $archives){$hashes[$file]=[ordered]@{Bytes=(Get-Item -LiteralPath $file).Length;SHA256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()}}
 [ordered]@{Version=$Version;SourceCheckpoint=$SourceCheckpoint;PhysicalAcceptance='Pending';Archives=$hashes;WindowsServer=FileManifest (Join-Path $windows 'Server')} |
  ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $windows 'package-audit.json') -Encoding UTF8
 Copy-Item -LiteralPath docs/GROUP_ATTACHMENTS_DEV158_DEPLOYMENT.md,server/portable/deployment/attachment-cache-settings.example.json -Destination $linux
 Write-Output "PASS candidate ZIP byte audit and source/payload hashes: $archive"
 $hashes | ConvertTo-Json -Depth 5
} finally {Pop-Location}
