param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\reliable-wire-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/reliable_peer_protocol_test.cpp group.cpp -lbcrypt -lcrypt32 -o "$out\reliable-wire.exe"
    if($LASTEXITCODE -ne 0){throw 'Shared wire compilation failed'}
    & "$out\reliable-wire.exe"
    if($LASTEXITCODE -ne 0){throw 'Shared wire regression failed'}
}finally{Pop-Location}
