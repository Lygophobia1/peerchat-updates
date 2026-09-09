param(
    [string]$BackupPath,
    [switch]$FreshProfile
)

$ErrorActionPreference = 'Stop'
$profilePath = Join-Path $env:LOCALAPPDATA 'PeerChat'
$desktop = [Environment]::GetFolderPath('Desktop')

if ([string]::IsNullOrWhiteSpace($BackupPath)) {
    $BackupPath = Get-ChildItem -LiteralPath $desktop -Directory -Filter 'PeerChat-backup-*' |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}
if ([string]::IsNullOrWhiteSpace($BackupPath) -or -not (Test-Path -LiteralPath $BackupPath)) {
    throw 'No PeerChat backup was found. Pass -BackupPath with a PeerChat-backup-* folder.'
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$currentBackup = Join-Path $desktop ("PeerChat-before-restore-" + $stamp)
Write-Host "Restoring from: $BackupPath"
Write-Host "Current state will be backed up to: $currentBackup"
if ($FreshProfile) {
    Write-Host 'Mode: restore the complete profile, including identity, contacts, and encrypted history.'
} else {
    Write-Host 'Mode: restore discovery endpoint/session files only.'
}
$answer = Read-Host 'Type RESTORE to continue'
if ($answer -cne 'RESTORE') { Write-Host 'Cancelled.'; exit 0 }

Get-Process -Name 'PeerChat.Desktop' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

New-Item -ItemType Directory -Path $currentBackup -Force | Out-Null
if (Test-Path -LiteralPath $profilePath) {
    if ($FreshProfile) {
        Move-Item -LiteralPath $profilePath -Destination (Join-Path $currentBackup 'PeerChat')
    } else {
        foreach ($name in @('discovery-service.dat','discovery-session.dat')) {
            $source = Join-Path $profilePath $name
            if (Test-Path -LiteralPath $source) { Move-Item -LiteralPath $source -Destination $currentBackup -Force }
        }
    }
}

if ($FreshProfile) {
    $savedProfile = Join-Path $BackupPath 'PeerChat'
    if (-not (Test-Path -LiteralPath $savedProfile)) { throw 'This backup does not contain a complete PeerChat profile.' }
    Copy-Item -LiteralPath $savedProfile -Destination $profilePath -Recurse
} else {
    New-Item -ItemType Directory -Path $profilePath -Force | Out-Null
    foreach ($name in @('discovery-service.dat','discovery-session.dat')) {
        $source = Join-Path $BackupPath $name
        if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination $profilePath -Force; Write-Host "Restored $name" }
    }
}

Write-Host "Restore complete. The previous state is backed up at $currentBackup"
