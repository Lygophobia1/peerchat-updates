param([string]$DependencyRoot='C:\Peerchatdep\deps',[string]$ToolRoot='C:\Users\Livel\.codex\worktrees\71be\Create 2D Game\peerchat\build\tools',[string]$OutputRoot='C:\Peerchatdep\build\apm\webrtc-6cf6bcb-presets\x64')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$src=Join-Path $DependencyRoot 'webrtc-complete-src\src'
$revision=(& git -c "safe.directory=$($src.Replace('\','/'))" -C $src rev-parse HEAD).Trim()
if($revision -ne '6cf6bcb585b31414aac02377df2bc8fec770d0e8'){throw "Wrong WebRTC revision: $revision"}
$gn=Join-Path $ToolRoot 'webrtc-gn\gn.exe'
$depot=Join-Path $ToolRoot 'depot_tools'
$python=Join-Path $depot '.cipd_bin\3.11\bin'
$ninja='C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
foreach($required in @($gn,$ninja,(Join-Path $python 'python3.exe'),(Join-Path $OutputRoot 'args.gn'))){if(-not(Test-Path -LiteralPath $required)){throw "Missing validated build prerequisite: $required"}}
$target=Join-Path $src 'peerchat_audio_receive'
New-Item -ItemType Directory -Force -Path $target|Out-Null
Copy-Item -LiteralPath (Join-Path $root 'audio_receive\BUILD.gn'),(Join-Path $root 'audio_receive\peerchat_audio_receive.cpp'),(Join-Path $root 'audio_receive\peerchat_audio_receive.h') -Destination $target -Force
# Register only this target in the ignored pinned checkout; no WebRTC algorithm edits.
$rootBuild=Join-Path $src 'BUILD.gn'
$content=Get-Content -LiteralPath $rootBuild -Raw
if(-not $content.Contains('group("peerchat_audio_receive")')){
 $anchor='import("webrtc.gni")'
 if(-not $content.Contains($anchor)){throw 'Unexpected pinned root BUILD.gn'}
 $addition="`r`ngroup(`"peerchat_audio_receive`") {`r`n  deps = [ `"//peerchat_audio_receive:peerchat_audio_receive`" ]`r`n}`r`n"
 [IO.File]::WriteAllText($rootBuild,$content.Replace($anchor,$anchor+$addition),[Text.UTF8Encoding]::new($false))
}
$oldPath=$env:PATH;$oldPython=$env:PYTHONPATH;$oldToolchain=$env:DEPOT_TOOLS_WIN_TOOLCHAIN
try{
 $env:PATH="$python;$oldPath";$env:PYTHONPATH="$depot;$oldPython";$env:DEPOT_TOOLS_WIN_TOOLCHAIN='0'
 Push-Location $src
 try{
  & $gn gen $OutputRoot
  if($LASTEXITCODE -ne 0){throw 'Receive adapter GN generation failed'}
  $closure=& $gn desc $OutputRoot '//peerchat_audio_receive:peerchat_audio_receive' deps --all
  if($LASTEXITCODE -ne 0){throw 'Receive adapter dependency inspection failed'}
  $closure|Set-Content -LiteralPath (Join-Path $OutputRoot 'audio-receive-dependencies.txt')
  & $ninja -C $OutputRoot peerchat_audio_receive
  if($LASTEXITCODE -ne 0){throw 'Receive adapter build failed'}
 }finally{Pop-Location}
}finally{$env:PATH=$oldPath;$env:PYTHONPATH=$oldPython;$env:DEPOT_TOOLS_WIN_TOOLCHAIN=$oldToolchain}
Get-FileHash -LiteralPath (Join-Path $OutputRoot 'PeerChat.AudioReceive.dll') -Algorithm SHA256
$inputs=@{}
foreach($name in @('peerchat_audio_receive.cpp','peerchat_audio_receive.h','BUILD.gn')){$inputs[$name]=(Get-FileHash -LiteralPath (Join-Path $root "audio_receive\$name") -Algorithm SHA256).Hash}
@{webrtc_revision=$revision;dll_sha256=(Get-FileHash -LiteralPath (Join-Path $OutputRoot 'PeerChat.AudioReceive.dll')).Hash;source_sha256=$inputs;args_sha256=(Get-FileHash -LiteralPath (Join-Path $OutputRoot 'args.gn')).Hash}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $OutputRoot 'audio-receive-build.json')
