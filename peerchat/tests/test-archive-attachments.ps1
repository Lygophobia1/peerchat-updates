param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\archive-attachments-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/archive_attachment_catalog_test.cpp storage.cpp group.cpp -o "$out\archive-attachments.exe" -lbcrypt -lcrypt32 -lncrypt
    if($LASTEXITCODE -ne 0){throw 'Archive attachment compilation failed'}
    & "$out\archive-attachments.exe"
    if($LASTEXITCODE -ne 0){throw 'Archive attachment regression failed'}
} finally {Pop-Location}
