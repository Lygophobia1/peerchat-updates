param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/cache-scope-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try{
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/transfer_cache_scope_test.cpp -o "$out/scope.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Cache scope compile failed'}
 & "$out/scope.exe"
 if($LASTEXITCODE){throw 'Cache scope regression failed'}
 Write-Output "Evidence: $out. Local purpose adapter only, no enabled server cache."
}finally{Pop-Location}
