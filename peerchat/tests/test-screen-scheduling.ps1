param([Parameter(Mandatory=$true)][string]$AdapterDirectory,[int[]]$PollMilliseconds=@(20,1),[int[]]$ViewerCounts=@(1,3))
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$native='C:\Peerchatdep\native\mingw-x64-libdatachannel-c6696d1-opus-ddbe483-mbedtls-068ff08'
$out=Join-Path $root ('build\tests\screen-scheduling-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item (Join-Path $AdapterDirectory 'PeerChat.ScreenMedia.dll') $out
Push-Location $root
try {
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. tests/screen_capture_cadence_test.cpp -o "$out\cadence.exe"
 if($LASTEXITCODE){throw 'Capture cadence test compile failed'}
 & "$out\cadence.exe"
 if($LASTEXITCODE){throw 'Capture cadence regression failed'}
 & g++ -DPEERCHAT_TEST_MEDIA_DIRECT -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. ("-I"+(Join-Path $native 'include')) tests/screen_scheduling_benchmark.cpp ("-L"+(Join-Path $native 'lib')) -ldatachannel -lsrtp2 -ljuice -lusrsctp -lmbedtls -lmbedx509 -lmbedcrypto -leverest -lp256m '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -lws2_32 -lsecur32 -lcrypt32 -lbcrypt -lncrypt -liphlpapi -luser32 -o "$out\benchmark.exe"
 if($LASTEXITCODE){throw 'Screen scheduling benchmark compile failed'}
 foreach($viewers in $ViewerCounts){foreach($poll in $PollMilliseconds){
  & "$out\benchmark.exe" $viewers $poll 2>&1 | Tee-Object -FilePath "$out\viewers-$viewers-poll-$poll.txt"
  if($LASTEXITCODE){throw 'Screen scheduling benchmark failed'}
 }}
 Write-Output "Evidence: $out"
}finally{Pop-Location}
