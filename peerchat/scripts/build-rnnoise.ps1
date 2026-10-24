param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$source=Join-Path $root 'third_party/rnnoise'
$pin=Get-Content (Join-Path $source 'PINNED.json') -Raw | ConvertFrom-Json
foreach($file in $pin.files.PSObject.Properties){if((Get-FileHash (Join-Path $source $file.Name) -Algorithm SHA256).Hash -ne $file.Value){throw "RNNoise pinned source mismatch: $($file.Name)"}}
New-Item -ItemType Directory -Force -Path $OutputDirectory|Out-Null
$files=@('denoise','rnn','rnn_data','pitch','kiss_fft','celt_lpc')|ForEach-Object{Join-Path $source "src/$_.c"}
& gcc -std=gnu99 -include "$root/rnnoise_symbols.h" -O2 -shared -DRNNOISE_BUILD -DDLL_EXPORT -I "$source/include" $files -static-libgcc '-Wl,-Bstatic' -lssp '-Wl,-Bdynamic' -o (Join-Path $OutputDirectory 'PeerChat.RNNoise.dll')
if($LASTEXITCODE -ne 0){throw 'RNNoise build failed'}
New-Item -ItemType Directory -Force -Path (Join-Path $OutputDirectory 'licenses')|Out-Null
Copy-Item "$source/COPYING" (Join-Path $OutputDirectory 'licenses/RNNoise-LICENSE.txt') -Force
