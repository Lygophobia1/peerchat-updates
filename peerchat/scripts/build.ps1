$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path 'build\legacy' | Out-Null
    & gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror peerchat.c -o build\legacy\peerchat-cli.exe -lws2_32 -lbcrypt
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    Write-Host 'Built build\legacy\peerchat-cli.exe (legacy C experiment)'
} finally { Pop-Location }
