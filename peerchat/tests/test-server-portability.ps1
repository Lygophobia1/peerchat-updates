param([string]$DotNet='C:\Program Files\dotnet\dotnet.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\server-portability-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
    if((& $DotNet --version) -ne '10.0.401'){throw 'Pinned .NET SDK 10.0.401 required'}
    $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $sources=Get-ChildItem -LiteralPath server -Filter '*.cs' -File |
        Where-Object {$_.Name -notin @('PeerChat.Server.Admin.cs','DiscoveryIntegrationTest.cs')} |
        ForEach-Object {$_.FullName}
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 /main:ServerJsonFixtures "/out:$out\framework-fixtures.exe" "/reference:$fx\System.Web.Extensions.dll" .\shared\VersionInfo.cs $sources .\tests\group_roster_proof_test.cs .\tests\server_json_fixtures.cs
    if($LASTEXITCODE){throw 'Framework fixture compile'}
    & "$out\framework-fixtures.exe" $out
    if($LASTEXITCODE){throw 'Framework fixture generation'}
    $producer=Get-ChildItem -Path 'build\tests\mailbox-protocol-*\mailbox-protocol-test.exe' |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if(!$producer){throw 'Run tests/test-mailbox-protocol.ps1 first to build the native canonical fixture producer'}
    & $producer.FullName "$out\mailbox"
    if($LASTEXITCODE){throw 'Native canonical mailbox fixtures'}
    & $DotNet run --project tests/portable/PeerChat.Server.PortabilityTests.csproj -c Release -- $out "$out\mailbox"
    if($LASTEXITCODE){throw 'Modern portable server regressions'}
    & "$out\framework-fixtures.exe" $out --verify
    if($LASTEXITCODE){throw 'Modern JSON -> Framework compatibility'}
    Write-Host "PASS: portable server parity evidence: $out"
}finally{Pop-Location}
