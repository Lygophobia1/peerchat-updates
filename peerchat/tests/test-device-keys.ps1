param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root 'build\tests\device-keys'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $root
try {
    foreach($test in @('device_key_manager_test','protected_key_registry_test')) {
        & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -I. "tests\$test.cpp" -o "$out\$test.exe" -lcrypt32
        if($LASTEXITCODE -ne 0){throw "$test compile failed ($LASTEXITCODE)"}
        & "$out\$test.exe"
        if($LASTEXITCODE -ne 0){throw "$test failed ($LASTEXITCODE)"}
    }
} finally { Pop-Location }
