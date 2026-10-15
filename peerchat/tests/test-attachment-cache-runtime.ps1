param([string]$FixtureRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/cache-runtime-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try{
 $fixtures=if($FixtureRoot){Join-Path $FixtureRoot ([Guid]::NewGuid().ToString('N'))}else{Join-Path $out 'fixtures'}
 & 'C:\Program Files\dotnet\dotnet.exe' run --project .\tests\portable\PeerChat.CacheRuntimeTests.csproj -c Release -- $fixtures
 if($LASTEXITCODE){throw 'Cache runtime regressions failed'}
 Write-Output "Evidence: $out; fixtures: $fixtures. Isolated ciphertext-cache tests only; no live Pi access or deployment."
}finally{Pop-Location}
