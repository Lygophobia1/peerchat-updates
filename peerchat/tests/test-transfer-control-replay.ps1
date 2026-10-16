param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\transfer-control-regressions-'+[Guid]::NewGuid().ToString('N'))
$compiler=if(Get-Command g++ -ErrorAction SilentlyContinue){(Get-Command g++).Source}else{'C:\TDM-GCC-64\bin\g++.exe'}
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try{
    foreach($test in @('transfer_control_replay','transfer_pipeline','transfer_restart')){
        & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -I. "tests\${test}_test.cpp" -o "$out\$test.exe" -lbcrypt -lcrypt32
        if($LASTEXITCODE){throw "$test compilation failed"}
        & "$out\$test.exe" 2>&1 | Tee-Object -FilePath "$out\$test.log"
        if($LASTEXITCODE){throw "$test failed; evidence retained at $out"}
    }
    Write-Host "PASS: transfer control replay, pipeline and protected restart regressions. Evidence: $out"
}finally{Pop-Location}
