param([string]$NativeCache='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build\tests\voice-topology-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $out|Out-Null
$exe=Join-Path $out 'voice-topology.exe'
& g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ ("-I"+(Join-Path $NativeCache 'include')) (Join-Path $root 'tests\voice_topology_test.cpp') (Join-Path $root 'voice_media_topology.cpp') ("-L"+(Join-Path $NativeCache 'lib')) -ldatachannel -lopus -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -lwinmm -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -o $exe
if($LASTEXITCODE -ne 0){throw 'Topology compile failed'}
$saved=$env:PATH
try{$env:PATH="$env:WINDIR\System32;$env:WINDIR";& $exe;if($LASTEXITCODE -ne 0){throw 'Topology regression failed'}}finally{$env:PATH=$saved}
