param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\managed-transfer-scheduling-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Copy-Item (Join-Path $ClientDirectory '*') $out -Recurse
$fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
& "$fx\csc.exe" /nologo /target:exe /platform:x64 /reference:System.Core.dll /reference:System.Web.Extensions.dll "/reference:$out\PeerChat.Desktop.exe" "/out:$out\scheduling.exe" "$PSScriptRoot\managed_transfer_scheduling_test.cs"
if($LASTEXITCODE){throw 'Managed scheduling fixture compile failed'}
foreach($mode in @('poll','event')){
    & "$out\scheduling.exe" (Join-Path $out "profiles-$mode") $mode
    if($LASTEXITCODE){throw "Managed scheduling fixture failed: $mode"}
}
Write-Output "Evidence: $out"
