param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$OutputDirectory){$OutputDirectory=Join-Path $root ('build/tests/rnnoise-'+[guid]::NewGuid().ToString('N'))}
& (Join-Path $root 'scripts/build-rnnoise.ps1') -OutputDirectory $OutputDirectory
Copy-Item 'C:/Peerchatdep/artifacts/audio-processing/webrtc-6cf6bcb-voice-presets/x64/PeerChat.AudioProcessing.dll' $OutputDirectory
& g++ -std=c++17 -O2 -Wall -Wextra -Werror "-I$root" (Join-Path $root 'tests/rnnoise_capture_test.cpp') -static-libgcc -static-libstdc++ '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o (Join-Path $OutputDirectory 'rnnoise-test.exe')
if($LASTEXITCODE){throw 'RNNoise test compile failed'}
& (Join-Path $OutputDirectory 'rnnoise-test.exe')
if($LASTEXITCODE){throw 'RNNoise capture regression failed'}
Write-Output "Evidence: $OutputDirectory"
