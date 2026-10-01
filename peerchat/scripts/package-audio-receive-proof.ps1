param([Parameter(Mandatory=$true)][string]$DevelopmentDirectory,[string]$AdapterDirectory='C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$metadata=Get-Content -LiteralPath (Join-Path $AdapterDirectory 'audio-receive-build.json') -Raw|ConvertFrom-Json
if($metadata.webrtc_revision -ne '6cf6bcb585b31414aac02377df2bc8fec770d0e8'){throw 'Receive proof revision mismatch'}
$dll=Join-Path $AdapterDirectory 'PeerChat.AudioReceive.dll'
if((Get-FileHash -LiteralPath $dll).Hash -ne $metadata.dll_sha256){throw 'Receive DLL hash mismatch'}
foreach($property in $metadata.source_sha256.psobject.Properties){if((Get-FileHash -LiteralPath (Join-Path $root ('audio_receive\'+$property.Name))).Hash -ne $property.Value){throw "Receive proof source mismatch: $($property.Name)"}}
$closure=Get-Content -LiteralPath (Join-Path $AdapterDirectory 'audio-receive-dependencies.txt')
if($closure|Where-Object {$_ -match '^//(pc|p2p|call|video|modules/video|media/engine)[:/]'} ){throw 'Unexpected calling/transport/video implementation in receive closure'}
$imports=@(& objdump -p $dll|ForEach-Object {if($_ -match 'DLL Name:\s*(\S+)'){$Matches[1].ToUpperInvariant()}})
if($LASTEXITCODE -ne 0){throw 'Receive import inspection failed'}
if($imports.Count -ne 3 -or ($imports|Where-Object {$_ -notin @('KERNEL32.DLL','USER32.DLL','WINMM.DLL')})){throw 'Unexpected receive runtime dependencies'}
& (Join-Path $root 'tests\test-audio-receive.ps1') -AdapterDirectory $AdapterDirectory
if($LASTEXITCODE -ne 0){throw 'Receive proof regression failed'}
$development=(Resolve-Path -LiteralPath $DevelopmentDirectory).Path
if(-not(Test-Path -LiteralPath (Join-Path $development 'Package\PeerChat.Desktop.exe'))){throw 'Full development build must pass before staging proof'}
$destination=Join-Path $development 'Proofs\AudioReceive'
if(Test-Path -LiteralPath $destination){throw 'Refusing to replace an existing proof package'}
New-Item -ItemType Directory -Path $destination -Force|Out-Null
foreach($name in @('PeerChat.AudioReceive.dll','audio-receive-build.json','audio-receive-dependencies.txt')){Copy-Item -LiteralPath (Join-Path $AdapterDirectory $name) -Destination $destination}
Write-Output "PASS: separate receive proof staged at $destination; imports=$($imports -join ', ')"
