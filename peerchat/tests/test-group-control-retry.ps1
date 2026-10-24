$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/group-control-retry-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
 & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/group_control_retry_test.cpp -o "$out/group-control-retry.exe"
 if($LASTEXITCODE){throw 'Scoped group retry fixture compile failed'}
 & "$out/group-control-retry.exe"
 if($LASTEXITCODE){throw 'Scoped group retry fixture failed'}
 Write-Output "Evidence: $out"
}finally{Pop-Location}
