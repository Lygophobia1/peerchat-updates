param([string]$FixtureRoot='F:\PeerChat-isolated-cache-tests')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
 $build=Join-Path $root 'build\tests\large-file-disk'
 New-Item -ItemType Directory -Force -Path $build | Out-Null
 foreach($test in @('transfer_large_preparation_test','attachment_large_crypto_disk_test')) {
  $exe=Join-Path $build ($test+'.exe')
  & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. ('tests/'+$test+'.cpp') -o $exe -lbcrypt -lcrypt32 -lpsapi
  if($LASTEXITCODE){throw "Large-file test build failed: $test"}
  $fixture=Join-Path $FixtureRoot ($test+'-'+[guid]::NewGuid().ToString('N'))
  & $exe $fixture
  if($LASTEXITCODE){throw "Large-file test failed: $test"}
 }
} finally {Pop-Location}