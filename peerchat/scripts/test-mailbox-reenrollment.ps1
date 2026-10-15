param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $OutputDirectory){$OutputDirectory=Join-Path $root ('build/tests/mailbox-reenrollment-'+[Guid]::NewGuid().ToString('N'))}
$out=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $out | Out-Null
& (Join-Path $PSScriptRoot 'build-go-hpke.ps1') -OutputDirectory $out
Push-Location $root
try {
    $exe=Join-Path $out 'mailbox-reenrollment-test.exe'
    & 'C:/TDM-GCC-64/bin/g++.exe' -I. -std=c++17 -D_WIN32_WINNT=0x0601 -Wno-cast-function-type -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests/mailbox_reenrollment_test.cpp platform/go_hpke_provider.cpp group.cpp -o $exe -lncrypt -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw 'Mailbox reenrollment build failed'}
    & $exe (Join-Path $out 'fixtures')
    if($LASTEXITCODE -ne 0){throw 'Mailbox reenrollment test failed'}
    Write-Output ('Fixtures: '+(Join-Path $out 'fixtures'))
}finally{Pop-Location}
