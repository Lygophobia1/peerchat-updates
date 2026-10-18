$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\legacy-session-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    & dotnet run --project tests/portable/PeerChat.LegacySessionCompatibility.csproj -c Release -- $out
    if($LASTEXITCODE -ne 0){throw 'Legacy session compatibility regression failed'}
}finally{Pop-Location}
