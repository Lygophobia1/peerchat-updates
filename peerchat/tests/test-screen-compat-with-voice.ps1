param([Parameter(Mandatory=$true)][string]$DevelopmentDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$native='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'
$out=Join-Path $root ('build\tests\screen-compat-voice-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item "$DevelopmentDirectory\ScreenMedia\PeerChat.ScreenMedia.dll","$DevelopmentDirectory\Client\PeerChat.AudioProcessing.dll","$DevelopmentDirectory\Client\PeerChat.AudioReceive.dll" $out
Push-Location $root
try {
    & g++ -DPEERCHAT_TEST_VOICE_FRAMES -DPEERCHAT_TEST_MEDIA_DIRECT -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. ("-I"+(Join-Path $native 'include')) tests/screen_compat_voice_startup_test.cpp voice_media.cpp voice_receive_engine.cpp audio.cpp ("-L"+(Join-Path $native 'lib')) -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -lwinmm -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -luser32 -o "$out\compat-voice.exe"
    if($LASTEXITCODE){throw 'Compatibility with voice test compile failed'}
    & "$out\compat-voice.exe"
    if($LASTEXITCODE){throw 'Compatibility with voice startup failed'}
}finally{Pop-Location}
