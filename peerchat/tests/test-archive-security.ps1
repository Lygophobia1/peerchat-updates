param([switch]$Provenance)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\archive-security-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$compiler=if(Get-Command g++ -ErrorAction SilentlyContinue){(Get-Command g++).Source}else{'C:\TDM-GCC-64\bin\g++.exe'}
Push-Location $root
try {
    $test=if($Provenance){'tests/archive_provenance_test.cpp'}else{'tests/archive_authorization_test.cpp'}
    & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. $test group.cpp -o "$out\archive-security-test.exe" -lbcrypt -lcrypt32 -lncrypt
    if($LASTEXITCODE -ne 0){throw 'Archive security test compilation failed'}
    & "$out\archive-security-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Archive security regression failed'}
} finally {Pop-Location}
