param([Parameter(Mandatory=$true)][string]$ClientDirectory,[switch]$Benchmark)
$ErrorActionPreference='Stop';$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/pipeline-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Directory $out|Out-Null
Push-Location $root
try{
 foreach($name in @('transfer_pipeline','transfer_timing','transfer_budget')){& g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. "tests/${name}_test.cpp" -o "$out/$name.exe" -lbcrypt -lcrypt32;if($LASTEXITCODE){throw "$name build failed"};& "$out/$name.exe";if($LASTEXITCODE){throw "$name failed"}}
 Copy-Item "$ClientDirectory/*" $out -Recurse
 $fx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
 $refs=@('System.dll','System.Core.dll','System.Xaml.dll','WPF/WindowsBase.dll','WPF/PresentationCore.dll','WPF/PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
 & "$fx/csc.exe" /nologo /target:exe /platform:x64 "/reference:$out/PeerChat.Desktop.exe" $refs "/out:$out/tuning-ui.exe" (Join-Path $PSScriptRoot 'transfer_tuning_ui_test.cs')
 if($LASTEXITCODE){throw 'Tuning UI compile failed'};& "$out/tuning-ui.exe";if($LASTEXITCODE){throw 'Tuning UI failed'}
 & "$fx/csc.exe" /nologo /target:exe /platform:x64 /reference:System.Core.dll /reference:System.Web.Extensions.dll "/reference:$out/PeerChat.Desktop.exe" "/out:$out/scheduling.exe" (Join-Path $PSScriptRoot 'managed_transfer_scheduling_test.cs')
 if($LASTEXITCODE){throw 'Packaged pipeline fixture build failed'}
 $prior=$env:PEERCHAT_TEST_TRANSFER_TUNING
 try{foreach($setting in @('32768,1,0','32768,4,0','32768,8,0','65536,1,0','65536,4,0','65536,8,0','65536,16,0')){
  $env:PEERCHAT_TEST_TRANSFER_TUNING=$setting
  if(!$Benchmark -and $setting -ne '65536,8,0'){continue}
  & "$out/scheduling.exe" (Join-Path $out ('profiles-'+$setting.Replace(',','-'))) event
  if($LASTEXITCODE){throw "Production pipeline failed: $setting"}
 }}finally{$env:PEERCHAT_TEST_TRANSFER_TUNING=$prior}
 Write-Output "Evidence: $out"
}finally{Pop-Location}
