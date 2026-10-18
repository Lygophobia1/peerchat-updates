param([string]$FixtureDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=if($FixtureDirectory){[IO.Path]::GetFullPath($FixtureDirectory)}else{Join-Path $root ('build\tests\route-demand-hint-'+[Guid]::NewGuid().ToString('N'))}
New-Item -ItemType Directory -Force -Path $out | Out-Null
$compiler=if(Get-Command g++ -ErrorAction SilentlyContinue){(Get-Command g++).Source}else{'C:\TDM-GCC-64\bin\g++.exe'}
Push-Location $root
try {
    & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/route_demand_hint_test.cpp group.cpp group_message_envelope.cpp -o (Join-Path $out 'route-demand-hint-test.exe') -lbcrypt -lcrypt32 -lncrypt
    if($LASTEXITCODE){throw 'Route-demand native regression compilation failed'}
    & (Join-Path $out 'route-demand-hint-test.exe') $out
    if($LASTEXITCODE){throw 'Route-demand native regression failed'}
}finally{Pop-Location}
