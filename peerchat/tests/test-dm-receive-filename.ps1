$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$run=Join-Path $root ('build\tests\dm-receive-filename-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
Push-Location $root
try {
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/dm_receive_filename_test.cpp -o (Join-Path $run 'filename.exe') -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'DM receive filename build failed'}
    & (Join-Path $run 'filename.exe') (Join-Path $run 'fixture')
    if($LASTEXITCODE -ne 0){throw 'DM receive filename regression failed'}
} finally {Pop-Location}
