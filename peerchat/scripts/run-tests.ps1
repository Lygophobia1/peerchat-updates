$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    if(-not (Test-Path 'build\native\install\lib\libdatachannel.a')) { & "$PSScriptRoot\build-voice-dependencies.ps1" }
    $media=@('-Ibuild\native\install\include','voice_media.cpp','audio.cpp','-Lbuild\native\install\lib','-ldatachannel','-lopus','-lsrtp2','-ljuice','-lusrsctp','-lmbedtls','-lmbedx509','-lmbedcrypto','-leverest','-lp256m','-lssp','-lwinmm','-liphlpapi')
    $outputDirectory='build\tests\core'
    New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
    & g++ -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\storage_test.cpp group.cpp -o "$outputDirectory\storage-test.exe" -lcrypt32 -lbcrypt
    if ($LASTEXITCODE -ne 0) { throw 'Storage test build failed' }
    & g++ -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\tls_test.cpp tls.cpp storage.cpp group.cpp -o "$outputDirectory\tls-test.exe" -lcrypt32 -lbcrypt -lncrypt -lsecur32
    if ($LASTEXITCODE -ne 0) { throw 'TLS test build failed' }
    & g++ -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\node_test.cpp node.cpp tls.cpp storage.cpp group.cpp $media -o "$outputDirectory\node-test.exe" -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt
    if ($LASTEXITCODE -ne 0) { throw 'Node test build failed' }
    & g++ -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\peer_harness.cpp node.cpp tls.cpp storage.cpp group.cpp $media -o "$outputDirectory\peer-harness.exe" -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt
    if ($LASTEXITCODE -ne 0) { throw 'Process harness build failed' }
    & g++ -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\group_test.cpp group.cpp storage.cpp -o "$outputDirectory\group-test.exe" -lcrypt32 -lbcrypt
    if ($LASTEXITCODE -ne 0) { throw 'Group ledger test build failed' }
    & g++ -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\group_node_test.cpp node.cpp tls.cpp storage.cpp group.cpp $media -o "$outputDirectory\group-node-test.exe" -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt
    if ($LASTEXITCODE -ne 0) { throw 'Group network test build failed' }
    & g++ -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\voice_call_test.cpp node.cpp tls.cpp storage.cpp group.cpp $media -o "$outputDirectory\voice-call-test.exe" -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt
    if ($LASTEXITCODE -ne 0) { throw 'Voice call-state test build failed' }
    & g++ -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\audio_test.cpp -o "$outputDirectory\audio-test.exe" -lwinmm
    if ($LASTEXITCODE -ne 0) { throw 'Native audio queue test build failed' }
    & g++ -Ibuild\native\install\include -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\opus_voice_test.cpp -Lbuild\native\install\lib -lopus -lssp -o "$outputDirectory\opus-voice-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Opus voice test build failed' }
    & g++ -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror tests\share_id_test.cpp share_id.cpp -o "$outputDirectory\share-id-test.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Share ID test build failed' }
    $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
    $systemReference='/reference:'+(Join-Path $framework 'System.dll')
    & "$framework\csc.exe" /nologo /target:exe "/out:$outputDirectory\firewall-rule-test.exe" $systemReference .\desktop\FirewallRules.cs .\tests\firewall_rule_test.cs
    if ($LASTEXITCODE -ne 0) { throw 'Firewall rule test build failed' }
    & ".\$outputDirectory\share-id-test.exe"; if ($LASTEXITCODE -ne 0) { throw 'Share ID test failed' }
    & ".\$outputDirectory\firewall-rule-test.exe"; if ($LASTEXITCODE -ne 0) { throw 'Firewall rule test failed' }
    & ".\$outputDirectory\storage-test.exe"; if ($LASTEXITCODE -ne 0) { throw 'Storage test failed' }
    & ".\$outputDirectory\group-test.exe" (Join-Path $PWD ('build\tests\runtime\group-'+[Guid]::NewGuid().ToString('N'))); if ($LASTEXITCODE -ne 0) { throw 'Group ledger test failed' }
    & ".\$outputDirectory\group-node-test.exe"; if ($LASTEXITCODE -ne 0) { throw 'Group network test failed' }
    & ".\$outputDirectory\voice-call-test.exe"; if ($LASTEXITCODE -ne 0) { throw 'Voice call-state test failed' }
    & ".\$outputDirectory\audio-test.exe"; if ($LASTEXITCODE -ne 0) { throw 'Native audio queue test failed' }
    & ".\$outputDirectory\opus-voice-test.exe"; if ($LASTEXITCODE -ne 0) { throw 'Opus voice test failed' }
    & ".\$outputDirectory\tls-test.exe"; if ($LASTEXITCODE -ne 0) { throw 'TLS test failed' }
    & ".\$outputDirectory\node-test.exe"; if ($LASTEXITCODE -ne 0) { throw 'Node test failed' }
    & python .\tests\test_six_processes.py; if ($LASTEXITCODE -ne 0) { throw 'Six-process test failed' }
    Write-Host 'All PeerChat regression tests passed.'
} finally { Pop-Location }
