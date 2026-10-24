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
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/transfer_cache_reservation_test.cpp -o "$out/reservation.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Cache reservation compile failed'}
 & "$out/reservation.exe"
 if($LASTEXITCODE){throw 'Cache reservation regression failed'}
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/transfer_cache_preparation_test.cpp -o "$out/preparation.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Cache preparation compile failed'}
 & "$out/preparation.exe"
 if($LASTEXITCODE){throw 'Cache preparation regression failed'}
 Write-Output "Evidence: $out. Local purpose and bounded reservation adapters only, no enabled server cache."
}finally{Pop-Location}
