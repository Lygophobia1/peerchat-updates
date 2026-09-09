$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    New-Item -ItemType Directory -Force -Path 'build\legacy' | Out-Null
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -mwindows -static-libgcc -static-libstdc++ text_app.cpp share_id.cpp node.cpp tls.cpp storage.cpp -o build\legacy\PeerChat.exe -lgdi32 -luser32 -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -lshell32 -lole32 -luuid
    if ($LASTEXITCODE -ne 0) { throw 'GUI build failed' }
    Write-Host 'Built build\legacy\PeerChat.exe (legacy direct P2P text client)'
} finally { Pop-Location }
