$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Start-Process -FilePath (Join-Path $projectRoot 'releases\v0.3-beta\Client\PeerChat.Desktop.exe')
