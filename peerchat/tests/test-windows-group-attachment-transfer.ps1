param([string]$FixtureRoot='F:\PeerChat-isolated-cache-tests')
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
$evidence=Join-Path $FixtureRoot ('windows-group-transfer-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence|Out-Null
Push-Location $sourceRoot
try {
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/windows_group_attachment_transfer_test.cpp -o "$evidence/transfer.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Windows group attachment transfer fixture compile failed'}
 & "$evidence/transfer.exe" "$evidence/profiles" | Tee-Object -FilePath "$evidence/results.log"
 if($LASTEXITCODE){throw 'Windows group attachment transfer fixture failed'}
 Write-Output "Evidence: $evidence. Isolated profiles only; shipping cache remains gated."
} finally {Pop-Location}
