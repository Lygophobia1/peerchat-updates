param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $run=Join-Path $root ('build\tests\statistics-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $run | Out-Null
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/transfer_statistics_test.cpp group.cpp -o "$run\statistics.exe" -lbcrypt -lcrypt32 -lncrypt
    if($LASTEXITCODE -ne 0){throw 'Statistics compilation failed'}
    & "$run\statistics.exe"
    if($LASTEXITCODE -ne 0){throw 'Statistics regression failed'}
} finally {Pop-Location}
