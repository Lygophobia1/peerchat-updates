param([string]$AdapterDirectory='C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64',[string]$NativeCache='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dll=(Resolve-Path -LiteralPath (Join-Path $AdapterDirectory 'PeerChat.AudioReceive.dll')).Path
$output=Join-Path $root ('build\tests\audio-receive-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $output|Out-Null
$exe=Join-Path $output 'audio-receive-test.exe'
& g++ -std=c++17 -O2 -Wall -Wextra -static-libgcc -static-libstdc++ ("-I"+(Join-Path $NativeCache 'include')) ("-I"+(Join-Path $root 'audio_receive')) (Join-Path $root 'audio_receive\audio_receive_test.cpp') ("-L"+(Join-Path $NativeCache 'lib')) -lopus '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -lpsapi -o $exe
if($LASTEXITCODE -ne 0){throw 'Standalone receive test compile failed'}
$old=$env:PATH
try{
 $env:PATH="$env:WINDIR\System32;$env:WINDIR"
 & $exe $dll
 if($LASTEXITCODE -ne 0){throw 'Standalone receive proof failed'}
}finally{$env:PATH=$old}
Write-Output "Receive proof test: $exe"
