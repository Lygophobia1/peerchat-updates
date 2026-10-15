param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/group-staging-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try{
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/group_attachment_staging_test.cpp group.cpp -o "$out/staging.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Staging compile failed'}
 & "$out/staging.exe" "$out/profiles"
 if($LASTEXITCODE){throw 'Staging tests failed'}
 Write-Output "Evidence: $out. Isolated profile data only; no Pi/phone access."
}finally{Pop-Location}
