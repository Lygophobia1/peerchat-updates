$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$run=Join-Path $root ('build\tests\remembered-device-proof-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $run | Out-Null
& (Join-Path $root 'scripts\build-go-hpke.ps1') -OutputDirectory $run
Push-Location $root
try {
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -D_WIN32_WINNT=0x0601 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/remembered_device_native_proof_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp -o (Join-Path $run 'proof.exe') -lncrypt -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Native remembered-device proof build failed'}
    & (Join-Path $run 'proof.exe') (Join-Path $run 'fixture')
    if($LASTEXITCODE -ne 0){throw 'Native remembered-device proof regression failed'}
} finally {Pop-Location}
