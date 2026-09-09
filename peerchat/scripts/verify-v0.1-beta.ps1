$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$releaseRoot = Join-Path $projectRoot 'releases\v0.1-beta'
$expected = @{
    'Client\peerchat-client.ico' = 'E5013384191686B88086CC229F28580AC1765369A0F20320DA21096846B62658'
    'Client\PeerChat.Core.dll' = 'B3A379ED960F84885769287DEB8B24FF072C410B2968993FA6B336EF8529E932'
    'Client\PeerChat.Desktop.exe' = '2D8E3208DFE8AA140026A64D46B30DFAAE99E89DF9749F601560A910845FB8CB'
    'Client\PeerChat.Desktop.exe.config' = '4107D0F8D433BE0AC73FB39B0546D33A5AD43975516F5C43A33FF62B8B24ACDB'
    'Server\peerchat-server.ico' = '0AB956D91F9B86C33E3B6EC8E7E8862F073AC95E1FBC69A7F6A3440B7802CE76'
    'Server\PeerChat.Server.Admin.exe' = '2A7E17D43383943639476AABEFCEE17EEA3F0A0B4406F153D76C5E8999578C4E'
    'Server\PeerChat.Server.exe' = '57A5523C2C277AD62DBEB1DBBE113144033C1DC5254A2A02AB4D1422C8C58CA4'
}
foreach($relativePath in $expected.Keys){
    $path = Join-Path $releaseRoot $relativePath
    if(!(Test-Path -LiteralPath $path)){throw "Frozen release file is missing: $relativePath"}
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
    if($actual -ne $expected[$relativePath]){throw "Frozen release hash mismatch: $relativePath"}
}
Write-Host 'PASS: frozen PeerChat v0.1-beta hashes match the known-good manifest.'
