param([string]$Compiler='C:\TDM-GCC-64\bin\g++.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Force -Path 'build/tests/core' | Out-Null
    & $Compiler -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests/mailbox_key_preparation_test.cpp group.cpp -o build/tests/core/mailbox-key-preparation-test.exe -lcrypt32 -lbcrypt -lncrypt
    if($LASTEXITCODE -ne 0){throw 'Mailbox key preparation test build failed'}
    & './build/tests/core/mailbox-key-preparation-test.exe'
    if($LASTEXITCODE -ne 0){throw 'Mailbox key preparation test failed'}
}finally{Pop-Location}
