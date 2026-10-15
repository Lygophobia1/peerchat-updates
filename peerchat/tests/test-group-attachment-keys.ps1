param([string]$FixtureRoot)
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/attachment-keys-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $out|Out-Null
& (Join-Path $root 'scripts/build-go-hpke.ps1') -OutputDirectory $out
Push-Location $root
try{
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -Wno-cast-function-type -static-libgcc -static-libstdc++ -I. tests/group_attachment_key_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp group_message_envelope.cpp -o "$out/keys.exe" -lbcrypt -lcrypt32 -lncrypt -lws2_32
 if($LASTEXITCODE){throw 'Attachment key compile failed'}
 if($FixtureRoot){
  & dotnet build tests/portable/PeerChat.CacheRuntimeTests.csproj --no-restore -v quiet -clp:ErrorsOnly
  if($LASTEXITCODE){throw 'Managed cache fixture compile failed'}
  $fixture=Join-Path $FixtureRoot ('native-keys-'+[guid]::NewGuid().ToString('N'))
  & "$out/keys.exe" $fixture (Resolve-Path 'tests/portable/bin/Debug/net10.0/PeerChat.CacheRuntimeTests.dll').Path
 }else{ & "$out/keys.exe" "$out/fixtures" }
 if($LASTEXITCODE){throw 'Attachment key regression failed'}
 Write-Output "Evidence: $out. Actual endpoint HPKE/registry fixtures, not physical or packaged-client fanout acceptance."
}finally{Pop-Location}
