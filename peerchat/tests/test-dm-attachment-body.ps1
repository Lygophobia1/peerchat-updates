param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/dm-attachment-body-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
& (Join-Path $root 'scripts/build-go-hpke.ps1') -OutputDirectory $out
Push-Location $root
try {
 & 'C:\TDM-GCC-64\bin\g++.exe' -std=c++17 -D_WIN32_WINNT=0x0601 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/dm_attachment_body_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp -o "$out/body.exe" -lncrypt -lbcrypt -lcrypt32 -lws2_32
 if($LASTEXITCODE){throw 'DM attachment contract compile failed'}
 & "$out/body.exe" "$out/profiles"
 if($LASTEXITCODE){throw 'DM attachment contract tests failed'}
 Write-Output "Evidence: $out"
}finally{Pop-Location}
