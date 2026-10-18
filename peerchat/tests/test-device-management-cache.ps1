param([string]$FixtureRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/device-management-cache-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
 $fixture=if($FixtureRoot){Join-Path $FixtureRoot ([Guid]::NewGuid().ToString('N'))}else{Join-Path $out 'state'}
 & 'C:\Program Files\dotnet\dotnet.exe' run --project .\tests\portable\PeerChat.DeviceManagementCacheTests.csproj -c Release -- $fixture
 if($LASTEXITCODE){throw 'Device management/cache regression failed'}
 Write-Output "Evidence: $out"
}finally{Pop-Location}
