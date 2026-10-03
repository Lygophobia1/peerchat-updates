param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\relay-requests-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/relay_requests_test.cpp -o "$out\relay-requests.exe"
    if($LASTEXITCODE -ne 0){throw 'Relay execution request compilation failed'}
    & "$out\relay-requests.exe"
    if($LASTEXITCODE -ne 0){throw 'Relay execution request regression failed'}
}finally{Pop-Location}
