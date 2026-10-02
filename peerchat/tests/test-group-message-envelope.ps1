param([switch]$IncludeStage1A)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\group-envelope-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
$compiler=if(Get-Command g++ -ErrorAction SilentlyContinue){(Get-Command g++).Source}else{'C:\TDM-GCC-64\bin\g++.exe'}
$savedPath=$env:PATH
Push-Location $root
try{
    & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ tests\group_message_envelope_test.cpp group_message_envelope.cpp group.cpp storage.cpp -o "$out\group-message-envelope-test.exe" -lbcrypt -lcrypt32
    if($LASTEXITCODE -ne 0){throw "Stage 1B compilation failed ($LASTEXITCODE)."}
    # The real DPAPI profile tests require the normal interactive Windows user.
    $env:PATH="$env:WINDIR\System32;$env:WINDIR"
    & "$out\group-message-envelope-test.exe" "$out\profiles"
    if($LASTEXITCODE -ne 0){throw "Stage 1B regression failed ($LASTEXITCODE)."}
    $env:PATH=$savedPath
    if($IncludeStage1A){
        $native=if($env:PEERCHAT_NATIVE_DEPENDENCY_ROOT){$env:PEERCHAT_NATIVE_DEPENDENCY_ROOT}else{'C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'}
        foreach($test in @('group_control_scope_test','group_control_handoff_test','group_control_three_member_test')){
            & $compiler -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. ("-I"+(Join-Path $native 'include')) "tests\$test.cpp" node.cpp tls.cpp storage.cpp group.cpp group_message_envelope.cpp voice_media.cpp voice_receive_engine.cpp audio.cpp ("-L"+(Join-Path $native 'lib')) -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o "$out\$test.exe" -lwinmm -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi
            if($LASTEXITCODE -ne 0){throw "$test compilation failed ($LASTEXITCODE)."}
            $env:PATH="$env:WINDIR\System32;$env:WINDIR"
            & "$out\$test.exe" "$out\$test-profiles"
            if($LASTEXITCODE -ne 0){throw "$test failed ($LASTEXITCODE)."}
            $env:PATH=$savedPath
        }
    }
    Write-Host "PASS: focused group-envelope regressions: $out"
}finally{$env:PATH=$savedPath;Pop-Location}
