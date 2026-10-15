param([string]$FixtureRoot='F:\PeerChat-isolated-cache-tests')
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
$evidence=Join-Path $FixtureRoot ('windows-group-history-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence|Out-Null
Push-Location $sourceRoot
try {
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/windows_group_attachment_history_test.cpp group.cpp -o "$evidence/history.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Windows attachment history fixture compile failed'}
 & "$evidence/history.exe" "$evidence/profiles" | Tee-Object -FilePath "$evidence/results.log"
 if($LASTEXITCODE){throw 'Windows attachment history fixture failed'}
 Write-Output "Evidence: $evidence. Isolated profiles only; no existing history schema changed."
} finally {Pop-Location}
