param(
    [switch]$FreshProfile,
    [switch]$KeepBackup
)

$ErrorActionPreference = 'Stop'
$profilePath = Join-Path $env:LOCALAPPDATA 'PeerChat'
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backupPath = Join-Path ([Environment]::GetFolderPath('Desktop')) ("PeerChat-backup-" + $stamp)

Write-Host "PeerChat client profile: $profilePath"
Write-Host "A backup will be created at: $backupPath"
if (-not $FreshProfile) {
    Write-Host "Mode: reset discovery endpoint/session only; identity, contacts, and encrypted history stay intact."
} else {
    Write-Host "Mode: fresh disposable profile; the existing profile will be moved into the backup."
}
$answer = Read-Host 'Type RESET to continue'
if ($answer -cne 'RESET') { Write-Host 'Cancelled.'; exit 0 }

Get-Process -Name 'PeerChat.Desktop' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

if (-not (Test-Path -LiteralPath $profilePath)) {
    Write-Host 'No existing client profile was found. The packaged bootstrap will be used on next launch.'
    exit 0
}

New-Item -ItemType Directory -Path $backupPath -Force | Out-Null

if ($FreshProfile) {
    # Moving the directory preserves a complete recovery copy, including the DPAPI-protected
    # identity and encrypted SQLite history. The next launch creates a new profile directory.
    Move-Item -LiteralPath $profilePath -Destination (Join-Path $backupPath 'PeerChat')
    Write-Host 'Fresh profile prepared. The old identity/history is preserved in the backup.'
} else {
    foreach ($name in @('discovery-service.dat','discovery-session.dat')) {
        $source = Join-Path $profilePath $name
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination $backupPath -Force
            Remove-Item -LiteralPath $source -Force
            Write-Host "Removed saved setting: $name"
        } else {
            Write-Host "Setting not present: $name"
        }
    }
    Write-Host 'Peer ID, contacts, and encrypted local history were retained.'
}

if (-not $KeepBackup) {
    Write-Host "Backup retained at $backupPath. Remove it manually after testing if no longer needed."
}

