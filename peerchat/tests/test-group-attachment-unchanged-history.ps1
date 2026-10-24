param([string]$FixtureRoot='F:\peerchat-cache-test-fixtures')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$run=Join-Path $FixtureRoot ('group-unchanged-history-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run|Out-Null
Push-Location $root
try {
    & g++ -std=c++17 -O1 -Wall -Wextra -Werror -Wno-cast-function-type -Wno-misleading-indentation -static-libgcc -static-libstdc++ -I. tests/group_attachment_unchanged_history_test.cpp group.cpp -o "$run/history.exe" -lbcrypt -lcrypt32 -lncrypt -lws2_32
    if($LASTEXITCODE){throw 'Unchanged group history fixture compilation failed'}
    & "$run/history.exe" "$run/profiles" 2>&1 | Tee-Object -FilePath "$run/results.log"
    if($LASTEXITCODE){throw 'Unchanged group history regression failed'}
    Write-Output "PASS isolated group history unchanged-record and protected CAS regressions. Evidence: $run"
} finally {Pop-Location}
