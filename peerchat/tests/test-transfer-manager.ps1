param([string]$OutputDirectory='build\tests\transfer-manager')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/transfer_manager_test.cpp -o "$OutputDirectory\transfer-manager-test.exe" -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Transfer manager build failed'}
    & "$OutputDirectory\transfer-manager-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Transfer manager regression failed'}
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/transfer_restart_test.cpp -o "$OutputDirectory\transfer-restart-test.exe" -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Transfer restart build failed'}
    & "$OutputDirectory\transfer-restart-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Transfer restart regression failed'}
    & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/transfer_large_preparation_test.cpp -o "$OutputDirectory\transfer-large-preparation-test.exe" -lbcrypt -lcrypt32 -lpsapi
    if($LASTEXITCODE -ne 0){throw 'Large transfer build failed'}
    & "$OutputDirectory\transfer-large-preparation-test.exe"
    if($LASTEXITCODE -ne 0){throw 'Large transfer regression failed'}
} finally {Pop-Location}
