$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    $out=Join-Path 'build\tests' ('friend-publication-'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    & "$fx\csc.exe" /nologo /target:exe /platform:x64 "/out:$out\test.exe" "/reference:$fx\System.Web.Extensions.dll" .\desktop\DeviceNameMetadata.cs .\desktop\FriendConversations.cs .\tests\authenticated_friend_publication_test.cs
    if($LASTEXITCODE -ne 0){throw 'Authenticated friend publication fixture compile failed'}
    & (Join-Path $out 'test.exe')
    if($LASTEXITCODE -ne 0){throw 'Authenticated friend publication fixture failed'}
} finally { Pop-Location }
