param([string]$BackupPath,[switch]$FreshProfile,[switch]$FullInstall,[string]$ProfileId)
$ErrorActionPreference = 'Stop'
$installRoot=Join-Path $env:LOCALAPPDATA 'PeerChat'; $catalogPath=Join-Path $installRoot 'profiles.json'; $desktop=[Environment]::GetFolderPath('Desktop')
if([string]::IsNullOrWhiteSpace($BackupPath)){$BackupPath=Get-ChildItem -LiteralPath $desktop -Directory -Filter 'PeerChat-backup-*' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName}
if([string]::IsNullOrWhiteSpace($BackupPath)-or-not(Test-Path -LiteralPath $BackupPath)){throw 'No PeerChat backup was found.'}
if($FullInstall){
    $savedPath=Join-Path $BackupPath 'PeerChatInstall';$targetPath=$installRoot;$backupName='PeerChatInstall';if(-not(Test-Path -LiteralPath $savedPath)){throw 'This is not a full-install backup.'}
}elseif(Test-Path -LiteralPath $catalogPath){
    $catalog=Get-Content -Raw -LiteralPath $catalogPath|ConvertFrom-Json;if([string]::IsNullOrWhiteSpace($ProfileId)){$ProfileId=$catalog.SelectedProfileId};$profile=@($catalog.Profiles|Where-Object{$_.Id -ceq $ProfileId})
    if($profile.Count-ne 1-or $ProfileId-notmatch '^[0-9a-f]{32}$'){throw 'The requested stable local profile ID was not found.'}
    $targetPath=Join-Path (Join-Path $installRoot 'Profiles') $ProfileId;$backupName='PeerChatProfile';$savedPath=Join-Path $BackupPath 'PeerChatProfile';if(-not(Test-Path -LiteralPath $savedPath)){throw 'This backup does not contain one isolated local profile.'}
}else{$targetPath=$installRoot;$backupName='PeerChat';$savedPath=Join-Path $BackupPath 'PeerChat'}
$currentBackup=Join-Path $desktop ("PeerChat-before-restore-"+(Get-Date -Format 'yyyyMMdd-HHmmss'))
Write-Host "Restoring only target: $targetPath";Write-Host "From: $savedPath";Write-Host "Safety backup: $currentBackup"
if((Read-Host 'Type RESTORE to continue')-cne 'RESTORE'){Write-Host 'Cancelled.';exit 0}
Get-Process -Name 'PeerChat.Desktop' -ErrorAction SilentlyContinue|Stop-Process -Force -ErrorAction SilentlyContinue;Start-Sleep -Milliseconds 500
New-Item -ItemType Directory -Path $currentBackup -Force|Out-Null
if($FreshProfile-or$FullInstall){if(Test-Path -LiteralPath $targetPath){Move-Item -LiteralPath $targetPath -Destination (Join-Path $currentBackup $backupName)};Copy-Item -LiteralPath $savedPath -Destination $targetPath -Recurse}else{New-Item -ItemType Directory -Path $targetPath -Force|Out-Null;foreach($name in @('discovery-service.dat','discovery-session.dat')){$source=Join-Path $savedPath $name;if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination $targetPath -Force}}}
Write-Host 'Restore complete. Other local profiles were not changed.'
