param([string]$HpkeLibrary='build/development/4.0.2-dev.188/Package/PeerChat.Hpke.dll')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/messaging-durability-audit-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
    Copy-Item -LiteralPath $HpkeLibrary -Destination $out
    & 'C:/TDM-GCC-64/bin/g++.exe' -I. -std=c++17 -D_WIN32_WINNT=0x0601 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/messaging_durability_audit_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp -o "$out/messaging-durability-audit.exe" -lncrypt -lbcrypt -lcrypt32
    if($LASTEXITCODE){throw 'Messaging durability audit compilation failed'}
    & "$out/messaging-durability-audit.exe" "$out/profiles"
    if($LASTEXITCODE){throw 'Messaging durability audit observations failed'}
    Write-Output "Evidence: $out"
}finally{Pop-Location}
