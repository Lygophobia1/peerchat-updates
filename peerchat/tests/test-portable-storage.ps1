param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\portable-storage-'+[Guid]::NewGuid().ToString('N'))
$compiler=if(Get-Command g++ -ErrorAction SilentlyContinue){(Get-Command g++).Source}else{'C:\TDM-GCC-64\bin\g++.exe'}
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try {
    foreach($test in @('storage_test','portable_windows_compat_test','opaque_identity_test','storage_backend_test','storage_sql_integration_test','storage_record_provider_test','opaque_profile_storage_test')){
        & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. "tests\$test.cpp" group.cpp -o "$out\$test.exe" -lbcrypt -lcrypt32
        if($LASTEXITCODE -ne 0){throw "$test compilation failed ($LASTEXITCODE)."}
        & "$out\$test.exe"
        if($LASTEXITCODE -ne 0){throw "$test failed ($LASTEXITCODE)."}
    }
    & "$PSScriptRoot\test-group-message-envelope.ps1"
    Write-Host "PASS: portable Storage host regression set: $out"
} finally {Pop-Location}
