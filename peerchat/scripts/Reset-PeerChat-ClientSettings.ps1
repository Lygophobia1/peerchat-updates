param([switch]$FreshProfile,[switch]$FullInstall,[string]$ProfileId,[switch]$KeepBackup)
$ErrorActionPreference = 'Stop'
$installRoot = Join-Path $env:LOCALAPPDATA 'PeerChat'
$catalogPath = Join-Path $installRoot 'profiles.json'
$backupPath = Join-Path ([Environment]::GetFolderPath('Desktop')) ("PeerChat-backup-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))

if ($FullInstall) {
    $targetPath=$installRoot; $backupName='PeerChatInstall'; $mode='FULL INSTALL reset: every profile will be backed up and removed.'
} elseif (Test-Path -LiteralPath $catalogPath) {
    $catalog=Get-Content -Raw -LiteralPath $catalogPath | ConvertFrom-Json
    if ([string]::IsNullOrWhiteSpace($ProfileId)) {$ProfileId=$catalog.SelectedProfileId}
    $profile=@($catalog.Profiles | Where-Object {$_.Id -ceq $ProfileId})
    if ($profile.Count -ne 1 -or $ProfileId -notmatch '^[0-9a-f]{32}$') {throw 'The requested stable local profile ID was not found.'}
    $targetPath=Join-Path (Join-Path $installRoot 'Profiles') $ProfileId; $backupName='PeerChatProfile'
    $mode=if($FreshProfile){"reset only '$($profile[0].Name)' ($ProfileId); other profiles are untouched."}else{"clear only discovery settings/session for '$($profile[0].Name)' ($ProfileId)."}
} else {
    $targetPath=$installRoot; $backupName='PeerChat'; $mode=if($FreshProfile){'reset the legacy single profile.'}else{'clear only legacy discovery settings/session.'}
}
Write-Host "Target: $targetPath"; Write-Host "Mode: $mode"; Write-Host "Backup: $backupPath"
if ((Read-Host 'Type RESET to continue') -cne 'RESET') {Write-Host 'Cancelled.'; exit 0}
Get-Process -Name 'PeerChat.Desktop' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500
if (-not (Test-Path -LiteralPath $targetPath)) {Write-Host 'The requested profile state does not exist.'; exit 0}
New-Item -ItemType Directory -Path $backupPath -Force | Out-Null
if ($FullInstall -or $FreshProfile) {
    Move-Item -LiteralPath $targetPath -Destination (Join-Path $backupPath $backupName)
    if (-not $FullInstall) {New-Item -ItemType Directory -Path $targetPath -Force | Out-Null}
    Write-Host 'Reset complete. The next open generates a new Peer ID for this profile only.'
} else {
    foreach($name in @('discovery-service.dat','discovery-session.dat')) {$source=Join-Path $targetPath $name;if(Test-Path -LiteralPath $source){Move-Item -LiteralPath $source -Destination $backupPath -Force;Write-Host "Removed saved setting: $name"}}
    Write-Host 'Peer ID, trust, contacts, endpoints, unread state, and encrypted history were retained.'
}
if (-not $KeepBackup) {Write-Host "Backup retained at $backupPath"}
