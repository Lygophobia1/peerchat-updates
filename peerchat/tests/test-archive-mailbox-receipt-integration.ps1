param([string]$FixtureRoot)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if([string]::IsNullOrWhiteSpace($FixtureRoot)){$FixtureRoot=Join-Path $env:TEMP ('pc-archive-mailbox-'+[Guid]::NewGuid().ToString('N'))}
$out=[System.IO.Path]::GetFullPath($FixtureRoot)
if([System.IO.Path]::GetPathRoot($out) -eq $out){throw 'An isolated fixture directory is required.'}
New-Item -ItemType Directory -Force -Path $out | Out-Null
& (Join-Path $root 'scripts\build-go-hpke.ps1') -OutputDirectory $out
$compiler=if(Get-Command g++ -ErrorAction SilentlyContinue){(Get-Command g++).Source}else{'C:\TDM-GCC-64\bin\g++.exe'}
Push-Location $root
try {
    & $compiler -std=c++17 -D_WIN32_WINNT=0x0601 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/archive_mailbox_receipt_integration_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp -o "$out\archive-mailbox-receipt-test.exe" -lncrypt -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Archive/mailbox receipt integration build failed'}
    & "$out\archive-mailbox-receipt-test.exe" "$out\profiles"
    if($LASTEXITCODE -ne 0){throw 'Archive/mailbox receipt integration regression failed'}
    Write-Host "Evidence: $out"
} finally {Pop-Location}
