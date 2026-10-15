param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/cache-interop-'+[guid]::NewGuid().ToString('N'))
$fixture=Join-Path $FixtureRoot ('interop-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
New-Item -ItemType Directory -Path $fixture|Out-Null
Push-Location $root
try{
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/group_attachment_staging_test.cpp group.cpp -o "$out/native.exe" -lbcrypt -lcrypt32
 if($LASTEXITCODE){throw 'Native interop fixture compile failed'}
 & dotnet build tests/portable/PeerChat.CacheRuntimeTests.csproj --no-restore -v quiet -clp:ErrorsOnly
 if($LASTEXITCODE){throw 'Managed interop fixture compile failed'}
 & "$out/native.exe" "$fixture/profile" --seed
 if($LASTEXITCODE){throw 'Native encrypted fixture creation failed'}
 & dotnet tests/portable/bin/Debug/net10.0/PeerChat.CacheRuntimeTests.dll --native-object "$fixture/profile" "$fixture/server"
 if($LASTEXITCODE){throw 'Managed signature/cache restart interop failed'}
 & "$out/native.exe" "$fixture/profile" --verify
 if($LASTEXITCODE){throw 'Native plaintext verification after managed cache failed'}
 Write-Output "Evidence: $out; isolated profile/cache fixtures: $fixture. Storage/codec interop, not physical or enabled group delivery."
}finally{Pop-Location}
