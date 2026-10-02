param([string]$AdapterDirectory='C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64',[string]$NativeCache='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dll=(Resolve-Path -LiteralPath (Join-Path $AdapterDirectory 'PeerChat.AudioReceive.dll')).Path
$output=Join-Path $root ('build\tests\voice-receive-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output|Out-Null
$exe=Join-Path $output 'voice-receive-test.exe'
Copy-Item -LiteralPath 'C:\Peerchatdep\artifacts\audio-processing\webrtc-6cf6bcb-voice-presets\x64\PeerChat.AudioProcessing.dll' -Destination $output
& g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ ("-I"+(Join-Path $NativeCache 'include')) (Join-Path $root 'tests\voice_receive_engine_test.cpp') (Join-Path $root 'voice_receive_engine.cpp') ("-L"+(Join-Path $NativeCache 'lib')) -lopus '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o $exe
if($LASTEXITCODE -ne 0){throw 'VoiceReceiveEngine test compile failed'}
$previousPath=$env:PATH
try{
 $env:PATH="$env:WINDIR\System32;$env:WINDIR"
 & $exe $dll
 if($LASTEXITCODE -ne 0){throw 'VoiceReceiveEngine test failed'}
}finally{$env:PATH=$previousPath}
