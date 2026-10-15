param([string]$FixtureRoot='F:\PeerChat-isolated-cache-tests')
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
$evidence=Join-Path $FixtureRoot ('windows-group-output-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence|Out-Null
Push-Location $sourceRoot
try {
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/windows_group_attachment_output_test.cpp -o "$evidence/output.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Windows attachment output fixture compile failed'}
 & "$evidence/output.exe" "$evidence/profiles" | Tee-Object -FilePath "$evidence/results.log"
 if($LASTEXITCODE){throw 'Windows attachment output fixture failed'}
 Write-Output "Evidence: $evidence. Isolated verified access/export files only."
} finally {Pop-Location}
