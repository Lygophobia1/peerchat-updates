param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\device-verification-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/device_verification_test.cpp tls.cpp storage.cpp group.cpp -lbcrypt -lcrypt32 -lncrypt -lsecur32 -o "$out\device-verification.exe"
    if($LASTEXITCODE -ne 0){throw 'Device verification regression compilation failed'}
    & "$out\device-verification.exe"
    if($LASTEXITCODE -ne 0){throw 'Device verification regression failed'}
}finally{Pop-Location}
