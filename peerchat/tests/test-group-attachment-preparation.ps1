param()
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/group-preparation-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try{
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/group_attachment_preparation_test.cpp -o "$out/preparation.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Preparation compile failed'}
 & "$out/preparation.exe"
 if($LASTEXITCODE){throw 'Preparation regression failed'}
 Write-Output "Evidence: $out. Bounded codec steps only; production cache remains disabled."
}finally{Pop-Location}
