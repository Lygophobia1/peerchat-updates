param([Parameter(Mandatory=$true)][string]$FixtureRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/native-cache-body-'+[guid]::NewGuid().ToString('N'))
$fixture=Join-Path $FixtureRoot ('native-body-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try{
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/group_attachment_body_test.cpp group_message_envelope.cpp group.cpp storage.cpp -o "$out/body.exe" -lbcrypt -lcrypt32 -lws2_32
 if($LASTEXITCODE){throw 'Native body fixture compile failed'}
 & dotnet build tests/portable/PeerChat.CacheRuntimeTests.csproj --no-restore -v quiet -clp:ErrorsOnly
 if($LASTEXITCODE){throw 'Managed fixture compile failed'}
 & "$out/body.exe" "$fixture/profiles"
 if($LASTEXITCODE){throw 'Native signed group body fixture failed'}
 & dotnet tests/portable/bin/Debug/net10.0/PeerChat.CacheRuntimeTests.dll --native-object "$fixture/profiles/cache-fixture" "$fixture/server"
 if($LASTEXITCODE){throw 'Native envelope/managed cache interoperability failed'}
 & "$out/body.exe" "$fixture/profiles" --verify-cached
 if($LASTEXITCODE){throw 'Protected native recipient verification failed'}
 Write-Output "Evidence: $out; isolated storage/codec fixtures: $fixture. TLS/roster admission is covered separately; not a packaged-client group delivery claim."
}finally{Pop-Location}
