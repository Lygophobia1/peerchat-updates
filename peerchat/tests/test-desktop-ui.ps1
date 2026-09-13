param([string]$ApplicationDirectory='releases\v0.3-beta\Client',[string]$OutputDirectory='build\tests\packaged-ui')
$ErrorActionPreference='Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
 New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
 Copy-Item "$ApplicationDirectory\PeerChat.Desktop.exe","$ApplicationDirectory\PeerChat.Core.dll","$ApplicationDirectory\PeerChat.Desktop.exe.config","$ApplicationDirectory\peerchat-client.ico","$ApplicationDirectory\PeerChat.Updater.Core.dll","$ApplicationDirectory\PeerChat.Updater.exe","$ApplicationDirectory\PeerChat.Updater.exe.config","$ApplicationDirectory\update-feeds.json" $OutputDirectory -Force
 $framework=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
 $references=@('System.dll','System.Core.dll','System.Xaml.dll','WPF\WindowsBase.dll','WPF\PresentationCore.dll','WPF\PresentationFramework.dll') | ForEach-Object { '/reference:'+(Join-Path $framework $_) }
 & "$framework\csc.exe" /nologo /target:exe /platform:x64 "/out:$OutputDirectory\UiSmoke.exe" "/reference:$OutputDirectory\PeerChat.Desktop.exe" $references .\desktop\UiSmoke.cs
 if($LASTEXITCODE -ne 0){throw 'UI test build failed'}
 $profile=Get-ChildItem build\tests\runtime -Directory -Filter 'desktop-*' | Where-Object {Test-Path (Join-Path $_.FullName 'B')} | Sort-Object LastWriteTime -Descending | Select-Object -First 1
 if(!$profile){throw 'Run test-desktop.ps1 first to create isolated test profiles'}
 $testExe=(Resolve-Path "$OutputDirectory\UiSmoke.exe").Path
 $resultRoot=(Resolve-Path build\tests\runtime).Path
 $runToken=[Guid]::NewGuid().ToString('N').Substring(0,8)
 $first=Start-Process -WindowStyle Hidden -FilePath $testExe -ArgumentList ('"'+$profile.FullName+'\A" 29431 "'+$resultRoot+'\wpf-A" '+$runToken) -PassThru
 $second=Start-Process -WindowStyle Hidden -FilePath $testExe -ArgumentList ('"'+$profile.FullName+'\B" 29432 "'+$resultRoot+'\wpf-B" '+$runToken) -PassThru
 $first.WaitForExit();$second.WaitForExit()
 Get-Content "$resultRoot\wpf-A.txt","$resultRoot\wpf-B.txt"
 if($first.ExitCode -ne 0 -or $second.ExitCode -ne 0){throw 'WPF UI test failed'}
} finally {Pop-Location}
