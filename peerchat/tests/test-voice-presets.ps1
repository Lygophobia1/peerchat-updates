param(
    [string]$OutputDirectory='build\tests\voice-presets',
    [string]$NativeDependencyRoot='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08',
    [string]$AudioProcessingDll='C:\Peerchatdep\artifacts\audio-processing\webrtc-6cf6bcb-voice-presets\x64\PeerChat.AudioProcessing.dll'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    if(-not(Test-Path -LiteralPath $NativeDependencyRoot)){throw "Shared native dependency install missing: $NativeDependencyRoot"}
    if(-not(Test-Path -LiteralPath $AudioProcessingDll)){throw "Preset-enabled APM artifact missing: $AudioProcessingDll"}
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    & .\tests\test-voice-settings.ps1 -OutputDirectory (Join-Path $OutputDirectory 'managed')
    if($LASTEXITCODE -ne 0){throw 'Voice preference settings regression failed.'}
    Copy-Item -LiteralPath $AudioProcessingDll -Destination $OutputDirectory -Force
    & .\scripts\build-rnnoise.ps1 -OutputDirectory $OutputDirectory
    # TDM's default stack protector imports this runtime even with static libgcc.
    Copy-Item -LiteralPath (Join-Path (Split-Path (Get-Command g++).Source -Parent) 'libssp_64-0.dll') -Destination $OutputDirectory -Force
    Copy-Item -LiteralPath 'C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64\PeerChat.AudioReceive.dll' -Destination $OutputDirectory -Force
    & g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -I. .\tests\voice_apm_capture_test.cpp -o (Join-Path $OutputDirectory 'voice-apm-capture-test.exe')
    if($LASTEXITCODE -ne 0){throw 'Voice APM capture test build failed.'}
    & (Join-Path $OutputDirectory 'voice-apm-capture-test.exe')
    if($LASTEXITCODE -ne 0){throw 'Voice APM capture preset/generation test failed.'}
    $include='-I'+(Join-Path $NativeDependencyRoot 'include')
    $library='-L'+(Join-Path $NativeDependencyRoot 'lib')
    & g++ -DPEERCHAT_TEST_VOICE_FRAMES -DPEERCHAT_TEST_MEDIA_DIRECT $include -I. -static-libgcc -static-libstdc++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror .\tests\voice_media_transport_test.cpp .\voice_media.cpp voice_receive_engine.cpp .\audio.cpp $library -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o (Join-Path $OutputDirectory 'voice-media-transport-test.exe') -lwinmm -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi
    if($LASTEXITCODE -ne 0){throw 'Voice media transport preset test build failed.'}
    & (Join-Path $OutputDirectory 'voice-media-transport-test.exe')
    if($LASTEXITCODE -ne 0){throw 'Voice media generation, preset, or framing regression failed.'}
& g++ -DPEERCHAT_TEST_VOICE_FRAMES -DPEERCHAT_TEST_MEDIA_DIRECT $include -I. -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror .\tests\voice_receive_lifetime_test.cpp .\voice_media.cpp voice_receive_engine.cpp .\audio.cpp $library -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m -lssp -o (Join-Path $OutputDirectory 'voice-receive-lifetime-test.exe') -lwinmm -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi
    if($LASTEXITCODE -ne 0){throw 'Voice receive lifecycle test build failed.'}
    $originalPath=$env:PATH
    try {
        $env:PATH="$env:WINDIR\System32;$env:WINDIR"
        & (Join-Path $OutputDirectory 'voice-receive-lifetime-test.exe')
        if($LASTEXITCODE -ne 0){throw 'Voice receive lifecycle test failed.'}
        $missingReceiver=Join-Path $OutputDirectory ('missing-receiver-'+[guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $missingReceiver|Out-Null
        foreach($file in @('voice-receive-lifetime-test.exe','PeerChat.AudioProcessing.dll','libssp_64-0.dll')){
            Copy-Item -LiteralPath (Join-Path $OutputDirectory $file) -Destination $missingReceiver
        }
        & (Join-Path $missingReceiver 'voice-receive-lifetime-test.exe') '--missing-receiver'
        if($LASTEXITCODE -ne 0){throw 'Voice receive audio-scoped failure regression failed.'}
    } finally {$env:PATH=$originalPath}
    Write-Output 'PASS: profile/APM presets, bidirectional NetEq receive, 100 receive lifetimes, and unchanged 20 ms Opus framing.'
} finally {Pop-Location}
