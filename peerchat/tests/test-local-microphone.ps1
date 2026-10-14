param([Parameter(Mandatory=$true)][string]$ClientDirectory)
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/microphone-'+[Guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path $out|Out-Null
Copy-Item (Join-Path $ClientDirectory 'PeerChat.AudioProcessing.dll') $out
Copy-Item (Join-Path $ClientDirectory '*') $out
Push-Location $root
try{foreach($name in @('local_microphone_test','voice_processing_comparison')){
 & g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. "tests/$name.cpp" -lwinmm -o "$out/$name.exe"
 if($LASTEXITCODE){throw "$name compile failed"};& "$out/$name.exe";if($LASTEXITCODE){throw "$name failed"}
}
 $fx=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
 $csc='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\Roslyn\csc.exe'
 $references=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
 & $csc /nologo /target:exe /platform:x64 "/out:$out/local_microphone_ui_test.exe" "/reference:$out/PeerChat.Desktop.exe" $references tests/local_microphone_ui_test.cs
 if($LASTEXITCODE){throw 'Local microphone UI compilation failed'}
 & "$out/local_microphone_ui_test.exe";if($LASTEXITCODE){throw 'Local microphone UI test failed'}
}finally{Pop-Location}
