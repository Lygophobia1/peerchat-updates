$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/audio-scheduler-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out|Out-Null
Push-Location $root
try{
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/audio_scheduler_test.cpp -lwinmm -o "$out/audio-scheduler.exe"
 if($LASTEXITCODE){throw 'Audio scheduler compile failed'}
 & "$out/audio-scheduler.exe" optimized
 if($LASTEXITCODE){throw 'Audio scheduler regression failed'}
}finally{Pop-Location}
