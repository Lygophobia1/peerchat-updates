param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\reliable-endpoint-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/reliable_endpoint_test.cpp tls.cpp storage.cpp group.cpp -lbcrypt -lcrypt32 -lncrypt -lsecur32 -o "$out\reliable-endpoint.exe"
    if($LASTEXITCODE -ne 0){throw 'Shared runtime endpoint compilation failed'}
    & "$out\reliable-endpoint.exe"
    if($LASTEXITCODE -ne 0){throw 'Shared runtime endpoint regression failed'}
}finally{Pop-Location}
